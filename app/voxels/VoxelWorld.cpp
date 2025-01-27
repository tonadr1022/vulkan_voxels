#include "VoxelWorld.hpp"

#include <tracy/Tracy.hpp>

#include "ChunkMeshManager.hpp"
#include "EAssert.hpp"
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
AutoCVarInt terrain_gen_chunks_y("world.terrain_gen_chunks_y", "Num chunks Y", 1);
AutoCVarFloat freq("world.terrain_freq", "Freq", 0.002);
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

void VoxelWorld::GenerateWorld(vec3 cam_pos) {
  ZoneScoped;
  fmt::println("generating world: radius {}", radius_);
  curr_cam_pos_ = cam_pos;
  prev_cam_pos_ = cam_pos;
  ivec3 iter;
  int y = terrain_gen_chunks_y.Get();
  ivec3 cp = CamPosToChunkPos(cam_pos);
  for (iter.y = 0; iter.y < y; iter.y++) {
    for (iter.x = cp.x - radius_; iter.x <= cp.x + radius_; iter.x++) {
      for (iter.z = cp.z - radius_; iter.z <= cp.z + radius_; iter.z++) {
        // if (iter.y == 0) fmt::println("{} {}", iter.x, iter.z);
        // TODO: use queue?
        to_gen_terrain_tasks_.emplace_back(iter);
        chunks.emplace(iter, ChunkState{});
        world_gen_chunk_payload_++;
      }
    }
  }
  world_start_timer_.Reset();
}

