#include "Octree.hpp"

#include <cstdint>
#include <cwchar>

#include "application/CVar.hpp"

namespace {
AutoCVarFloat lod_thresh("terrain.lod_thresh", "lod threshold of terrain", 1.0);

uint8_t GetChildIdx(uvec3 pos, uint8_t curr_depth) {
  return (pos.y >> (Octree::MaxDepth - curr_depth - 1) & 1) << 2 |
         (pos.x >> (Octree::MaxDepth - curr_depth - 1) & 1) << 1 |
         (pos.z >> (Octree::MaxDepth - curr_depth - 1) & 1);
}

template <typename T>
void DumpBits(T d, size_t size = sizeof(T)) {
  static_assert(std::is_integral_v<T>, "T must be an integral type.");
  for (int i = static_cast<int>(size) - 1; i >= 0; i--) {
    fmt::print("{}", ((d & (static_cast<T>(1) << i)) ? 1 : 0));
  }
  fmt::println("");
}

}  // namespace

void Octree::Init() {
  auto* d = new uint32_t[10];
  for (int i = 0; i < 10; i++) {
    d[i] = 10;
    fmt::println("i {}", d[i]);
  }
  exit(1);
  // nodes_[0].AllocNode();
  // uvec3 iter{};
  // auto cnt = 2u;
  // uint32_t val = 1;
  // int i = 0;
  // for (iter.y = 0; iter.y < cnt; iter.y++) {
  //   for (iter.x = 0; iter.x < cnt; iter.x++) {
  //     for (iter.z = 0; iter.z < cnt; iter.z++, i++) {
  //       SetVoxel(iter, 2, val + i);
  //       EASSERT(GetVoxel(iter) == val + i);
  //     }
  //   }
  // }
  // val = 0;
  // SetVoxel({0, 0, 0}, 1, val);
  // for (iter.y = 0; iter.y < cnt; iter.y++) {
  //   for (iter.x = 0; iter.x < cnt; iter.x++) {
  //     for (iter.z = 0; iter.z < cnt; iter.z++) {
  //       EASSERT(GetVoxel(iter) == val);
  //     }
  //   }
  // }
}

void Octree::SetVoxel(uvec3 pos, uint32_t depth, VoxelData val) {
  uint32_t curr_depth = 0;
  // get root node
  Node* curr_node = nodes_[0].Get(0);
  while (curr_depth < depth) {
    uint8_t child_idx = GetChildIdx(pos, curr_depth);
    // make node if needed
    if (curr_node->IsLeaf(child_idx)) {
      uint32_t idx = nodes_[curr_depth + 1].AllocNode();
      curr_node->SetChild(child_idx, idx);
    }

    // traverse to child
    curr_node = nodes_[curr_depth + 1].Get(curr_node->data[child_idx]);
    curr_depth++;
  }

  uint32_t final_child_idx = GetChildIdx(pos, curr_depth);
  if (!curr_node->IsLeaf(final_child_idx)) {
    FreeChildNodes(curr_node->data[final_child_idx], curr_depth + 1);
  }
  curr_node->SetData(final_child_idx, val);
}

uint32_t Octree::GetVoxel(uvec3 pos) {
  uint32_t curr_depth = 0;
  Node* curr_node = nodes_[0].Get(0);
  while (curr_depth < MaxDepth) {
    uint8_t idx = GetChildIdx(pos, curr_depth);
    if (curr_node->IsLeaf(idx)) {
      return curr_node->data[idx];
    }
    curr_node = nodes_[curr_depth].Get(curr_node->data[idx]);
    curr_depth++;
  }
  EASSERT(0 && "unreachable");
}

void Octree::FreeChildNodes(uint32_t node_idx, uint32_t depth) {
  if (depth >= MaxDepth) return;
  Node* node = nodes_[depth].Get(node_idx);
  for (uint8_t i = 0; i < 8; i++) {
    if (!node->IsLeaf(i)) {
      uint32_t child_idx = node->data[i];
      FreeChildNodes(child_idx, depth + 1);
      nodes_[depth + 1].Free(child_idx);
    }
  }
  nodes_[depth].Free(node_idx);
}
void MeshOctree::Init() {
  noise_.Init(1, 0.005, 4);
  auto root = nodes_[0].AllocNode();
  EASSERT(root == 0);

  int k = 1;
  for (auto& b : lod_bounds_) {
    b = k * CS;
    k *= 2;
  }
  std::ranges::reverse(lod_bounds_);

  Update(ivec3{0});
}

