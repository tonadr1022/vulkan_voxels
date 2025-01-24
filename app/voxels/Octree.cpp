#include "Octree.hpp"

#include <cstdint>
#include <thread>

#include "ChunkMeshManager.hpp"
#include "EAssert.hpp"
#include "application/CVar.hpp"
#include "application/ThreadPool.hpp"
#include "imgui.h"

namespace {
AutoCVarFloat lod_thresh("terrain.lod_thresh", "lod threshold of terrain", 10.0);

template <typename T>
void DumpBits(T d, size_t size = sizeof(T)) {
  static_assert(std::is_integral_v<T>, "T must be an integral type.");
  for (int i = static_cast<int>(size) - 1; i >= 0; i--) {
    fmt::print("{}", ((d & (static_cast<T>(1) << i)) ? 1 : 0));
  }
  fmt::println("");
}

}  // namespace

void MeshOctree::Init() {
  lod_bounds_.reserve(AbsoluteMaxDepth);

  const int max_tasks = std::thread::hardware_concurrency() * 4;

  terrain_tasks_.Init(max_tasks);
  chunk_pool_.Init(max_tasks * 64);
  height_map_pool_.Init(50000);
  // TODO: fine tune
  mesh_alg_buf_.Init(1000);
  mesher_output_data_buf_.Init(1000);

  chunk_states_pool_.Init(MaxChunks);
  prev_cam_chunk_pos_ = ivec3{INT_MAX};

  noise_.Init(1, 0.005, 4);
  auto root = nodes_[0].AllocNode();
  EASSERT(root == 0);
  GetNode(0, 0)->chunk_state_handle = chunk_states_pool_.Alloc();

  UpdateLodBounds();
  Update(ivec3{0});
}

void MeshOctree::FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx,
                              uint32_t depth, ivec3 pos) {
  child_free_stack_.emplace_back(NodeQueueItem{node_idx, pos, depth});
  while (!child_free_stack_.empty()) {
    auto [curr_idx, curr_pos, curr_depth] = child_free_stack_.back();
    child_free_stack_.pop_back();
    auto* node = GetNode(curr_depth, curr_idx);
    EASSERT(curr_depth != max_depth_ || node->mask == 0);
    int i = 0;
    for (int y = 0; y < 2; y++) {
      for (int z = 0; z < 2; z++) {
        for (int x = 0; x < 2; x++, i++) {
          if (node->IsSet(i)) {
            child_free_stack_.emplace_back(NodeQueueItem{
                node->data[i],
                pos + (ivec3{x, y, z} * static_cast<int>(GetOffset(max_depth_ - depth - 1))),
                curr_depth + 1});
          }
        }
      }
    }
    node->ClearMask();
    if (curr_depth != depth) {
      ChunkState* s = chunk_states_pool_.Get(node->chunk_state_handle);
      if (s->mesh_handle) {
        meshes_to_free.emplace_back(s->mesh_handle);
        // fmt::println("freeing mesh {} ,depth {}, pos {} {} {}", s->mesh_handle, curr_depth,
        //              curr_pos.x, curr_pos.y, curr_pos.z);
        s->mesh_handle = 0;
      }
      chunk_states_pool_.Free(node->chunk_state_handle);
      FreeNode(curr_depth, curr_idx);
    }
  }
}

void MeshOctree::Reset() {
  std::vector<NodeQueueItem> node_q;
  std::vector<uint32_t> to_free;
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0});
  while (node_q.size()) {
    auto [node_idx, pos, depth] = node_q.back();
    node_q.pop_back();
    const auto* node = GetNode(depth, node_idx);
    EASSERT(node);
    ChunkState* s = chunk_states_pool_.Get(node->chunk_state_handle);
    if (s->mesh_handle) {
      to_free.emplace_back(s->mesh_handle);
      if (depth < max_depth_) {
        if (node->mask != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->mask == 0);
        }
      }
    } else {
      if (depth < max_depth_) {
        int off = GetOffset(max_depth_ - depth - 1);
        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
              if (node->IsSet(i)) {
                NodeQueueItem e;
                e.lod = depth + 1;
                e.pos = pos + ivec3{x, y, z} * off;
                e.node_idx = node->data[i];
                node_q.emplace_back(e);
              }
              i++;
            }
          }
        }
      }
    }
  }
  ChunkMeshManager::Get().FreeMeshes(to_free);
  for (auto& n : nodes_) {
    n.Clear();
  }
  nodes_[0].AllocNode();
  height_maps_.clear();
}

