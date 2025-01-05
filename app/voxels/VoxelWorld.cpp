#include "VoxelWorld.hpp"

#include <tracy/Tracy.hpp>

#include "ChunkMeshManager.hpp"
#include "application/CVar.hpp"
#include "application/ThreadPool.hpp"
#include "application/Timer.hpp"
#include "imgui.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Terrain.hpp"
#include "voxels/Types.hpp"

namespace {
AutoCVarInt terrain_gen_chunks_y("world.terrain_gen_chunks_y", "Num chunks Y", 5);
AutoCVarFloat freq("world.terrain_freq", "Freq", 0.001);
}  // namespace
void VoxelWorld::Init() {
  max_terrain_tasks_ = 16;
  max_mesh_tasks_ = 16;

  // TODO: refactor the counts here
  mesh_alg_pool_.Init(max_mesh_tasks_);
  mesher_output_data_pool_.Init(max_mesh_tasks_);
  chunk_pool_.Init(max_terrain_tasks_ + max_mesh_tasks_);
  height_map_pool_.Init(10000);

  noise_.Init(seed_, freq.GetFloat(), 4);
  initalized_ = true;
}

void VoxelWorld::GenerateWorld(int radius) {
  ZoneScoped;
  fmt::println("generating world: radius {}", radius);
  ivec3 iter;
  int y = terrain_gen_chunks_y.Get();
  for (iter.y = 0; iter.y < y; iter.y++) {
    for (iter.x = -radius; iter.x <= radius; iter.x++) {
      for (iter.z = -radius; iter.z <= radius; iter.z++) {
        // TODO: use queue?
        to_gen_terrain_tasks_.emplace_back(iter);
        world_gen_chunk_payload_++;
      }
    }
  }
  world_start_timer_.Reset();
}

