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
  mesh_alg_pool_.Init(max_mesh_tasks_ * 3);
  mesher_output_data_pool_.Init(max_mesh_tasks_ * 3);

  noise_generator_pool_.Init(max_terrain_tasks_ * 2);
  for (auto& n : noise_generator_pool_.data) {
    // TODO: config
    n.Init(seed_, 0.005, 4);
  }
  GenerateWorld();
}

void VoxelWorld::GenerateWorld() {
  ZoneScoped;
  ivec3 iter{0};
  int radius = 50;
  for (iter.x = 0; iter.x <= radius; iter.x++) {
    for (iter.z = 0; iter.z <= radius; iter.z++) {
      to_gen_terrain_tasks_.emplace_back(iter);
      world_gen_chunk_payload++;
    }
  }
  t.Reset();
}

void VoxelWorld::Update() {
  ZoneScoped;
  if (done != prev_done && done == world_gen_chunk_payload) {
    world_load_time = t.ElapsedMS();
  }
  prev_done = done;

  {
    ZoneScopedN("proc terrain to complete");
    int i = 0;
    while (terrain_tasks_.to_complete_task_queue_size < max_terrain_tasks_ &&
           !to_gen_terrain_tasks_.empty()) {
      i++;
      auto pos = to_gen_terrain_tasks_.back();
      to_gen_terrain_tasks_.pop_back();
      // TODO: leak
      grids_.emplace_back(std::make_unique<PaddedChunkGrid3D>());
      grid_positions_.emplace_back(pos);
      gen::FBMNoise* noise = noise_generator_pool_.Alloc();
      assert(noise);
      TerrainGenTask terrain_task{grids_.size() - 1, pos, noise};
      terrain_tasks_.to_complete_tasks.enqueue(terrain_task);
      terrain_tasks_.to_complete_task_queue_size++;
    }
    if (i) {
      fmt::println("i {}", i);
    }
  }

  TerrainGenTask terrain_task;
  {
    ZoneScopedN("enqueue process terrain tasks");
    while (terrain_tasks_.in_flight < max_terrain_tasks_ &&
           terrain_tasks_.to_complete_tasks.try_dequeue(terrain_task)) {
      terrain_tasks_.to_complete_task_queue_size--;
      terrain_tasks_.in_flight++;
      thread_pool.detach_task([terrain_task, this]() mutable {
        assert(terrain_task.noise);
        terrain_tasks_.done_tasks.enqueue(ProcessTerrainTask(terrain_task));
      });
    }
  }

  MeshTask mesh_task;
  {
    ZoneScopedN("enqueue mesh tasks");
    while (mesh_ts_.in_flight < max_terrain_tasks_ &&
           mesh_ts_.to_complete_tasks.try_dequeue(mesh_task)) {
      mesh_ts_.in_flight++;
      thread_pool.detach_task(
          [mesh_task, this]() mutable { mesh_ts_.done_tasks.enqueue(ProcessMeshTask(mesh_task)); });
    }
  }

  {
    ZoneScopedN("finished terrain tasks");
    // TODO: refactor
    TerrainGenResponse terrain_response;
    while (terrain_tasks_.in_flight > 0 &&
           terrain_tasks_.done_tasks.try_dequeue(terrain_response)) {
      terrain_tasks_.in_flight--;
      uint32_t alg_data_alloc = mesh_alg_pool_.Alloc();
      MesherOutputData* data = mesher_output_data_pool_.Alloc();
      assert(data);
      MeshTask task{data, alg_data_alloc, terrain_response.grid_idx};
      noise_generator_pool_.Free(terrain_response.noise);
      mesh_ts_.to_complete_tasks.enqueue(task);
    }
  }

  {
    ZoneScopedN("chunk mesh upload process");
    chunk_mesh_uploads_.clear();
    while (mesh_ts_.in_flight > 0 && mesh_ts_.done_tasks.try_dequeue(mesh_task)) {
      mesh_ts_.in_flight--;
      auto& alg_data = *mesh_alg_pool_.Get(mesh_task.alg_data_idx);
      if (mesh_task.data->vertex_cnt) {
        auto chunk_pos = grid_positions_[mesh_task.grid_idx];
        for (int i = 0; i < 6; i++) {
          if (alg_data.face_vertex_lengths[i]) {
            uint32_t base_instance =
                (i << 24) | (chunk_pos.z << 16) | (chunk_pos.y << 16) | (chunk_pos.x);
            ChunkMeshUpload u;
            u.count = alg_data.face_vertex_lengths[i];
            u.first_instance = base_instance;
            u.data = &mesh_task.data->vertices[alg_data.face_vertices_start_indices[i]];
            chunk_mesh_uploads_.emplace_back(u);
          }
        }
      }
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
  assert(task.noise);
  auto& fbm_noise = *task.noise;
  ChunkPaddedHeightMapGrid heights;
  ChunkPaddedHeightMapFloats height_map_floats;
  HeightMapFloats<i8vec3{PCS}> white_noise_floats;
  fbm_noise.FillWhiteNoise<i8vec3{PCS}>(white_noise_floats, ivec2{task.pos.x, task.pos.z} * CS);
  fbm_noise.FillNoise<i8vec3{PCS}>(height_map_floats, ivec2{task.pos.x, task.pos.z} * CS);
  gen::NoiseToHeights(height_map_floats, heights, {0, 32});
  gen::FillChunk(*grids_[task.grid_idx], heights, 1);
  gen::FillChunk(*grids_[task.grid_idx], heights, [&white_noise_floats](int x, int, int z) {
    return std::fmod((white_noise_floats[(x * PCS) + z] + 1.f) * 128.f, 254) + 1;
  });
  return {task.grid_idx, task.pos, task.noise};
}

MeshTask VoxelWorld::ProcessMeshTask(MeshTask& task) {
  ZoneScoped;
  auto& grid = *grids_[task.grid_idx];
  MeshAlgData* alg_data = mesh_alg_pool_.Get(task.alg_data_idx);
  assert(alg_data);
  alg_data->mask = &grid.mask;
  GenerateMesh(grid.grid.grid, *alg_data, *task.data);
  return task;
}

void VoxelWorld::DrawImGuiStats() const {
  ImGui::Text("done: %d", done);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload);
  ImGui::Text("Final world load time: %f", world_load_time);
}