void VoxelWorld::Update(vec3 cam_pos) {
  ZoneScoped;
  std::lock_guard<std::mutex> lock(reset_mtx_);

  prev_cam_pos_ = curr_cam_pos_;
  curr_cam_pos_ = cam_pos;
  stats_.max_terrain_done_size =
      std::max(stats_.max_terrain_done_size, terrain_tasks_.done_tasks.size_approx());
  stats_.max_pool_size = std::max(stats_.max_pool_size, mesher_output_data_pool_.Size());
  stats_.max_pool_size2 = std::max(stats_.max_pool_size2, mesh_alg_pool_.allocs);
  if (tot_chunks_loaded_ != prev_world_start_finished_chunks_ &&
      tot_chunks_loaded_ == world_gen_chunk_payload_) {
    world_load_time_ = world_start_timer_.ElapsedMS();
    fmt::println("loaded in: {} ms", world_load_time_);
  }

  prev_world_start_finished_chunks_ = tot_chunks_loaded_;

  {
    ZoneScopedN("update chunks to create/destroy");
    ivec3 curr_cp = CamPosToChunkPos(curr_cam_pos_);
    ivec3 prev_cp = CamPosToChunkPos(prev_cam_pos_);
    // auto delete_ch = [this, &meshes_to_delete](ivec3 pos) {
    //   auto it = chunks.find(pos);
    //   if (it != chunks.end()) {
    //     meshes_to_delete.emplace_back(it->second);
    //   }
    // };
    auto make = [this](ivec3 pos) {
      auto it = chunks.find(pos);
      if (it == chunks.end()) {
        chunks.emplace(pos, ChunkState{});
        to_gen_terrain_tasks_.emplace_back(pos);
      } else if (it->second.state == ChunkState::None) {
        to_gen_terrain_tasks_.emplace_back(pos);
      }
    };
    if (curr_cp.x != prev_cp.x || curr_cp.z != prev_cp.z) {
      int y = terrain_gen_chunks_y.Get();

      ivec3 iter;
      ivec3 cp = curr_cp;
      for (iter.y = 0; iter.y < y; iter.y++) {
        for (iter.x = cp.x - radius_; iter.x <= cp.x + radius_; iter.x++) {
          for (iter.z = cp.z - radius_; iter.z <= cp.z + radius_; iter.z++) {
            make(iter);
            // to_gen_terrain_tasks_.emplace_back(iter);
            // world_gen_chunk_payload_++;
          }
        }
      }
      ivec3 diff = curr_cp - prev_cp;
      for (int axis = 0, other_axis = 2; axis <= 2; axis += 2, other_axis -= 2) {
        if (diff[axis]) {
          ivec3 pos;
          pos[axis] = prev_cp[axis] - radius_ * glm::sign(diff[axis]);
          for (pos[other_axis] = prev_cp[other_axis] - radius_;
               pos[other_axis] <= prev_cp[other_axis] + radius_; pos[other_axis]++) {
            for (pos.y = 0; pos.y < y; pos.y++) {
              auto it = chunks.find(pos);
              if (it != chunks.end()) {
                if (it->second.mesh_handle) {
                  meshes_to_delete.emplace_back(it->second.mesh_handle);
                }
                chunks.erase(it);
              }
            }
          }
        }
      }
      //   for (iter.x = cp.x - radius_ - unload_radius_pad;
      //        iter.x <= cp.x + radius_ + unload_radius_pad; iter.x++) {
      //     for (iter.z = cp.z - radius_ - unload_radius_pad;
      //          iter.z <= cp.z + radius_ + unload_radius_pad; iter.z++) {
      //       auto it = chunks.find(iter);
      //       if (it != chunks.end()) {
      //         meshes_to_delete.emplace_back(it->second.mesh_handle);
      //         chunks.erase(it);
      //       }
      //       // to_gen_terrain_tasks_.emplace_back(iter);
      //       // world_gen_chunk_payload_++;
      //     }
      //   }
      // }

      // for (int axis = 0, other_axis = 2; axis <= 2; axis += 2, other_axis -= 2) {
      //   if (curr_cp[axis] > prev_cp[axis]) {
      //     // these are chunks that need generation
      //     ivec3 iter = curr_cp;
      //     for (iter[axis] = prev_cp[axis] + radius_ + 1; iter[axis] <= curr_cp[axis] + radius_;
      //          iter[axis]++) {
      //       for (iter.y = 0; iter.y < y; iter.y++) {
      //         for (iter[other_axis] = curr_cp[other_axis] - radius_;
      //              iter[other_axis] <= curr_cp[other_axis] + radius_; iter[other_axis]++) {
      //           make(iter);
      //           fmt::println("making i {}, axis {}", iter[axis], axis);
      //         }
      //       }
      //     }
      //     iter = curr_cp;
      //     for (iter[axis] = prev_cp[axis] - radius_; iter[axis] < curr_cp[axis] - radius_;
      //          iter[axis]++) {
      //       delete_ch(iter);
      //       fmt::println("deleting i {}, axis {}", iter[axis], axis);
      //     }
      //   } else if (curr_cp[axis] < prev_cp[axis]) {
      //     ivec3 iter = curr_cp;
      //     for (iter[axis] = curr_cp[axis] + radius_ + 1; iter[axis] <= prev_cp[axis] + radius_;
      //          iter[axis]++) {
      //       delete_ch(iter);
      //       fmt::println("deleting i {}, axis {}", iter[axis], axis);
      //     }
      //     for (iter[axis] = curr_cp[axis] - radius_; iter[axis] < prev_cp[axis] - radius_;
      //          iter[axis]++) {
      //       make(iter);
      //       fmt::println("making i {}, axis {}", iter[axis], axis);
      //     }
      //   }
      // }
    }
  }

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
        tot_chunks_loaded_++;
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
        tot_chunks_loaded_++;
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
        int m = 1;
        u.mult = 1 << (m - 1);
        // fmt::println("{}", u.mult);
        u.pos = chunk_pool_.Get(mesh_task.chunk_handle)->pos * CS * u.mult;
        for (int i = 0; i < 6; i++) {
          u.vert_counts[i] = alg_data.face_vertex_lengths[i];
        }
        chunk_mesh_uploads_.emplace_back(u);
        stats_.tot_meshes++;
      }
      mesh_tasks_.in_flight--;
      chunk_pool_.Free(mesh_task.chunk_handle);
      mesh_alg_pool_.Free(mesh_task.alg_data_handle);
      mesher_output_data_pool_.Free(mesh_task.output_data_handle);
      tot_chunks_loaded_++;
    }
  }
  ChunkMeshManager::Get().FreeMeshes(meshes_to_delete);
  meshes_to_delete.clear();

  if (chunk_mesh_uploads_.size()) {
    // TODO: fix
    mesh_handle_alloc_buffer_.clear();
    mesh_handle_alloc_buffer_.resize(chunk_mesh_uploads_.size());
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_, mesh_handle_alloc_buffer_);
    for (size_t i = 0; i < chunk_mesh_uploads_.size(); i++) {
      auto pos = chunk_mesh_uploads_[i].pos / CS;
      auto it = chunks.find(pos);
      if (it == chunks.end()) {
        fmt::println(" {} {} {}", pos.x, pos.y, pos.z);
      }
      EASSERT(it != chunks.end());
      it->second.mesh_handle = mesh_handle_alloc_buffer_[i];
      it->second.state = ChunkState::Meshed;
    }
  }
}