void VoxelWorld::Update() {
  ZoneScoped;
  // std::lock_guard<std::mutex> lock(height_map_mtx_);
  if (reset_req_) {
    reset_req_ = false;
    ResetInternal();
    GenerateWorld(*CVarSystem::Get().GetIntCVar("world.initial_load_radius"));
  }
  stats_.max_terrain_done_size =
      std::max(stats_.max_terrain_done_size, terrain_tasks_.done_tasks.size_approx());
  stats_.max_pool_size = std::max(stats_.max_pool_size, mesher_output_data_pool_.Size());
  stats_.max_pool_size2 = std::max(stats_.max_pool_size2, mesh_alg_pool_.allocs);
  if (tot_chunks_loaded != prev_world_start_finished_chunks_ &&
      tot_chunks_loaded == world_gen_chunk_payload_) {
    world_load_time_ = world_start_timer_.ElapsedMS();
    fmt::println("loaded in: {} ms", world_load_time_);
  }

  prev_world_start_finished_chunks_ = tot_chunks_loaded;

  {
    ZoneScopedN("finished terrain tasks and enqueue mesh");
    TerrainGenResponse terrain_response;
    while (terrain_tasks_.in_flight > 0 &&
           terrain_tasks_.done_tasks.try_dequeue(terrain_response)) {
      // here, i allow more terrain tasks to be enqueued, bnut there aren't enough grids?
      // fmt::println("grids before dec: {}", grid_pool_.allocs);
      terrain_tasks_.in_flight--;
      auto* chunk = chunk_pool_.Get(terrain_response.grid);
      if (chunk->grid.mask.AnySolid()) {
        MeshTaskEnqueue task;
        task.chunk_handle = terrain_response.grid;
        mesh_tasks_.to_complete.emplace(task);
      } else {
        tot_chunks_loaded++;
        chunk_pool_.Free(terrain_response.grid);
      }
    }
  }
  {
    ZoneScopedN("dispatch mesh tasks");
    while (mesh_tasks_.in_flight < max_mesh_tasks_ && mesh_tasks_.to_complete.size()) {
      MeshTaskResponse response;

      response.chunk_handle = mesh_tasks_.to_complete.front().chunk_handle;
      auto& chunk = *chunk_pool_.Get(response.chunk_handle);
      if (chunk.grid.mask.AllSet()) {
        mesh_tasks_.to_complete.pop();
        tot_chunks_loaded++;
        continue;
      }
      response.alg_data_handle = mesh_alg_pool_.Alloc();
      response.output_data_handle = mesher_output_data_pool_.Alloc();

      mesh_tasks_.to_complete.pop();
      thread_pool.detach_task([this, response]() mutable {
        mesh_tasks_.done_tasks.enqueue(ProcessMeshTask(response));
      });
      mesh_tasks_.in_flight++;
    }
  }

  {
    ZoneScopedN("proc terrain to complete");
    while (terrain_tasks_.in_flight < max_terrain_tasks_ && !to_gen_terrain_tasks_.empty()) {
      auto pos = to_gen_terrain_tasks_.back();
      to_gen_terrain_tasks_.pop_back();
      auto chunk_handle = chunk_pool_.Alloc();
      auto* chunk = chunk_pool_.Get(chunk_handle);
      EASSERT(chunk);
      chunk->pos = pos;
      TerrainGenTask terrain_task{chunk_handle};
      terrain_tasks_.in_flight++;
      {
        ZoneScopedN("detatch");
        thread_pool.detach_task([terrain_task, this]() {
          terrain_tasks_.done_tasks.enqueue(ProcessTerrainTask(terrain_task));
        });
      }
    }
  }

  MeshTaskResponse mesh_task;
  {
    ZoneScopedN("chunk mesh upload process");
    chunk_mesh_uploads_.clear();
    while (mesh_tasks_.in_flight > 0 && mesh_tasks_.done_tasks.try_dequeue(mesh_task)) {
      auto& alg_data = *mesh_alg_pool_.Get(mesh_task.alg_data_handle);
      auto& data = *mesher_output_data_pool_.Get(mesh_task.output_data_handle);
      if (data.vertex_cnt > 0) {
        stats_.tot_quads += data.vertex_cnt;
        ChunkMeshUpload u{};
        u.staging_copy_idx = mesh_task.staging_copy_idx;
        u.pos = chunk_pool_.Get(mesh_task.chunk_handle)->pos;
        for (int i = 0; i < 6; i++) {
          u.counts[i] = alg_data.face_vertex_lengths[i];
        }
        chunk_mesh_uploads_.emplace_back(u);
        stats_.tot_meshes++;
      }
      mesh_tasks_.in_flight--;
      chunk_pool_.Free(mesh_task.chunk_handle);
      mesh_alg_pool_.Free(mesh_task.alg_data_handle);
      mesher_output_data_pool_.Free(mesh_task.output_data_handle);
      tot_chunks_loaded++;
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

TerrainGenResponse VoxelWorld::ProcessTerrainTask(const TerrainGenTask& task) {
  ZoneScoped;
  // ChunkPaddedHeightMapGrid heights;
  // FloatArray3D<i8vec3{PCS}> white_noise_floats;
  // ChunkPaddedHeightMapFloats height_map_floats;
  // HeightMapFloats<i8vec3{PCS}> white_noise_floats;
  auto* chunk = chunk_pool_.Get(task.chunk_handle);
  // noise.FillWhiteNoise<i8vec3{PCS}>(white_noise_floats, chunk->pos * CS);
  static AutoCVarInt chunk_mult("chunks.chunk_mult", "chunk mult", 2);
  // constexpr int Mults[] = {1, 2, 4, 8, 16, 32, 64};
  auto m = chunk_mult.Get();
  EASSERT(m);
  {
    ZoneScopedN("Clear gird");
    chunk->grid.Clear();
  }
  auto* height_map = GetHeightMap(chunk->pos.x, chunk->pos.z);
  // noise.FillNoise2D(height_map_floats, ivec2{chunk->pos.x, chunk->pos.z} * CS, uvec2{PCS}, m);
  // gen::NoiseToHeights(height_map_floats, heights,
  //                     {0, (((terrain_gen_chunks_y.Get() * CS / m) - 1))});
  gen::FillChunk(chunk->grid, chunk->pos * CS, *height_map, [](int, int, int) {
    // return (rand() % 255) + 1;
    return 128;
  });

  // gen::NoiseToHeights(height_map_floats, heights, {0, terrain_gen_chunks_y.Get() * CS});
  // int i = 0;
  // gen::FillSphere<i8vec3{PCS}>(chunk->grid, [&i, &white_noise_floats]() {
  //   return std::fmod((white_noise_floats[i++ % PCS2] + 1.f) * 128.f, 254) + 1;
  // });
  // gen::FillChunk(
  //     task.chunk->grid, task.chunk->pos * CS, heights, [&white_noise_floats](int x, int y, int z)
  //     {
  //       constexpr const int MaxMaterial = 254;
  //       return std::fmod((white_noise_floats[(y * PCS2) + (x * PCS) + z] + 1.f) * MaxMaterial *
  //       0.5,
  //                        MaxMaterial) +
  //              1;
  //     });
  return {task.chunk_handle, chunk->pos};
}

MeshTaskResponse VoxelWorld::ProcessMeshTask(MeshTaskResponse& task) {
  ZoneScoped;
  auto& chunk = *chunk_pool_.Get(task.chunk_handle);
  MeshAlgData* alg_data = mesh_alg_pool_.Get(task.alg_data_handle);
  EASSERT(alg_data);
  alg_data->mask = &chunk.grid.mask;
  auto* data = mesher_output_data_pool_.Get(task.output_data_handle);
  GenerateMesh(chunk.grid.grid.grid, *alg_data, *data);
  if (data->vertices.size()) {
    // fmt::println("s {}", data->vertices.size());
    task.staging_copy_idx =
        ChunkMeshManager::Get().CopyChunkToStaging(data->vertices.data(), data->vertex_cnt);
    // fmt::println("task.staging_copy_idx {}", task.staging_copy_idx);
  }
  return task;
}

void VoxelWorld::DrawImGuiStats() const {
  if (ImGui::TreeNodeEx("maxes", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("terrain done queue: %ld", stats_.max_terrain_done_size);
    ImGui::Text("mesher_output_data_pool_ : %ld", stats_.max_pool_size);
    ImGui::Text("mesher_output_data_pool_ max : %ld", stats_.max_pool_size);
    ImGui::Text("mesh_alg_pool_: %ld", stats_.max_pool_size2);
    ImGui::Text("noise_generator_pool_: %ld", stats_.max_pool_size3);
    ImGui::Text("terrain tasks in flight: %ld", terrain_tasks_.in_flight);
    ImGui::Text("mesh tasks in flight: %ld", mesh_tasks_.in_flight);
    ImGui::TreePop();
  }
  ImGui::Text("Quad count: %ld", ChunkMeshManager::Get().QuadCount());
  ImGui::Text("done: %d", tot_chunks_loaded);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload_);
  ImGui::Text("Final world load time: %f", world_load_time_);
  ImGui::Text("meshes: %ld, quads: %ld, avg mesh quads: %ld", stats_.tot_meshes, stats_.tot_quads,
              stats_.tot_quads / std::max(stats_.tot_meshes, 1ul));
}

void VoxelWorld::Shutdown() {
  ChunkMeshManager::Get().FreeMeshes(mesh_handles_);
  initalized_ = false;
}

void VoxelWorld::ResetInternal() {
  thread_pool.wait();
  tot_chunks_loaded = 0;
  prev_world_start_finished_chunks_ = -1;
  world_gen_chunk_payload_ = 0;
  ChunkMeshManager::Get().FreeMeshes(mesh_handles_);
  stats_ = {};
  chunk_mesh_uploads_.clear();
  mesh_handles_.clear();
  ResetPools();
}
void VoxelWorld::Reset() { reset_req_ = true; }

void VoxelWorld::ResetPools() {
  chunk_pool_.ClearNoDealloc();
  mesh_alg_pool_.ClearNoDealloc();
  mesher_output_data_pool_.ClearNoDealloc();
  height_map_pool_.ClearNoDealloc();
  height_map_pool_idx_cache_.clear();
  while (terrain_tasks_.in_flight > 0 || mesh_tasks_.in_flight > 0) {
    Update();
  }
  terrain_tasks_.Clear();
  mesh_tasks_.Clear();
}

HeightMapData* VoxelWorld::GetHeightMap(int x, int y) {
  ZoneScoped;
  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    auto it = height_map_pool_idx_cache_.find({x, y});
    if (it != height_map_pool_idx_cache_.end()) {
      return height_map_pool_.Get(it->second);
    }
  }
  uint32_t idx;
  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    idx = height_map_pool_.Alloc();
  }
  ChunkPaddedHeightMapFloats height_map_floats;
  auto* height_map = height_map_pool_.Get(idx);
  // TODO: pass scale in
  noise_.FillNoise2D(height_map_floats, ivec2{x, y} * CS, uvec2{PCS}, 2);
  gen::NoiseToHeights(height_map_floats, *height_map,
                      {0, (terrain_gen_chunks_y.Get() * CS * 0.5) - 1});

  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    height_map_pool_idx_cache_.emplace(std::make_pair(x, y), idx);
  }
  return height_map;
}
