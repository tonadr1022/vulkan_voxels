#pragma once

#include <cstdint>

#include "EAssert.hpp"
#include "imgui.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include "ChunkMeshManager.hpp"
#include "voxels/Common.hpp"
#include "voxels/Mesher.hpp"
#include "voxels/Terrain.hpp"

template <typename NodeT>
struct NodeList {
  std::vector<uint32_t> free_list;
  std::vector<NodeT> nodes;
  uint32_t AllocNode() {
    if (!free_list.empty()) {
      auto idx = free_list.back();
      free_list.pop_back();
      nodes[idx] = {};
      return idx;
    }
    uint32_t h = nodes.size();
    nodes.emplace_back(NodeT{});
    return h;
  }

  NodeT* Get(uint32_t idx) {
    EASSERT(idx < nodes.size());
    return &nodes[idx];
  }

  // TODO: destructor?
  void Free(uint32_t idx) {
    free_list.emplace_back(idx);
    nodes[idx] = {};
  }
  void Clear() {
    free_list.clear();
    nodes.clear();
  }
};

struct MeshOctree {
  struct Node {
    static constexpr std::array<uint32_t, 8> Mask = {1 << 0, 1 << 1, 1 << 2, 1 << 3,
                                                     1 << 4, 1 << 5, 1 << 6, 1 << 7};
    // [[nodiscard]] bool IsLeaf(uint8_t child) const { return leaf_mask & Mask[child]; }

    void ClearMask(uint8_t idx) { leaf_mask &= ~Mask[idx]; }
    [[nodiscard]] bool IsSet(uint8_t child) const { return leaf_mask & Mask[child]; }
    void ClearMask() { leaf_mask = 0; }

    // void SetChild(uint8_t child, uint32_t idx) {
    //   leaf_mask &= ~Mask[child];
    //   data[child] = idx;
    // }
    void SetData(uint8_t child, uint32_t val) {
      data[child] = val;
      leaf_mask |= Mask[child];
    }
    // void ClearChild(uint8_t child) {
    //   data[child] = 0;
    //   leaf_mask |= Mask[child];
    // }
    // void ClearAll() {
    //   data.fill(0);
    //   leaf_mask = UINT8_MAX;
    // }
    std::array<uint32_t, 8> data;
    uint32_t mesh_handle;
    // TODO: refactor
    uint8_t leaf_mask;
  };

  // TODO: memory pool
  struct NodeQueueItem {
    uint32_t node_idx;
    ivec3 pos;
    uint32_t depth;
  };
  static constexpr int MaxDepth = 10;
  std::array<int, MaxDepth + 1> lod_bounds;
  std::vector<ChunkMeshUpload> chunk_mesh_uploads;
  struct NodeKey {
    uint32_t depth;
    uint32_t idx;
  };
  std::vector<NodeKey> chunk_mesh_node_keys;
  // TODO: refactor
  std::vector<uint32_t> mesh_handles;
  std::vector<uint32_t> meshes_to_free;
  std::vector<NodeQueueItem> node_queue;
  std::array<NodeList<Node>, MaxDepth + 1> nodes;

 private:
  std::vector<NodeQueueItem> to_mesh_queue_;

  void FreeNode(uint32_t depth, uint32_t idx) { nodes[depth].Free(idx); }
  Node* GetNode(uint32_t depth, uint32_t idx) { return GetNode(NodeKey{depth, idx}); }
  [[nodiscard]] const Node* GetNode(uint32_t depth, uint32_t idx) const {
    return GetNode(NodeKey{depth, idx});
  }
  Node* GetNode(const NodeKey& loc) { return &nodes[loc.depth].nodes[loc.idx]; }
  [[nodiscard]] const Node* GetNode(const NodeKey& loc) const {
    return &nodes[loc.depth].nodes[loc.idx];
  }

  gen::FBMNoise noise_;

 public:
  std::unordered_map<ivec3, HeightMapFloats> height_maps;

  void Init() {
    noise_.Init(1, 0.005, 4);
    auto root = nodes[0].AllocNode();
    EASSERT(root == 0);
    auto res = height_maps.emplace(ivec3{0}, HeightMapFloats{});
    FillNoise(res.first->second, vec2{0});

    int k = 1;
    for (auto& b : lod_bounds) {
      b = k * CS;
      k *= 2;
    }
    std::ranges::reverse(lod_bounds);

    Update(ivec3{0});
  }

  vec3 ChunkPosToAbsPos(ivec3 chunk_pos) { return vec3{chunk_pos} * vec3{PCS}; }

