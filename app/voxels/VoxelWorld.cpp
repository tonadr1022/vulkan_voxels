#include "VoxelWorld.hpp"

#include "ChunkMeshManager.hpp"
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
  initalized_ = true;
}

void VoxelWorld::GenerateWorld(int radius) {
  ZoneScoped;
  ivec3 iter{0};
  for (iter.x = -radius; iter.x <= radius; iter.x++) {
    for (iter.z = -radius; iter.z <= radius; iter.z++) {
      // TODO: use queue?
      to_gen_terrain_tasks_.emplace_back(iter);
      world_gen_chunk_payload_++;
    }
  }
  world_start_timer_.Reset();
}

void VoxelWorld::Update() {
  stats_.max_terrain_done_size =
      std::max(stats_.max_terrain_done_size, terrain_tasks_.done_tasks.size_approx());
  stats_.max_pool_size = std::max(stats_.max_pool_size, mesher_output_data_pool_.allocs);
  stats_.max_pool_size2 = std::max(stats_.max_pool_size2, mesh_alg_pool_.allocs);
  stats_.max_pool_size3 = std::max(stats_.max_pool_size3, noise_generator_pool_.allocs);
  ZoneScoped;
  if (world_start_finished_chunks_ != prev_world_start_finished_chunks_ &&
      world_start_finished_chunks_ == world_gen_chunk_payload_) {
    worldNload_time_ = world_start_timer_.ElapsedMS();
  }
  prev_world_start_finished_chunks_ = world_start_finished_chunks_;

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
    }
  }

  {
    ZoneScopedN("finished terrain tasks");
    // TODO: refactor
    TerrainGenResponse terrain_response;
    while (terrain_tasks_.in_flight > 0 && mesh_tasks_.in_flight < max_terrain_tasks_ &&
           terrain_tasks_.done_tasks.try_dequeue(terrain_response)) {
      uint32_t alg_data_alloc = mesh_alg_pool_.Alloc();
      MesherOutputData* data = mesher_output_data_pool_.Alloc();
      EASSERT(data);
      MeshTask task{data, alg_data_alloc, terrain_response.grid};
      noise_generator_pool_.Free(terrain_response.noise);
      mesh_tasks_.in_flight++;
      thread_pool.detach_task(
          [task, this]() mutable { mesh_tasks_.done_tasks.enqueue(ProcessMeshTask(task)); });
      terrain_tasks_.in_flight--;
    }
  }

  MeshTask mesh_task;
  {
    ZoneScopedN("chunk mesh upload process");
    chunk_mesh_uploads_.clear();
    while (mesh_tasks_.in_flight > 0 && mesh_tasks_.done_tasks.try_dequeue(mesh_task)) {
      auto& alg_data = *mesh_alg_pool_.Get(mesh_task.alg_data_idx);
      if (mesh_task.data->vertex_cnt) {
        stats_.tot_quads += mesh_task.data->vertex_cnt;
        ChunkMeshUpload u{};
        u.data = mesh_task.data->vertices.data();
        u.pos = mesh_task.grid->pos;
        u.tot_cnt = mesh_task.data->vertex_cnt;
        for (int i = 0; i < 6; i++) {
          u.counts[i] = alg_data.face_vertex_lengths[i];
        }
        chunk_mesh_uploads_.emplace_back(u);
        stats_.tot_meshes++;
      }
      mesh_tasks_.in_flight--;
      grid_pool_.Free(mesh_task.grid);
      mesh_alg_pool_.Free(mesh_task.alg_data_idx);
      mesher_output_data_pool_.Free(mesh_task.data);
      world_start_finished_chunks_++;
    }
  }

  if (chunk_mesh_uploads_.size()) {
    // TODO: fix
    auto old_count = mesh_handles_.size();
    mesh_handles_.resize(mesh_handles_.size() + chunk_mesh_uploads_.size());
    std::span<ChunkAllocHandle> s(mesh_handles_.begin() + old_count, mesh_handles_.size());
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_, s);
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
  // int i = 0;
  // gen::FillSphere<i8vec3{PCS}>(task.grid->grid, [&i, &white_noise_floats]() {
  //   return std::fmod((white_noise_floats[i++ % PCS2] + 1.f) * 128.f, 254) + 1;
  // });
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
    ImGui::Text("terrain done queue: %ld", stats_.max_terrain_done_size);
    ImGui::Text("mesher_output_data_pool_ : %ld", stats_.max_pool_size);
    ImGui::Text("mesher_output_data_pool_ max : %ld", stats_.max_pool_size);
    ImGui::Text("mesh_alg_pool_: %ld", stats_.max_pool_size2);
    ImGui::Text("noise_generator_pool_: %ld", stats_.max_pool_size3);
    ImGui::TreePop();
  }
  ImGui::Text("done: %d", world_start_finished_chunks_);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload_);
  ImGui::Text("Final world load time: %f", worldNload_time_);
  ImGui::Text("meshes: %ld, quads: %ld, avg mesh quads: %ld", stats_.tot_meshes, stats_.tot_quads,
              stats_.tot_quads / std::max(stats_.tot_meshes, 1ul));
}

void VoxelWorld::Shutdown() {
  ChunkMeshManager::Get().FreeMeshes(mesh_handles_);
  initalized_ = false;
}

void VoxelWorld::Reset() {
  thread_pool.wait();
  ChunkMeshManager::Get().FreeMeshes(mesh_handles_);
  stats_ = {};
  chunk_mesh_uploads_.clear();
  mesh_handles_.clear();
  ResetPools();
}
void VoxelWorld::ResetPools() {
  grid_pool_.ClearNoDealloc();
  mesh_alg_pool_.ClearNoDealloc();
  mesher_output_data_pool_.ClearNoDealloc();
  noise_generator_pool_.ClearNoDealloc();
  while (terrain_tasks_.in_flight > 0 || mesh_tasks_.in_flight > 0) {
    Update();
  }
  terrain_tasks_.Clear();
  mesh_tasks_.Clear();
}
