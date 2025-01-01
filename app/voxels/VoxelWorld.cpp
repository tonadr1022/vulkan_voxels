#include "VoxelWorld.hpp"

#include "application/ThreadPool.hpp"
#include "imgui.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Terrain.hpp"
#include "voxels/Types.hpp"

void VoxelWorld::Init() {
  max_terrain_tasks_ = num_threads_ * 2;
  max_mesh_tasks_ = num_threads_ * 2;

  // TODO: refactor the counts here
  mesh_alg_pool_.Init(max_mesh_tasks_);
  mesher_output_data_pool_.Init(max_mesh_tasks_);
  grid_pool_.Init(max_terrain_tasks_ + max_mesh_tasks_);

  noise_generator_pool_.Init(max_terrain_tasks_);
  for (auto& n : noise_generator_pool_.data) {
    // TODO: config
    n.Init(seed_, 0.005, 4);
  }
  GenerateWorld();
}

void VoxelWorld::GenerateWorld() {
  ZoneScoped;
  ivec3 iter{0};
  int radius = 40;
  for (iter.x = 0; iter.x <= radius; iter.x++) {
    for (iter.z = 0; iter.z <= radius; iter.z++) {
      to_gen_terrain_tasks_.emplace_back(iter);
      world_gen_chunk_payload++;
    }
  }
  t.Reset();
}

void VoxelWorld::Update() {
  stats.max_terrain_done_size =
      std::max(stats.max_terrain_done_size, terrain_tasks_.done_tasks.size_approx());
  stats.max_pool_size = std::max(stats.max_pool_size, mesher_output_data_pool_.allocs);
  stats.max_pool_size2 = std::max(stats.max_pool_size2, mesh_alg_pool_.allocs);
  stats.max_pool_size3 = std::max(stats.max_pool_size3, noise_generator_pool_.allocs);
  ZoneScoped;
  if (done != prev_done && done == world_gen_chunk_payload) {
    world_load_time = t.ElapsedMS();
  }
  prev_done = done;

  {
    ZoneScopedN("proc terrain to complete");
    while (terrain_tasks_.in_flight < max_terrain_tasks_ && !to_gen_terrain_tasks_.empty()) {
      auto pos = to_gen_terrain_tasks_.back();
      to_gen_terrain_tasks_.pop_back();
      GridAndPos* grid_and_pos = grid_pool_.Alloc();
      EASSERT(grid_and_pos);
      grid_and_pos->pos = pos;
      gen::FBMNoise* noise = noise_generator_pool_.Alloc();
      EASSERT(noise);
      TerrainGenTask terrain_task{grid_and_pos, pos, noise};
      terrain_tasks_.in_flight++;
      thread_pool.detach_task([terrain_task, this]() mutable {
        EASSERT(terrain_task.noise);
        terrain_tasks_.done_tasks.enqueue(ProcessTerrainTask(terrain_task));
      });
      terrain_tasks_.to_complete_task_queue_size++;
    }
  }

  {
    ZoneScopedN("finished terrain tasks");
    // TODO: refactor
    TerrainGenResponse terrain_response;
    while (terrain_tasks_.in_flight > 0 && mesh_ts_.in_flight < max_terrain_tasks_ &&
           terrain_tasks_.done_tasks.try_dequeue(terrain_response)) {
      uint32_t alg_data_alloc = mesh_alg_pool_.Alloc();
      MesherOutputData* data = mesher_output_data_pool_.Alloc();
      EASSERT(data);
      MeshTask task{data, alg_data_alloc, terrain_response.grid};
      noise_generator_pool_.Free(terrain_response.noise);
      mesh_ts_.in_flight++;
      thread_pool.detach_task(
          [task, this]() mutable { mesh_ts_.done_tasks.enqueue(ProcessMeshTask(task)); });
      terrain_tasks_.to_complete_task_queue_size--;
      terrain_tasks_.in_flight--;
    }
  }

  MeshTask mesh_task;
  {
    ZoneScopedN("chunk mesh upload process");
    chunk_mesh_uploads_.clear();
    while (mesh_ts_.in_flight > 0 && mesh_ts_.done_tasks.try_dequeue(mesh_task)) {
      auto& alg_data = *mesh_alg_pool_.Get(mesh_task.alg_data_idx);
      if (mesh_task.data->vertex_cnt) {
        auto chunk_pos = mesh_task.grid->pos;
        for (int i = 0; i < 6; i++) {
          if (alg_data.face_vertex_lengths[i]) {
            uint32_t base_instance =
                (i << 24) | (chunk_pos.z << 16) | (chunk_pos.y << 16) | (chunk_pos.x);
            ChunkMeshUpload u;
            u.pos = chunk_pos;
            u.count = alg_data.face_vertex_lengths[i];
            u.first_instance = base_instance;
            u.data = &mesh_task.data->vertices[alg_data.face_vertices_start_indices[i]];
            chunk_mesh_uploads_.emplace_back(u);
            stats.tot_quads += u.count;
            stats.tot_meshes++;
          }
        }
      }
      mesh_ts_.in_flight--;
      grid_pool_.Free(mesh_task.grid);
      mesh_alg_pool_.Free(mesh_task.alg_data_idx);
      mesher_output_data_pool_.Free(mesh_task.data);
      done++;
    }
  }

  if (chunk_mesh_uploads_.size()) {
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_);
  }
}