void MeshOctree::Update(vec3 cam_pos) {
  ZoneScoped;
  EASSERT(node_queue_.empty());
  auto new_cam_chunk_pos = ivec3(cam_pos) / CS;
  bool changed_chunk_pos = new_cam_chunk_pos != prev_cam_chunk_pos_;
  prev_cam_chunk_pos_ = new_cam_chunk_pos;
  if (changed_chunk_pos) {
    node_queue_.push_back(NodeQueueItem{0, vec3{0}, 0});
    while (!node_queue_.empty()) {
      auto [node_idx, pos, depth] = node_queue_.back();
      node_queue_.pop_back();
      ivec3 chunk_center = pos + ((1 << (max_depth_ - depth)) * HALFCS);

      {
        // TODO: other structures may be present in the chunk!
        auto* node = GetNode(depth, node_idx);
        ChunkState* s = chunk_states_pool_.Get(node->chunk_state_handle);
        if (!(s->data & ChunkState::DataFlagsTerrainGenDirty) && s->num_solid == 0) {
          continue;
        }
      }
      //
      // ivec3 chunk_center = pos + (1 << (MaxDepth - depth)) * HALFCS;
      // fmt::println("node pos {} {} {}, depth {}, cs {} {} {}", pos.x, pos.y, pos.z, depth,
      //              chunk_center.x, chunk_center.y, chunk_center.z);
      // mesh if far away enough not to split
      if (glm::distance(vec3(chunk_center), cam_pos) >=
              (lod_bounds_[depth] * lod_thresh.GetFloat()) ||
          depth == max_depth_) {
        auto* node = GetNode(depth, node_idx);
        ChunkState* s = chunk_states_pool_.Get(node->chunk_state_handle);
        if (depth < max_depth_) {
          FreeChildren(meshes_to_free_, node_idx, depth, pos);
        }
        if (s->GetNeedsGenOrMeshing()) {
          s->SetNeedsGenOrMeshing(false);
          to_mesh_queue_.emplace_back(NodeQueueItem{node_idx, pos, depth});
        }
      } else if (depth < max_depth_) {
        int off = GetOffset(max_depth_ - depth - 1);

        // TODO: refactor
        auto* node = nodes_[depth].Get(node_idx);
        ChunkState* s = chunk_states_pool_.Get(node->chunk_state_handle);
        if (s->mesh_handle) {
          meshes_to_free_.emplace_back(s->mesh_handle);
          s->SetNeedsGenOrMeshing(true);
          s->mesh_handle = 0;
        }

        // fmt::println("{} {} {}", pos.x, pos.z, depth);
        // InterpolateHeightMap(pos, depth, height_maps.at({pos.x, pos.z, depth}));

        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++, i++) {
              NodeQueueItem e;
              e.lod = depth + 1;
              e.pos = pos + ivec3{x, y, z} * off;
              auto* node = nodes_[depth].Get(node_idx);
              uint32_t child_node_idx;
              if (node->IsSet(i)) {
                child_node_idx = node->data[i];
              } else {
                child_node_idx = nodes_[e.lod].AllocNode();
                GetNode(e.lod, child_node_idx)->chunk_state_handle = chunk_states_pool_.Alloc();
                node->SetData(i, child_node_idx);
              }
              e.node_idx = child_node_idx;
              node_queue_.emplace_back(e);
            }
          }
        }
      }
    }
  }

  // fmt::println("no skip");
  // ChunkMeshManager::Get().chunk_quad_buffer_.draw_cmd_allocator.PrintHandles();
  // fmt::println("skip");
  // ChunkMeshManager::Get().chunk_quad_buffer_.draw_cmd_allocator.PrintHandles(true);
  {
    ZoneScopedN("fill chunk and mesh");

    // TODO:
    // use ring buffer for chunks and mesh alg data since they're temporary
    // chain terrain gen to meshing
    //
    //
    {
      ZoneScopedN("terrain task enqueue");
      while (terrain_tasks_.CanEnqueueTask() && !to_mesh_queue_.empty()) {
        auto [node_idx, pos, lod] = to_mesh_queue_.back();
        to_mesh_queue_.pop_back();
        auto chunk_handle = chunk_pool_.Alloc();
        auto* chunk = chunk_pool_.Get(chunk_handle);
        EASSERT(chunk);
        chunk->pos = pos;
        TerrainGenTask terrain_task{NodeKey{.lod = lod, .idx = node_idx}, chunk_handle,
                                    GetNode(lod, node_idx)->chunk_state_handle};
        terrain_tasks_.IncInFlight();
        thread_pool.detach_task([terrain_task, this]() {
          auto t = terrain_task;
          ProcessTerrainTask(t);
          auto* chunk = chunk_pool_.Get(terrain_task.chunk_gen_data_handle);
          auto* chunk_state = chunk_states_pool_.Get(terrain_task.chunk_state_handle);
          size_t num_solid = chunk->grid.mask.SolidCount();
          chunk_state->num_solid = num_solid;
          chunk_state->SetFlags(ChunkState::DataFlagsTerrainGenDirty, false);
          MeshGenTask mesh_task{};
          mesh_task.chunk_gen_data_handle = terrain_task.chunk_gen_data_handle;
          if (chunk && chunk_state && num_solid) {
            mesh_task.node_key = terrain_task.node_key;
            ProcessMeshGenTask(mesh_task);
            terrain_tasks_.done_tasks.enqueue(mesh_task);
          } else if (chunk) {
            terrain_tasks_.done_tasks.enqueue(mesh_task);
          }
        });
      }
    }

    {
      ZoneScopedN("finished mesh gpu upload tasks");
      MeshGenTask task;
      while (terrain_tasks_.InFlight() > 0 && terrain_tasks_.done_tasks.try_dequeue(task)) {
        terrain_tasks_.DecInFlight();
        auto* chunk = chunk_pool_.Get(task.chunk_gen_data_handle);
        EASSERT(chunk);
        chunk_pool_.Free(task.chunk_gen_data_handle);
        if (task.vert_count) {
          auto pos = chunk->pos;
          ChunkMeshUpload u;
          u.staging_copy_idx = task.staging_copy_idx;
          memcpy(u.vert_counts, task.vert_counts, sizeof(uint32_t) * 6);
          u.pos = pos;
          u.mult = 1 << (max_depth_ - task.node_key.lod);
          chunk_mesh_uploads_.emplace_back(u);
          chunk_mesh_node_keys_.emplace_back(task.node_key);
        }
        // fmt::println("meshing {} {} {} depth {}", pos.x, pos.y, pos.z, depth);
      }
    }
  }

  EASSERT(chunk_mesh_node_keys_.size() == chunk_mesh_uploads_.size());
  if (chunk_mesh_uploads_.size()) {
    mesh_handle_upload_buffer_.resize(chunk_mesh_uploads_.size());
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_, mesh_handle_upload_buffer_);
    for (size_t i = 0; i < chunk_mesh_node_keys_.size(); i++) {
      auto* node = GetNode(chunk_mesh_node_keys_[i]);
      EASSERT(node);
      auto* state = chunk_states_pool_.Get(node->chunk_state_handle);
      EASSERT(state);
      state->mesh_handle = mesh_handle_upload_buffer_[i];
    }
  }
  mesh_handle_upload_buffer_.clear();
  chunk_mesh_node_keys_.clear();
  chunk_mesh_uploads_.clear();
  // Validate();
  {
    ZoneScopedN("free meshes");
    if (meshes_to_free_.size()) {
      ChunkMeshManager::Get().FreeMeshes(meshes_to_free_);
      meshes_to_free_.clear();
    }
  }

  // cleanup old height maps
  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_height_map_cleanup_time_) >
      std::chrono::milliseconds(1000)) {
    ClearOldHeightMaps(now);
    last_height_map_cleanup_time_ = now;
  }
}