  ChunkMeshUpload PrepareChunkMeshUpload(const MeshAlgData& alg_data, const MesherOutputData& data,
                                         ivec3 pos, uint32_t depth) {
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

  [[nodiscard]] uint32_t GetOffset(uint32_t depth) const { return (1 << depth) * CS; }

  std::vector<NodeQueueItem> s;
  void FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx, uint32_t depth,
                    ivec3 pos) {
    s.emplace_back(NodeQueueItem{node_idx, pos, depth});
    while (!s.empty()) {
      auto [curr_idx, curr_pos, curr_depth] = s.back();
      s.pop_back();
      auto* node = GetNode(curr_depth, curr_idx);
      EASSERT(curr_depth != MaxDepth || node->leaf_mask == 0);
      int i = 0;
      for (int y = 0; y < 2; y++) {
        for (int z = 0; z < 2; z++) {
          for (int x = 0; x < 2; x++, i++) {
            if (node->IsSet(i)) {
              s.emplace_back(NodeQueueItem{
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
          // fmt::println("freeing mesh {} , {} {}, pos {} {} {}", node->mesh_handle, curr_depth,
          //              curr_idx, curr_pos.x, curr_pos.y, curr_pos.z);
          node->mesh_handle = 0;
        }
        FreeNode(curr_depth, curr_idx);
      }
    }
  }

  // when split node:
  // make four new nodes
  // if node has mesh, free it

  // when not split node
  // free children
  // make mesh for node if not mesh already or mesh dirty

  void Validate() const {
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

  void Reset() {
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
    for (auto& n : nodes) {
      n.Clear();
    }
    nodes[0].AllocNode();
  }

 private:
  bool HasChildren(NodeKey node_key) { return GetNode(node_key)->leaf_mask != 0; }

  void InterpolateHeightMap(ivec3 pos, uint32_t depth, HeightMapFloats& height_map) {
    auto to_height_map_idx = [](int x, int z) { return (z * PCS) + x; };
    auto off = GetOffset(depth);
    for (int quad = 0; quad < 4; quad++) {
      int x_offset = (quad % 2) * (PCS / 2);
      int z_offset = (quad / 2) * (PCS / 2);
      ivec2 quad_chunk_pos_xz = ivec2{pos.x + (off * (quad & 2)), pos.z + (off * (quad / 2))};
      ivec3 quad_hm_key = ivec3{quad_chunk_pos_xz.x, quad_chunk_pos_xz.y, depth + 1};
      auto it = height_maps.find(quad_hm_key);
      if (it == height_maps.end()) {
        it = height_maps.emplace(quad_hm_key, HeightMapFloats{}).first;
        HeightMapFloats& quadrant_hm = it->second;
        HeightMapFloats new_noise_map;
        FillNoise(new_noise_map, vec2{pos.x, pos.z} + vec2{quad_hm_key.x, quad_hm_key.y});
        for (int z = 0, i = 0; z < PCS; z++) {
          for (int x = 0; x < PCS; x++, i++) {
            int x1 = (x / 2) + x_offset;
            int x2 = x1 + 1;
            int z1 = (z / 2) + z_offset;
            int z2 = z1 + 1;

            // Clamp indices to avoid out-of-bounds
            x1 = std::min(x1, PCS - 1);
            x2 = std::min(x2, PCS - 1);
            z1 = std::min(z1, PCS - 1);
            z2 = std::min(z2, PCS - 1);
            float v1 = height_map[to_height_map_idx(x1, z1)];
            float v2 = height_map[to_height_map_idx(x2, z1)];
            float v3 = height_map[to_height_map_idx(x1, z2)];
            float v4 = height_map[to_height_map_idx(x2, z2)];
            float tx = (x % 2) / 2.f;
            float tz = (z % 2) / 2.f;
            float interpolated = (v1 * (1.f - tx) * (1.f - tz)) + (v2 * (1.f - tz) * tx) +
                                 (v3 * (1.f - tx) * tz) + (v4 * tx * tz);
            quadrant_hm[i] = interpolated + new_noise_map[i];
          }
        }
      }
    }
  }

 public:
  void Update(vec3 cam_pos) {
    EASSERT(node_queue.empty());
    node_queue.push_back(NodeQueueItem{0, vec3{0}, 0});
    while (!node_queue.empty()) {
      auto [node_idx, pos, depth] = node_queue.back();
      node_queue.pop_back();
      ivec3 chunk_center = pos + (1 << (MaxDepth - depth)) * HALFCS;
      // ivec3 chunk_center = pos + (1 << (MaxDepth - depth)) * HALFCS;
      // fmt::println("node pos {} {} {}, depth {}, cs {} {} {}", pos.x, pos.y, pos.z, depth,
      //              chunk_center.x, chunk_center.y, chunk_center.z);
      // mesh if far away enough not to split
      if (glm::distance(vec3(chunk_center), cam_pos) >= lod_bounds[depth] || depth == MaxDepth) {
        // get rid of children of this node since it's a mesh now
        if (depth < MaxDepth) {
          FreeChildren(meshes_to_free, node_idx, depth, pos);
        }
        to_mesh_queue_.emplace_back(NodeQueueItem{node_idx, pos, depth});
      } else if (depth < MaxDepth) {
        auto get_offset = [](int depth) { return (1 << (depth)) * CS; };
        int off = get_offset(MaxDepth - depth - 1);

        // TODO: refactor
        auto* node = nodes[depth].Get(node_idx);
        if (node->mesh_handle) {
          meshes_to_free.emplace_back(node->mesh_handle);
          node->mesh_handle = 0;
        }

        fmt::println("{} {} {}", pos.x, pos.z, depth);
        InterpolateHeightMap(pos, depth, height_maps.at({pos.x, pos.z, depth}));

        int i = 0;
        for (int y = 0; y < 2; y++) {
          for (int z = 0; z < 2; z++) {
            for (int x = 0; x < 2; x++) {
              NodeQueueItem e;
              e.depth = depth + 1;
              e.pos = pos + ivec3{x, y, z} * off;
              auto* node = nodes[depth].Get(node_idx);
              uint32_t child_node_idx = node->IsSet(i) ? node->data[i] : nodes[e.depth].AllocNode();
              node->SetData(i, child_node_idx);
              e.node_idx = child_node_idx;
              node_queue.emplace_back(e);
              i++;
            }
          }
        }
      }
    }
    if (meshes_to_free.size()) {
      ChunkMeshManager::Get().FreeMeshes(meshes_to_free);
      meshes_to_free.clear();
    }

    for (auto [node_idx, pos, depth] : to_mesh_queue_) {
      auto* node = GetNode(depth, node_idx);
      if (!node->mesh_handle) {
        Chunk chunk{pos};
        chunk.grid = {};

        // gen::FillSphere<PCS>(chunk.grid, depth * 30);
        gen::FillVisibleCube(chunk.grid, 8, depth * 30);
        MeshAlgData alg_data{};
        MesherOutputData data{};
        alg_data.mask = &chunk.grid.mask;
        GenerateMesh(chunk.grid.grid.grid, alg_data, data);
        if (data.vertex_cnt) {
          ChunkMeshUpload u = PrepareChunkMeshUpload(alg_data, data, pos, depth);
          chunk_mesh_uploads.emplace_back(u);
          chunk_mesh_node_keys.emplace_back(NodeKey{.depth = depth, .idx = node_idx});
          // fmt::println("meshing {} {} {} depth {}", pos.x, pos.y, pos.z, depth, node_idx);
        }
      }
    }
    to_mesh_queue_.clear();

    EASSERT(chunk_mesh_node_keys.size() == chunk_mesh_uploads.size());
    if (chunk_mesh_uploads.size()) {
      mesh_handles.resize(chunk_mesh_uploads.size());
      ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads, mesh_handles);
      for (size_t i = 0; i < chunk_mesh_node_keys.size(); i++) {
        GetNode(chunk_mesh_node_keys[i])->mesh_handle = mesh_handles[i];
        // auto& pos = chunk_mesh_uploads[i].pos;
        // fmt::println("meshing {} {} {}, handle {}, mult {}", pos.x, pos.y, pos.z,
        // mesh_handles[i],
        //              chunk_mesh_uploads[i].mult);
      }
    }
    mesh_handles.clear();
    chunk_mesh_node_keys.clear();
    chunk_mesh_uploads.clear();

    // Validate();
  }

  void OnImGui() const {
    if (ImGui::Begin("Voxel Octree")) {
      for (int i = 0; i <= MaxDepth; i++) {
        ImGui::Text("%d: %zu", i, nodes[i].nodes.size() - nodes[i].free_list.size());
      }
    }
    ImGui::End();
  }

 private:
  float freq_{0.005};
  int seed_{1};
  void FillNoise(HeightMapFloats& floats, ivec2 pos) const {
    noise_.white_noise->GenUniformGrid2D(floats.data(), pos.x, pos.y, PCS, PCS, freq_, seed_);
  }
};

using VoxelData = uint8_t;

struct OctreeNode {
  std::array<uint32_t, 8> data;
  uint8_t leaf_mask;
  [[nodiscard]] bool IsLeaf(uint8_t child) const { return leaf_mask & Mask[child]; }
  void SetChild(uint8_t child, uint32_t idx) {
    leaf_mask &= ~Mask[child];
    data[child] = idx;
  }
  void SetData(uint8_t child, uint32_t val) {
    data[child] = val;
    leaf_mask |= Mask[child];
  }
  void ClearChild(uint8_t child) {
    data[child] = 0;
    leaf_mask |= Mask[child];
  }
  void ClearAll() {
    data.fill(0);
    leaf_mask = UINT8_MAX;
  }
  static constexpr std::array<uint32_t, 8> Mask = {1 << 0, 1 << 1, 1 << 2, 1 << 3,
                                                   1 << 4, 1 << 5, 1 << 6, 1 << 7};
};

// LOD 0 is the coarsest
struct Octree {
  using Node = OctreeNode;

  void Init();

  void SetVoxel(uvec3 pos, uint32_t depth, VoxelData val);
  uint32_t GetVoxel(uvec3 pos);

  constexpr static uint64_t MaxDepth = 5;

 private:
  void FreeChildNodes(uint32_t node_idx, uint32_t depth);
  std::array<NodeList<Node>, MaxDepth> nodes_;
};