TerrainGenResponse VoxelWorld::ProcessTerrainTask(TerrainGenTask& task) {
  ZoneScoped;
  EASSERT(task.noise);
  auto& fbm_noise = *task.noise;
  ChunkPaddedHeightMapGrid heights;
  ChunkPaddedHeightMapFloats height_map_floats;
  HeightMapFloats<i8vec3{PCS}> white_noise_floats;
  fbm_noise.FillWhiteNoise<i8vec3{PCS}>(white_noise_floats, ivec2{task.pos.x, task.pos.z} * CS);
  fbm_noise.FillNoise<i8vec3{PCS}>(height_map_floats, ivec2{task.pos.x, task.pos.z} * CS);
  gen::NoiseToHeights(height_map_floats, heights, {0, 32});
  gen::FillChunk(task.grid->grid, heights, [&white_noise_floats](int x, int, int z) {
    return std::fmod((white_noise_floats[(x * PCS) + z] + 1.f) * 128.f, 254) + 1;
  });
  return {task.grid, task.pos, task.noise};
}

MeshTask VoxelWorld::ProcessMeshTask(MeshTask& task) {
  ZoneScoped;
  auto& grid = task.grid;
  MeshAlgData* alg_data = mesh_alg_pool_.Get(task.alg_data_idx);
  EASSERT(alg_data);
  alg_data->mask = &grid->grid.mask;
  GenerateMesh(grid->grid.grid.grid, *alg_data, *task.data);
  return task;
}

void VoxelWorld::DrawImGuiStats() const {
  if (ImGui::TreeNodeEx("maxes", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("terrain done queue: %ld", stats.max_terrain_done_size);
    ImGui::Text("terrain to complete queue: %ld", stats.max_terrain_to_complete_size);
    ImGui::Text("mesher_output_data_pool_ : %ld", stats.max_pool_size);
    ImGui::Text("mesher_output_data_pool_ max : %ld", stats.max_pool_size);
    ImGui::Text("mesh_alg_pool_: %ld", stats.max_pool_size2);
    ImGui::Text("noise_generator_pool_: %ld", stats.max_pool_size3);

    ImGui::TreePop();
  }
  ImGui::Text("done: %d", done);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload);
  ImGui::Text("Final world load time: %f", world_load_time);
  ImGui::Text("meshes: %ld, quads: %ld, avg mesh quads: %ld", stats.tot_meshes, stats.tot_quads,
              stats.tot_quads / std::max(stats.tot_meshes, 1ul));
}