void MeshOctree::OnImGui() {
  size_t tot_nodes_cnt = 0;
  if (ImGui::Begin("Voxel Octree")) {
    for (uint32_t i = 0; i <= max_depth_; i++) {
      size_t s = nodes_[i].nodes.size() - nodes_[i].free_list.size();
      tot_nodes_cnt += s;
      ImGui::Text("%d: %zu", i, s);
    }
    ImGui::Text("Total Nodes: %zu", tot_nodes_cnt);
    ImGui::Text("height maps: %zu", height_maps_.size());
    ImGui::Text("terrain: to complete %zu, done %zu", terrain_tasks_.to_complete.size(),
                terrain_tasks_.done_tasks.size_approx());
    int d = max_depth_;
    if (ImGui::DragInt("max depth", &d, 0, AbsoluteMaxDepth)) {
      max_depth_ = d;
      UpdateLodBounds();
    }
  }
  ImGui::End();
}

void MeshOctree::Validate() {
  ZoneScoped;
  std::vector<NodeQueueItem> node_q;
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0});
  while (node_q.size()) {
    auto [node_idx, pos, depth] = node_q.back();
    node_q.pop_back();
    const auto* node = GetNode(depth, node_idx);
    EASSERT(node);
    if (chunk_states_pool_.Get(node->chunk_state_handle)->mesh_handle) {
      if (depth < max_depth_) {
        if (node->mask != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->mask == 0);
        }
      }
    } else {
      if (depth < max_depth_) {
        int off = GetOffset(max_depth_ - depth - 1);
        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
              if (node->IsSet(i)) {
                NodeQueueItem e;
                e.lod = depth + 1;
                e.pos = pos + ivec3{x, y, z} * off;
                e.node_idx = node->data[i];
                node_q.emplace_back(e);
              }
              i++;
            }
          }
        }
      }
    }
  }
}
// draw calls are made on vertices that aren't ready
// make the vertices ready before the draw calls