ChunkMeshUpload MeshOctree::PrepareChunkMeshUpload(const MeshAlgData& alg_data,
                                                   const MesherOutputData& data, ivec3 pos,
                                                   uint32_t depth) {
  uint32_t staging_copy_idx =
      ChunkMeshManager::Get().CopyChunkToStaging(data.vertices.data(), data.vertex_cnt);
  ChunkMeshUpload u{};
  u.staging_copy_idx = staging_copy_idx;
  u.pos = pos;
  int m = MaxDepth - depth + 1;
  u.mult = 1 << (m - 1);
  for (int i = 0; i < 6; i++) {
    u.counts[i] = alg_data.face_vertex_lengths[i];
  }
  return u;
}

void MeshOctree::FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx,
                              uint32_t depth, ivec3 pos) {
  child_free_stack_.emplace_back(NodeQueueItem{node_idx, pos, depth});
  while (!child_free_stack_.empty()) {
    auto [curr_idx, curr_pos, curr_depth] = child_free_stack_.back();
    child_free_stack_.pop_back();
    auto* node = GetNode(curr_depth, curr_idx);
    EASSERT(curr_depth != MaxDepth || node->leaf_mask == 0);
    int i = 0;
    for (int y = 0; y < 2; y++) {
      for (int z = 0; z < 2; z++) {
        for (int x = 0; x < 2; x++, i++) {
          if (node->IsSet(i)) {
            child_free_stack_.emplace_back(NodeQueueItem{
                node->data[i],
                pos + (ivec3{x, y, z} * static_cast<int>(GetOffset(MaxDepth - depth - 1))),
                curr_depth + 1});
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
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0});
  while (node_q.size()) {
    auto [node_idx, pos, depth] = node_q.back();
    node_q.pop_back();
    const auto* node = GetNode(depth, node_idx);
    EASSERT(node);
    if (node->mesh_handle) {
      to_free.emplace_back(node->mesh_handle);
      if (depth < MaxDepth) {
        if (node->leaf_mask != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->leaf_mask == 0);
        }
      }
    } else {
      if (depth < MaxDepth) {
        int off = GetOffset(MaxDepth - depth - 1);
        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
              if (node->IsSet(i)) {
                NodeQueueItem e;
                e.depth = depth + 1;
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
  EASSERT(node_queue_.empty());
  node_queue_.push_back(NodeQueueItem{0, vec3{0}, 0});
  while (!node_queue_.empty()) {
    auto [node_idx, pos, depth] = node_queue_.back();
    node_queue_.pop_back();
    ivec3 chunk_center = pos + ((1 << (MaxDepth - depth)) * HALFCS);
    // ivec3 chunk_center = pos + (1 << (MaxDepth - depth)) * HALFCS;
    // fmt::println("node pos {} {} {}, depth {}, cs {} {} {}", pos.x, pos.y, pos.z, depth,
    //              chunk_center.x, chunk_center.y, chunk_center.z);
    // mesh if far away enough not to split
    if (glm::distance(vec3(chunk_center), cam_pos) >=
            (lod_bounds_[depth] * lod_thresh.GetFloat()) ||
        depth == MaxDepth) {
      // get rid of children of this node since it's a mesh now
      if (depth < MaxDepth) {
        FreeChildren(meshes_to_free_, node_idx, depth, pos);
      }
      to_mesh_queue_.emplace_back(NodeQueueItem{node_idx, pos, depth});
    } else if (depth < MaxDepth) {
      int off = GetOffset(MaxDepth - depth - 1);

      // TODO: refactor
      auto* node = nodes_[depth].Get(node_idx);
      if (node->mesh_handle) {
        meshes_to_free_.emplace_back(node->mesh_handle);
        node->mesh_handle = 0;
      }

      // fmt::println("{} {} {}", pos.x, pos.z, depth);
      // InterpolateHeightMap(pos, depth, height_maps.at({pos.x, pos.z, depth}));

      int i = 0;
      for (int y = 0; y < 2; y++) {
        for (int z = 0; z < 2; z++) {
          for (int x = 0; x < 2; x++) {
            NodeQueueItem e;
            e.depth = depth + 1;
            e.pos = pos + ivec3{x, y, z} * off;
            auto* node = nodes_[depth].Get(node_idx);
            uint32_t child_node_idx = node->IsSet(i) ? node->data[i] : nodes_[e.depth].AllocNode();
            node->SetData(i, child_node_idx);
            e.node_idx = child_node_idx;
            node_queue_.emplace_back(e);
            i++;
          }
        }
      }
    }
  }

  if (meshes_to_free_.size()) {
    // ChunkMeshManager::Get().chunk_quad_buffer_.draw_cmd_allocator.PrintHandles();
    ChunkMeshManager::Get().FreeMeshes(meshes_to_free_);
    meshes_to_free_.clear();
  }

  for (auto [node_idx, pos, depth] : to_mesh_queue_) {
    auto* node = GetNode(depth, node_idx);
    if (!node->mesh_handle) {
      Chunk chunk{pos};
      chunk.grid = {};

      // TODO: refactor
      auto fill_chunk = [this, &pos, &chunk, depth]() {
        HeightMapData& hm = GetHeightMap(pos.x, pos.z, depth);
        int chunk_len = ChunkLenFromDepth(depth);
        if (pos.y + chunk_len <= hm.range.x || pos.y >= hm.range.y) {
          return;
        }
        int c = depth * 30;
        gen::FillChunkNoCheck(chunk.grid, chunk.pos, hm, [c](int, int, int) { return c; });
        // gen::FillChunk(chunk.grid, pos * CS, hm, [](int, int, int) { return 128; });
      };
      fill_chunk();

      // gen::FillSphere<PCS>(chunk.grid, depth * 30);
      // gen::FillVisibleCube(chunk.grid, 8, depth * 30);
      MeshAlgData alg_data{};
      MesherOutputData data{};
      alg_data.mask = &chunk.grid.mask;
      GenerateMesh(chunk.grid.grid.grid, alg_data, data);
      if (data.vertex_cnt) {
        ChunkMeshUpload u = PrepareChunkMeshUpload(alg_data, data, pos, depth);
        chunk_mesh_uploads_.emplace_back(u);
        chunk_mesh_node_keys_.emplace_back(NodeKey{.depth = depth, .idx = node_idx});
        // fmt::println("meshing {} {} {} depth {}", pos.x, pos.y, pos.z, depth);
      }
    }
  }
  to_mesh_queue_.clear();

  EASSERT(chunk_mesh_node_keys_.size() == chunk_mesh_uploads_.size());
  if (chunk_mesh_uploads_.size()) {
    mesh_handles_.resize(chunk_mesh_uploads_.size());
    ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads_, mesh_handles_);
    for (size_t i = 0; i < chunk_mesh_node_keys_.size(); i++) {
      GetNode(chunk_mesh_node_keys_[i])->mesh_handle = mesh_handles_[i];
    }
  }
  mesh_handles_.clear();
  chunk_mesh_node_keys_.clear();
  chunk_mesh_uploads_.clear();
  Validate();
}

void MeshOctree::OnImGui() const {
  if (ImGui::Begin("Voxel Octree")) {
    for (int i = 0; i <= MaxDepth; i++) {
      ImGui::Text("%d: %zu", i, nodes_[i].nodes.size() - nodes_[i].free_list.size());
    }
    ImGui::Text("height maps: %zu", height_maps_.size());
  }
  ImGui::End();
}

void MeshOctree::Validate() const {
  std::vector<NodeQueueItem> node_q;
  node_q.emplace_back(NodeQueueItem{0, vec3{0}, 0});
  while (node_q.size()) {
    auto [node_idx, pos, depth] = node_q.back();
    node_q.pop_back();
    const auto* node = GetNode(depth, node_idx);
    EASSERT(node);
    if (node->mesh_handle) {
      if (depth < MaxDepth) {
        if (node->leaf_mask != 0) {
          fmt::println("failed depth {}, node_idx {}", depth, node_idx);
          EASSERT(node->leaf_mask == 0);
        }
      }
    } else {
      if (depth < MaxDepth) {
        int off = GetOffset(MaxDepth - depth - 1);
        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
              if (node->IsSet(i)) {
                NodeQueueItem e;
                e.depth = depth + 1;
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

HeightMapData& MeshOctree::GetHeightMap(int x, int z, int lod) {
  auto it = height_maps_.find({x, z, lod});
  if (it != height_maps_.end()) {
    return it->second;
  }
  auto res = height_maps_.emplace(ivec3{x, z, lod}, HeightMapData{});
  HeightMapData& hm = res.first->second;

  uint32_t scale = (1 << (MaxDepth - lod));

  static AutoCVarFloat freq("terrain.freq", "freq of terrain", 0.0005);
  float adj_freq = freq.GetFloat() * static_cast<float>(scale);
  HeightMapFloats floats;
  noise_.fbm->GenUniformGrid2D(floats.data(), x / scale, z / scale, PCS, PCS, adj_freq, seed_);

  static AutoCVarInt maxheight("terrain.maxheight", "max height", 62);
  gen::NoiseToHeights(floats, hm, {0, maxheight.Get() / scale});

  return res.first->second;
}
