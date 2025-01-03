#include "VoxelWorld.hpp"

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
AutoCVarInt terrain_gen_chunks_y("world.terrain_gen_chunks_y", "Num chunks Y", 10);
AutoCVarFloat freq("world.terrain_freq", "Freq", 0.001);
}  // namespace
void VoxelWorld::Init() {
  max_terrain_tasks_ = 200;
  max_mesh_tasks_ = 200;

  // TODO: refactor the counts here
  mesh_alg_pool_.Init(max_mesh_tasks_);
  mesher_output_data_pool_.Init(max_mesh_tasks_);
  chunk_pool_.Init(max_terrain_tasks_ + max_mesh_tasks_);

  noise_.Init(seed_, freq.GetFloat(), 4);
  initalized_ = true;
}

void VoxelWorld::GenerateWorld(int radius) {
  ZoneScoped;
  ivec3 iter;
  for (iter.x = -radius; iter.x <= radius; iter.x++) {
    for (iter.z = -radius; iter.z <= radius; iter.z++) {
      for (iter.y = 0; iter.y < terrain_gen_chunks_y.Get(); iter.y++) {
        // TODO: use queue?
        to_gen_terrain_tasks_.emplace_back(iter);
        world_gen_chunk_payload_++;
      }
    }
  }
  world_start_timer_.Reset();
}

void VoxelWorld::Update() {
  stats_.max_terrain_done_size =
      std::max(stats_.max_terrain_done_size, terrain_tasks_.done_tasks.size_approx());
  stats_.max_pool_size = std::max(stats_.max_pool_size, mesher_output_data_pool_.Size());
  stats_.max_pool_size2 = std::max(stats_.max_pool_size2, mesh_alg_pool_.allocs);
  ZoneScoped;
  if (world_start_finished_chunks_ != prev_world_start_finished_chunks_ &&
      world_start_finished_chunks_ == world_gen_chunk_payload_) {
    world_load_time_ = world_start_timer_.ElapsedMS();
  }
  prev_world_start_finished_chunks_ = world_start_finished_chunks_;

  {
    ZoneScopedN("proc terrain to complete");
    while (terrain_tasks_.in_flight < max_terrain_tasks_ && !to_gen_terrain_tasks_.empty()) {
      auto pos = to_gen_terrain_tasks_.back();
      to_gen_terrain_tasks_.pop_back();
      auto chunk_handle = chunk_pool_.Alloc();
      auto* chunk = chunk_pool_.Get(chunk_handle);
      chunk->grid = {};
      EASSERT(chunk);
      chunk->pos = pos;
      TerrainGenTask terrain_task{chunk_handle};
      terrain_tasks_.in_flight++;
      thread_pool.detach_task([terrain_task, this]() mutable {
        terrain_tasks_.done_tasks.enqueue(ProcessTerrainTask(terrain_task));
      });
    }
  }

  // {
  //   ZoneScopedN("finished terrain tasks");
  //   // TODO: refactor
  //   TerrainGenResponse terrain_response;
  //   while (terrain_tasks_.in_flight > 0 && mesh_tasks_.in_flight < max_mesh_tasks_ &&
  //          terrain_tasks_.done_tasks.try_dequeue(terrain_response)) {
  //     uint32_t alg_data_alloc = mesh_alg_pool_.Alloc();
  //     MesherOutputData* data = mesher_output_data_pool_.Alloc();
  //     EASSERT(data);
  //     MeshTask task{data, alg_data_alloc, terrain_response.grid};
  //     noise_generator_pool_.Free(terrain_response.noise);
  //     mesh_tasks_.in_flight++;
  //     terrain_tasks_.in_flight--;
  //     if (terrain_response.grid->grid.mask.AnySolid()) {
  //       thread_pool.detach_task(
  //           [task, this]() mutable { mesh_tasks_.done_tasks.enqueue(ProcessMeshTask(task)); });
  //     }
  //   }
  // }
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
        mesh_tasks_.to_complete.enqueue(task);
      } else {
        chunk_pool_.Free(terrain_response.grid);
      }
    }
  }
  {
    ZoneScopedN("dispatch mesh tasks");
    MeshTaskEnqueue enq_mesh_task;
    while (mesh_tasks_.in_flight < max_mesh_tasks_ &&
           mesh_tasks_.to_complete.try_dequeue(enq_mesh_task)) {
      mesh_tasks_.in_flight++;
      uint32_t data_handle = mesher_output_data_pool_.Alloc();
      uint32_t alg_data_alloc = mesh_alg_pool_.Alloc();
      thread_pool.detach_task(
          [this, data_handle, alg_data_alloc, chunk_handle = enq_mesh_task.chunk_handle]() mutable {
            MeshTaskResponse response;
            response.output_data_handle = data_handle;
            response.alg_data_handle = alg_data_alloc;
            response.chunk_handle = chunk_handle;
            mesh_tasks_.done_tasks.enqueue(ProcessMeshTask(response));
          });
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
  auto& noise = noise_;
  ChunkPaddedHeightMapGrid heights;
  FloatArray3D<i8vec3{PCS}> white_noise_floats;
  ChunkPaddedHeightMapFloats height_map_floats;
  // HeightMapFloats<i8vec3{PCS}> white_noise_floats;
  auto* chunk = chunk_pool_.Get(task.chunk_handle);
  noise.FillWhiteNoise<i8vec3{PCS}>(white_noise_floats, chunk->pos * CS);
  static AutoCVarInt chunk_mult("chunks.chunk_mult", "chunk mult", 2);
  // constexpr int Mults[] = {1, 2, 4, 8, 16, 32, 64};
  // fmt::println("get {}", chunk_mult.Get());
  auto m = chunk_mult.Get();
  EASSERT(m);
  noise.FillNoise2D(height_map_floats, ivec2{chunk->pos.x, chunk->pos.z} * CS, uvec2{PCS}, m);
  gen::NoiseToHeights(height_map_floats, heights,
                      {0, (((terrain_gen_chunks_y.Get() * CS / m) - 1))});
  gen::FillChunk(chunk->grid, chunk->pos * CS, heights, [](int, int, int) { return 128; });
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
    task.staging_copy_idx = ChunkMeshManager::Get().CopyChunkToStaging(data->vertices);
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
    ImGui::TreePop();
  }
  ImGui::Text("done: %d", world_start_finished_chunks_);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload_);
  ImGui::Text("Final world load time: %f", world_load_time_);
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
  chunk_pool_.ClearNoDealloc();
  mesh_alg_pool_.ClearNoDealloc();
  mesher_output_data_pool_.ClearNoDealloc();
  while (terrain_tasks_.in_flight > 0 || mesh_tasks_.in_flight > 0) {
    Update();
  }
  terrain_tasks_.Clear();
  mesh_tasks_.Clear();
}