void MeshOctree::UpdateLodBounds() {
  lod_bounds_.resize(max_depth_ + 1);
  uint32_t k = 1;
  for (auto& b : lod_bounds_) {
    b = k * CS;
    k *= 2;
  }
  std::ranges::reverse(lod_bounds_);
}

void MeshOctree::ClearOldHeightMaps(const std::chrono::steady_clock::time_point& now) {
  ZoneScoped;
  for (auto it = height_maps_.begin(); it != height_maps_.end();) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.access) >
        std::chrono::milliseconds(1000)) {
      height_map_pool_.Free(it->second.height_map_pool_handle);
      it = height_maps_.erase(it);
    } else {
      ++it;
    }
  }
}

void MeshOctree::ProcessTerrainTask(TerrainGenTask& task) {
  ZoneScoped;
  auto* chunk = chunk_pool_.Get(task.chunk_gen_data_handle);
  chunk->grid.Clear();
  auto& hm = GetHeightMap(chunk->pos.x, chunk->pos.z, task.node_key.lod);
  // int color = 128;
  int color = task.node_key.lod * 30;

  // gen::FillSphere<PCS>(chunk->grid, color);
  // return;
  if (!ChunkInHeightMapRange(hm.range, task.node_key.lod, chunk->pos)) {
    return;
  }
  int scale = (1 << (max_depth_ - task.node_key.lod));
  for (int y = 0; y < PaddedChunkGrid3D::Dims.y; y++) {
    int adj_y = (y * scale) + chunk->pos.y;
    int i = 0;
    for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
      for (int x = 0; x < PaddedChunkGrid3D::Dims.x; x++, i++) {
        if (adj_y < ((hm.heights[i]))) {
          chunk->grid.Set(x, y, z, color);
        }
      }
    }
  }
  // gen::FillChunkNoCheck(chunk->grid, chunk->pos, hm, [c](int, int, int) { return c; });
}