TerrainGenResponse VoxelWorld::ProcessTerrainTask(const TerrainGenTask& task) {
  ZoneScoped;
  // ChunkPaddedHeightMapGrid heights;
  // FloatArray3D<i8vec3{PCS}> white_noise_floats;
  // ChunkPaddedHeightMapFloats height_map_floats;
  // HeightMapFloats<i8vec3{PCS}> white_noise_floats;
  auto* chunk = chunk_pool_.Get(task.chunk_handle);
  chunk->grid.Clear();
  // noise.FillNoise2D(height_map_floats, ivec2{chunk->pos.x, chunk->pos.z} * CS, uvec2{PCS}, m);
  // gen::NoiseToHeights(height_map_floats, heights,
  //                     {0, (((terrain_gen_chunks_y.Get() * CS / m) - 1))});
  auto* height_map = GetHeightMap(chunk->pos.x, chunk->pos.z);
  // gen::FillSphere<PCS>(chunk->grid, 128);
  gen::FillChunk(chunk->grid, chunk->pos * CS, *height_map, [](int, int, int) {
    // return (rand() % 255) + 1;
    return 128;
  });
  // for (int z = 2; z < 4; z++) {
  //   for (int y = 2; y < 4; y++) {
  //     for (int x = 2; x < 4; x++) {
  //       chunk->grid.Set(x, y, z, 128);
  //     }
  //   }
  // }
  // gen::NoiseToHeights(height_map_floats, heights, {0, terrain_gen_chunks_y.Get() * CS});
  // int i = 0;
  // gen::FillSphere<i8vec3{PCS}>(chunk->grid, [&i, &white_noise_floats]() {
  //   return std::fmod((white_noise_floats[i++ % PCS2] + 1.f) * 128.f, 254) + 1;
  // });
  // gen::FillChunk(
  //     task.chunk->grid, task.chunk->pos * CS, heights, [&white_noise_floats](int x, int y, int
  //     z)
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

  if (data->vertex_cnt) {
    task.staging_copy_idx =
        ChunkMeshManager::Get().CopyChunkToStaging(data->vertices.data(), data->vertex_cnt);
  }
  return task;
}

void VoxelWorld::DrawImGuiStats() {
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
  ImGui::Text("Quad count: %ld, quad mem size: %ld mb", ChunkMeshManager::Get().QuadCount(),
              ChunkMeshManager::Get().QuadCount() * ChunkMeshManager::QuadSize / 1024 / 1024);
  ImGui::Text("done: %d", tot_chunks_loaded_);
  ImGui::Text("tot chunks: %d", world_gen_chunk_payload_);
  ImGui::Text("Final world load time: %f", world_load_time_);
  ImGui::Text("meshes: %ld, quads: %ld, avg mesh quads: %ld", stats_.tot_meshes, stats_.tot_quads,
              stats_.tot_quads / std::max(stats_.tot_meshes, 1ul));
  ImGui::DragInt("radius", &radius_);
}

void VoxelWorld::Shutdown() {
  FreeAllMeshes();
  initalized_ = false;
}

void VoxelWorld::ResetInternal() {
  thread_pool.wait();
  tot_chunks_loaded_ = 0;
  prev_world_start_finished_chunks_ = -1;
  world_gen_chunk_payload_ = 0;
  FreeAllMeshes();
  stats_ = {};
  chunk_mesh_uploads_.clear();
  ResetPools();
}
void VoxelWorld::Reset() {
  ResetInternal();
  GenerateWorld(curr_cam_pos_);
}

void VoxelWorld::ResetPools() {
  chunk_pool_.ClearNoDealloc();
  mesh_alg_pool_.ClearNoDealloc();
  mesher_output_data_pool_.ClearNoDealloc();
  height_map_pool_.ClearNoDealloc();
  height_map_pool_idx_cache_.clear();
  while (terrain_tasks_.in_flight > 0 || mesh_tasks_.in_flight > 0) {
    Update(curr_cam_pos_);
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

ivec3 VoxelWorld::CamPosToChunkPos(vec3 cam_pos) { return ivec3(cam_pos) / CS; }

void VoxelWorld::FreeAllMeshes() {
  std::vector<uint32_t> to_free;
  to_free.reserve(chunks.size());
  for (auto& [pos, data] : chunks) {
    to_free.emplace_back(data.mesh_handle);
  }
  ChunkMeshManager::Get().FreeMeshes(to_free);
}
