#include "Octree.hpp"

#include <cstdint>
#include <thread>

#include "ChunkMeshManager.hpp"
#include "EAssert.hpp"
#include "application/CVar.hpp"
#include "application/ThreadPool.hpp"
#include "application/Timer.hpp"
#include "fmt/base.h"
#include "imgui.h"

// TODO: when a mesh is made, the node might be stale, so a mesh exists without a node to free
// it
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
  chunk_pool_.Init(1000);
  height_map_pool_.Init(50000);
  // TODO: fine tune
  mesh_alg_buf_.Init(1000);
  mesher_output_data_buf_.Init(1000);

  prev_cam_chunk_pos_ = ivec3{INT_MAX};

  noise_.Init(1, 0.005, 15);
  auto root = AllocNode(0);
  EASSERT(root == 0);

  UpdateLodBounds();
  Update(ivec3{0});
}

void MeshOctree::FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx,
                              uint32_t depth, ivec3 pos) {
  child_free_stack_.emplace_back(
      NodeQueueItem{node_idx, pos, depth, nodes_.GetGeneration(depth, node_idx)});
  while (!child_free_stack_.empty()) {
    auto [curr_idx, curr_pos, curr_depth, generation] = child_free_stack_.back();
    child_free_stack_.pop_back();
    auto* node = nodes_.GetNode(curr_depth, curr_idx);
    EASSERT(curr_depth != max_depth_ || (node->flags & 0x000000FF) == 0);
    int i = 0;
    for (int y = 0; y < 2; y++) {
      for (int z = 0; z < 2; z++) {
        for (int x = 0; x < 2; x++, i++) {
          if (node->IsSet(i)) {
            child_free_stack_.emplace_back(NodeQueueItem{
                node->data[i],
                pos + (ivec3{x, y, z} * static_cast<int>(GetOffset(max_depth_ - depth - 1))),
                curr_depth + 1, nodes_.GetGeneration(curr_depth + 1, node->data[i])});
          }
        }
      }
    }
    node->ClearMask();
    if (curr_depth != depth) {
      if (node->mesh_handle) {
        meshes_to_free.emplace_back(node->mesh_handle);
        // fmt::println("freeing mesh {} ,depth {}, pos {} {} {}", node->mesh_handle, curr_depth,
        //              curr_pos.x, curr_pos.y, curr_pos.z);
        node->mesh_handle = 0;
      }
      FreeNode(curr_depth, curr_idx);
    }
  }
}