void MeshOctree::ProcessMeshGenTask(MeshGenTask& task) {
  ZoneScoped;
  auto& chunk = *chunk_pool_.Get(task.chunk_gen_data_handle);

  MeshAlgData* alg_data{};
  MesherOutputData* data{};
  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    alg_data = mesh_alg_buf_.Allocate();
    data = mesher_output_data_buf_.Allocate();
  }
  EASSERT(alg_data && data);
  alg_data->mask = &chunk.grid.mask;
  GenerateMesh(chunk.grid.grid.grid, *alg_data, *data);
  task.vert_count = data->vertex_cnt;
  if (data->vertex_cnt) {
    task.staging_copy_idx =
        ChunkMeshManager::Get().CopyChunkToStaging(data->vertices.data(), data->vertex_cnt);
    for (int i = 0; i < 6; i++) {
      task.vert_counts[i] = alg_data->face_vertex_lengths[i];
    }
  }
}

HeightMapData& MeshOctree::GetHeightMap(int x, int z, int lod) {
  ZoneScoped;

  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    auto it = height_maps_.find({x, z, lod});
    if (it != height_maps_.end()) {
      it->second.access = std::chrono::steady_clock::now();
      return *height_map_pool_.Get(it->second.height_map_pool_handle);
    }
  }
  uint32_t handle;
  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    handle = height_map_pool_.Alloc();
  }
  HeightMapData* hm = height_map_pool_.Get(handle);

  uint32_t scale = (1 << (max_depth_ - lod));
  static AutoCVarFloat freq("terrain.freq", "freq of terrain", 0.00005);
  float adj_freq = freq.GetFloat() * static_cast<float>(scale);
  HeightMapFloats floats;
  noise_.fbm->GenUniformGrid2D(floats.data(), x / scale, z / scale, PCS, PCS, adj_freq, seed_);
  static AutoCVarInt maxheight("terrain.maxheight", "max height", 10000);
  gen::NoiseToHeights(floats, *hm, {0, maxheight.Get()});

  OctreeHeightMapData data{.access = std::chrono::steady_clock::now(),
                           .height_map_pool_handle = handle};
  {
    std::lock_guard<std::mutex> lock(height_map_mtx_);
    height_maps_.emplace(ivec3{x, z, lod}, data);
  }
  return *hm;
}

// uint32_t MeshOctree::GetHeightMap(int x, int z, int lod) {
//   ZoneScoped;
//   auto it = height_maps_.find({x, z, lod});
//   if (it != height_maps_.end()) {
//     it->second.access = std::chrono::steady_clock::now();
//     return it->second.height_map_pool_handle;
//     // return *height_map_pool_.Get(it->second.height_map_pool_handle);
//   }
//   auto res = height_maps_.emplace(ivec3{x, z, lod}, OctreeHeightMapData{});
//   res.first->second.height_map_pool_handle = height_map_pool_.Alloc();
//   HeightMapData* hm = height_map_pool_.Get(res.first->second.height_map_pool_handle);
//
//   uint32_t scale = (1 << (max_depth_ - lod));
//
//   static AutoCVarFloat freq("terrain.freq", "freq of terrain", 0.00005);
//   float adj_freq = freq.GetFloat() * static_cast<float>(scale);
//   HeightMapFloats floats;
//   noise_.fbm->GenUniformGrid2D(floats.data(), x / scale, z / scale, PCS, PCS, adj_freq, seed_);
//
//   static AutoCVarInt maxheight("terrain.maxheight", "max height", 10000);
//   gen::NoiseToHeights(floats, *hm, {0, maxheight.Get()});
//
//   res.first->second.access = std::chrono::steady_clock::now();
//   return res.first->second.height_map_pool_handle;
// }