void MeshOctree::Reset() {
  std::vector<NodeQueueItem> node_q;
  std::vector<uint32_t> to_free;
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0, nodes_.GetGeneration(0, 0)});
  while (node_q.size()) {
    auto [node_idx, pos, depth, generation] = node_q.back();
    node_q.pop_back();
    const auto* node = nodes_.GetNode(depth, node_idx);
    EASSERT(node);
    if (node->mesh_handle) {
      to_free.emplace_back(node->mesh_handle);
      if (depth < max_depth_) {
        if (node->flags != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->flags == 0);
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
  for (auto& n : nodes_.nodes) {
    n.Clear();
  }
  AllocNode(0);
  height_maps_.clear();
}

void MeshOctree::Update(vec3 cam_pos) {
  ZoneScoped;
  node_queue_.clear();
  EASSERT(node_queue_.empty());
  curr_cam_pos_ = cam_pos;
  auto new_cam_chunk_pos = ivec3(cam_pos) / CS;
  bool changed_chunk_pos = new_cam_chunk_pos != prev_cam_chunk_pos_;
  prev_cam_chunk_pos_ = new_cam_chunk_pos;
  bool update_ready = changed_chunk_pos;
  auto now = std::chrono::steady_clock::now();
  update_ready =
      (update_ready || std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_octree_update_time_) > std::chrono::milliseconds(7));
  if (!to_mesh_queue_.size() && update_ready) {
    last_octree_update_time_ = now;
    node_queue_.push_back(NodeQueueItem{0, vec3{0}, 0, nodes_.nodes[0].nodes[0].generation});
    while (!node_queue_.empty()) {
      auto [node_idx, pos, lod, generation] = node_queue_.back();
      node_queue_.pop_back();
      {
        auto* node = nodes_.GetNode(lod, node_idx);
        EASSERT(node);
        if (!(node->flags & Node::DataFlagsTerrainGenDirty) && node->num_solid == 0) {
          continue;
        }
      }
      if (ShouldMeshChunk(pos, lod)) {
        if (lod < max_depth_) {
          FreeChildren(meshes_to_free_, node_idx, lod, pos);
        }
        auto* node = nodes_.GetNode(lod, node_idx);
        if (node->GetNeedsGenOrMeshing()) {
          node->SetNeedsGenOrMeshing(false);
          // auto& hm = GetHeightMap(pos.x, pos.z, lod);
          // if (ChunkInHeightMapRange(hm.range, lod, pos)) {
          to_mesh_queue_.emplace_back(node_idx, pos, lod, nodes_.GetGeneration(lod, node_idx));
          // } else {
          //   node->num_solid = 0;
          //   node->SetFlags(Node::DataFlagsTerrainGenDirty, false);
          // }
        }
      } else if (lod < max_depth_) {
        int off = GetOffset(max_depth_ - lod - 1);
        // TODO: refactor
        auto* node = nodes_.nodes[lod].Get(node_idx);
        if (node->mesh_handle) {
          meshes_to_free_.emplace_back(node->mesh_handle);
          node->SetNeedsGenOrMeshing(true);
          nodes_.GetGeneration(lod, node_idx)++;
          node->mesh_handle = 0;
        }
        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++, i++) {
              NodeQueueItem e;
              e.lod = lod + 1;
              e.pos = pos + ivec3{x, y, z} * off;
              auto* node = nodes_.nodes[lod].Get(node_idx);
              uint32_t child_node_idx;
              if (node->IsSet(i)) {
                child_node_idx = node->data[i];
              } else {
                child_node_idx = AllocNode(e.lod);
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
  DispatchTasks();

  {
    ZoneScopedN("fill chunk and mesh");
    {
      ZoneScopedN("finished mesh gpu upload tasks");
      MeshGenTask task;
      while (terrain_tasks_.done_tasks.try_dequeue(task)) {
        terrain_tasks_.DecInFlight();
        auto& chunk = *task.chunk;

        bool mesh_curr_test = MeshCurrTest(chunk.pos, task.node_key.lod);
        if (task.vert_count) {
          auto pos = chunk.pos;
          ChunkMeshUpload u;
          u.stale = !mesh_curr_test;
          u.staging_copy_idx = task.staging_copy_idx;
          if (!u.stale) {
            memcpy(u.vert_counts, task.vert_counts, sizeof(uint32_t) * 6);
            u.pos = pos;
            u.mult = 1 << (max_depth_ - task.node_key.lod);
          }
          chunk_mesh_uploads_.emplace_back(u);
          chunk_mesh_node_keys_.emplace_back(task.node_key);
        }
        // fmt::println("meshing {} {} {} depth {}", pos.x, pos.y, pos.z, depth);
      }
    }
  }

  EASSERT(chunk_mesh_node_keys_.size() == chunk_mesh_uploads_.size());
  if (chunk_mesh_uploads_.size()) {
    mesh_handle_upload_buffer_.reserve(chunk_mesh_uploads_.size());
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_, mesh_handle_upload_buffer_);
    size_t j = 0;
    for (size_t i = 0; i < chunk_mesh_node_keys_.size(); i++) {
      if (!chunk_mesh_uploads_[i].stale) {
        auto* node = nodes_.GetNode(chunk_mesh_node_keys_[j]);
        if (!node) continue;
        node->mesh_handle = mesh_handle_upload_buffer_[j];
        j++;
      }
    }
  }
  mesh_handle_upload_buffer_.clear();
  chunk_mesh_node_keys_.clear();
  chunk_mesh_uploads_.clear();
  {
    ZoneScopedN("free meshes");
    if (meshes_to_free_.size()) {
      ChunkMeshManager::Get().FreeMeshes(meshes_to_free_);
      meshes_to_free_.clear();
    }
  }

  {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_height_map_cleanup_time_) >
        std::chrono::milliseconds(1000)) {
      ClearOldHeightMaps(now);
      last_height_map_cleanup_time_ = now;
    }
  }
}

void MeshOctree::OnImGui() {
  size_t tot_nodes_cnt = 0;
  if (ImGui::Begin("Voxel Octree")) {
    for (uint32_t i = 0; i <= max_depth_; i++) {
      size_t s = nodes_.nodes[i].Size();
      tot_nodes_cnt += s;
      ImGui::Text("%d: %zu", i, s);
    }
    ImGui::Text("Total Nodes: %zu", tot_nodes_cnt);
    ImGui::Text("height maps: %zu", height_maps_.size());
    ImGui::Text("terrain: to complete %zu, done %zu, in flight %zu",
                terrain_tasks_.to_complete.size(), terrain_tasks_.done_tasks.size_approx(),
                terrain_tasks_.InFlight());
    int d = max_depth_;
    if (ImGui::DragInt("max depth", &d, 0, AbsoluteMaxDepth)) {
      max_depth_ = d;
      UpdateLodBounds();
    }
    ImGui::Text("mesh queue size: %zu", to_mesh_queue_.size());
  }
  ImGui::End();
}

void MeshOctree::Validate() {
  ZoneScoped;
  std::vector<NodeQueueItem> node_q;
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0, 1});
  while (node_q.size()) {
    auto [node_idx, pos, depth, generation] = node_q.back();
    node_q.pop_back();
    const auto* node = nodes_.GetNode(depth, node_idx);
    EASSERT(node);
    if (node->mesh_handle) {
      if (depth < max_depth_) {
        if ((node->flags & 0x000000FF) != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->flags == 0);
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
  std::lock_guard<std::mutex> lock(height_map_mtx_);
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
  auto& chunk = *task.chunk;
  chunk.grid.Clear();
  auto& hm = GetHeightMap(chunk.pos.x, chunk.pos.z, task.node_key.lod);
  if (!ChunkInHeightMapRange(hm.range, task.node_key.lod, chunk.pos)) {
    return;
  }
  // int color = 128;
  int color = task.node_key.lod * 30;
  int scale = (1 << (max_depth_ - task.node_key.lod));
  for (int y = 0; y < PaddedChunkGrid3D::Dims.y; y++) {
    int adj_y = (y * scale) + chunk.pos.y;
    int i = 0;
    for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
      for (int x = 0; x < PaddedChunkGrid3D::Dims.x; x++, i++) {
        if (adj_y < ((hm.heights[i]))) {
          chunk.grid.Set(x, y, z, color);
        }
      }
    }
  }
  // gen::FillChunkNoCheck(chunk->grid, chunk->pos, hm, [c](int, int, int) { return c; });
}

void MeshOctree::ProcessMeshGenTask(MeshGenTask& task) {
  ZoneScoped;

  auto& chunk = *task.chunk;

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
#define LOCK_HM std::lock_guard<std::mutex> lock(height_map_mtx_)
  // #define LOCK_HM
  {
    LOCK_HM;
    auto it = height_maps_.find({x, z, lod});
    if (it != height_maps_.end()) {
      it->second.access = std::chrono::steady_clock::now();
      return *height_map_pool_.Get(it->second.height_map_pool_handle);
    }
  }
  uint32_t handle;
  {
    LOCK_HM;
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
    LOCK_HM;
    height_maps_.emplace(ivec3{x, z, lod}, data);
  }
  return *hm;
}

bool MeshOctree::ShouldMeshChunk(ivec3 pos, uint32_t lod) {
  return glm::distance(vec3(ChunkCenter(pos, lod)), curr_cam_pos_) >=
         (lod_bounds_[lod] * lod_thresh.GetFloat());
}

bool MeshOctree::MeshCurrTest(ivec3 pos, uint32_t lod) {
  bool mesh_curr = ShouldMeshChunk(pos, lod);
  bool mesh_next = lod == max_depth_ || ShouldMeshChunk(pos, lod + 1);
  return mesh_curr && (mesh_curr || !mesh_next);
}

uint32_t MeshOctree::AllocNode(uint32_t lod) {
  uint32_t ret = nodes_.nodes[lod].AllocNode();
  auto* node = nodes_.GetNode(lod, ret);
  node->Reset();
  nodes_.GetGeneration(lod, ret)++;
  nodes_.GetNode(lod, ret)->Reset();
  return ret;
}
// instead of queuing tasks:
// pool of chunks to be drawn. each frame: each chunk queries whether it should be drawn
// octree thread: updates whether each chunk should be drawn and adds old meshes to the deletion
// queue for octree thread to know whether chunk should be drawn, needs to generate terrain and
void MeshOctree::DispatchTasks() {
  ZoneScoped;
  size_t stale_cnt = 0;
  while (terrain_tasks_.CanEnqueueTask() && !to_mesh_queue_.empty()) {
    auto stale = [&stale_cnt]() { stale_cnt++; };
    NodeQueueItem2 item = to_mesh_queue_.back();
    to_mesh_queue_.pop_back();
    auto pos = item.pos;
    auto lod = item.lod;
    auto node_generation = item.node_generation;
    auto node_idx = item.node_idx;
    if (!MeshCurrTest(pos, lod)) {
      stale();
      continue;
    }
    if (nodes_.GetGeneration(lod, node_idx) != node_generation) {
      stale();
      continue;
    }
    auto* chunk = chunk_pool_.Allocate();
    EASSERT(chunk);
    chunk->pos = pos;
    TerrainGenTask terrain_task{NodeKey{.lod = lod, .idx = node_idx}, chunk};
    terrain_tasks_.IncInFlight();
    thread_pool.detach_task([terrain_task, this, node_generation]() {
      auto t = terrain_task;
      MeshGenTask mesh_task{};
      mesh_task.chunk = t.chunk;
      mesh_task.node_key = t.node_key;
      auto no_mesh_done = [this]() {
        terrain_tasks_.DecInFlight();
        // terrain_tasks_.done_tasks.enqueue(mesh_task);
      };
      if (nodes_.GetGeneration(t.node_key.lod, t.node_key.idx) != node_generation) {
        no_mesh_done();
        return;
      }
      auto* chunk = t.chunk;
      // check whether still need to do this task
      bool mesh_curr_test = MeshCurrTest(chunk->pos, t.node_key.lod);
      if (!mesh_curr_test) {
        no_mesh_done();
        return;
      }
      ProcessTerrainTask(t);
      auto* node = nodes_.GetNode(t.node_key);
      if (!node || nodes_.GetGeneration(t.node_key.lod, t.node_key.idx) != node_generation) {
        no_mesh_done();
        return;
      }
      size_t num_solid = chunk->grid.mask.SolidCount();
      node->num_solid = num_solid;
      node->SetFlags(Node::DataFlagsTerrainGenDirty, false);
      if (chunk && num_solid) {
        ProcessMeshGenTask(mesh_task);
        terrain_tasks_.done_tasks.enqueue(mesh_task);
      } else if (chunk) {
        no_mesh_done();
      }
    });
  }
  if (stale_cnt) {
    fmt::println("{}", stale_cnt);
  }
}
