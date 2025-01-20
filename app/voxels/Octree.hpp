#pragma once

#include <cstdint>

#include "EAssert.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <queue>

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
};

struct MeshOctree {
  struct Node {
    static constexpr std::array<uint32_t, 8> Mask = {1 << 0, 1 << 1, 1 << 2, 1 << 3,
                                                     1 << 4, 1 << 5, 1 << 6, 1 << 7};
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
    std::array<uint32_t, 8> data;
    // TODO: refactor
    uint8_t leaf_mask;
  };

  // TODO: memory pool
  struct NodeStackItem {
    uint32_t node_idx;
    ivec3 pos;
    int depth;
  };
  static constexpr int MaxDepth = 3;
  std::queue<NodeStackItem> node_queue;
  std::unordered_map<ivec3, HeightMapFloats> height_maps;
  std::array<int, MaxDepth + 1> lod_bounds;
  std::vector<ChunkMeshUpload> chunk_mesh_uploads;
  // TODO: refactor
  std::vector<uint32_t> mesh_handles;
  std::array<NodeList<Node>, MaxDepth + 1> nodes;
  gen::FBMNoise noise;

  void Init() {
    // for (int depth = 0; depth < MaxDepth; depth++) {
    //   int off = std::pow(CS, MaxDepth - depth) / 2;
    //   fmt::println("off {}", off);
    // }
    noise.Init(1, 0.005, 4);
    auto root = nodes[0].AllocNode();
    EASSERT(root == 0);
    // auto res = height_maps.emplace(ivec3{0}, HeightMapFloats{});
    // FillNoise(res.first->second, vec2{0});
    Update(ivec3{0});
  }

  vec3 ChunkPosToAbsPos(ivec3 chunk_pos) { return vec3{chunk_pos} * vec3{PCS}; }

  void Update(vec3) {
    auto initial_depth = 0;
    // EASSERT(nodes.size() && nodes[initial_depth].nodes.size() == 1);
    node_queue.push(NodeStackItem{0, vec3{0}, initial_depth});

    // lod_bounds.fill(0);
    // int k = 1;
    // for (auto& b : lod_bounds) {
    //   b = k * CS;
    //   k *= 2;
    //   fmt::println("b {}", b);
    // }
    // std::ranges::reverse(lod_bounds);
    while (!node_queue.empty()) {
      NodeStackItem e = node_queue.front();
      node_queue.pop();
      // auto node_idx = e.node_idx;
      auto pos = e.pos;
      auto depth = e.depth;
      // auto [node_idx, pos, depth] = node_queue.front();
      // fmt::println("node pos {} {} {}, depth {}", pos.x, pos.y, pos.z, depth);
      // auto it = height_maps.find(ivec3{pos.x, pos.z, depth});
      // EASSERT(it != height_maps.end());
      // auto& height_map = it->second;
      // bool recurse = (pos.x + pos.y + pos.z + depth) % 2 != 1;
      // if (true) {
      // if (glm::distance(vec3(pos), cam_pos) > lod_bounds[depth]) {
      // static int i = 0;
      // if (depth != 1 || i++ != 3) {
      //   continue;
      // }
      // make terrain and mesh using its
      // HeightMapData heights;
      // gen::NoiseToHeights(height_map, heights, {0, (1 * CS * 0.5) - 1});
      // gen::FillSphere<PCS>(chunk.grid, []() { return 128; });
      // gen::FillChunk(chunk.grid, chunk.pos * CS, heights, [](int, int, int) {
      //   // return (rand() % 255) + 1;
      //   return 128;
      // });
      Chunk chunk{};
      chunk.pos = pos;
      gen::FillVisibleCube(chunk.grid, 0, 1);
      MeshAlgData alg_data;
      MesherOutputData data;
      alg_data.mask = &chunk.grid.mask;
      GenerateMesh(chunk.grid.grid.grid, alg_data, data);
      if (data.vertex_cnt != 6) {
        fmt::println("{} cnt", data.vertex_cnt);
        EASSERT(data.vertex_cnt == 6);
      }
      fmt::println("meshed {} {} {} {}, cnt {}", pos.x, pos.y, pos.z, MaxDepth - depth,
                   data.vertex_cnt);
      if (data.vertex_cnt) {
        uint32_t staging_copy_idx =
            ChunkMeshManager::Get().CopyChunkToStaging(data.vertices.data(), data.vertex_cnt);
        ChunkMeshUpload u{};
        u.staging_copy_idx = staging_copy_idx;
        u.pos = pos;
        u.mult = MaxDepth - depth;
        // 1 2 3 4 5
        // 1 2 4 8 16
        // 2^(m-1)
        for (int i = 0; i < 6; i++) {
          u.counts[i] = alg_data.face_vertex_lengths[i];
        }
        chunk_mesh_uploads.emplace_back(u);
      }
    }
    // else {
    //   if (depth >= MaxDepth) {
    //     continue;
    //   }
    //   // auto to_height_map_idx = [](int x, int z) { return (z * PCS) + x; };
    //   // auto get_offset = [](int depth) { return (2 << (depth)) * CS; };
    //   // int off = get_offset(depth + 1);
    //
    //   // for (int quad = 0; quad < 4; quad++) {
    //   //   // int x_offset = (quad % 2) * (PCS / 2);
    //   //   // int z_offset = (quad / 2) * (PCS / 2);
    //   //   ivec2 quad_chunk_pos_xz = ivec2{pos.x + (off * (quad % 2)), pos.z + (off * (quad /
    //   //   2))}; ivec3 quad_hm_key = ivec3{quad_chunk_pos_xz.x, quad_chunk_pos_xz.y, depth + 1};
    //   //
    //   //   auto it = height_maps.find(quad_hm_key);
    //   //   if (it == height_maps.end()) {
    //   //     it = height_maps.emplace(quad_hm_key, HeightMapFloats{}).first;
    //   //     // HeightMapFloats& quadrant_hm = it->second;
    //   //     // HeightMapFloats new_noise_map;
    //   //
    //   //     // FillNoise(new_noise_map, vec2{pos.x, pos.z} + vec2{quad_hm_key.x,
    //   quad_hm_key.y});
    //   //     // for (int z = 0, i = 0; z < PCS; z++) {
    //   //     //   for (int x = 0; x < PCS; x++, i++) {
    //   //     //     int x1 = (x / 2) + x_offset;
    //   //     //     int x2 = x1 + 1;
    //   //     //     int z1 = (z / 2) + z_offset;
    //   //     //     int z2 = z1 + 1;
    //   //     //
    //   //     //     // Clamp indices to avoid out-of-bounds
    //   //     //     x1 = std::min(x1, PCS - 1);
    //   //     //     x2 = std::min(x2, PCS - 1);
    //   //     //     z1 = std::min(z1, PCS - 1);
    //   //     //     z2 = std::min(z2, PCS - 1);
    //   //     //     float v1 = height_map[to_height_map_idx(x1, z1)];
    //   //     //     float v2 = height_map[to_height_map_idx(x2, z1)];
    //   //     //     float v3 = height_map[to_height_map_idx(x1, z2)];
    //   //     //     float v4 = height_map[to_height_map_idx(x2, z2)];
    //   //     //     float tx = (x % 2) / 2.f;
    //   //     //     float tz = (z % 2) / 2.f;
    //   //     //     float interpolated = (v1 * (1.f - tx) * (1.f - tz)) + (v2 * (1.f - tz) * tx)
    //   +
    //   //     //                          (v3 * (1.f - tx) * tz) + (v4 * tx * tz);
    //   //     //
    //   //     //     quadrant_hm[i] = interpolated + new_noise_map[i];
    //   //     //   }
    //   //     // }
    //   //   }
    //   // }
    //
    //   // TODO: refactor
    //   // int i = 0;
    //   // for (int y = 0; y < 2; y++) {
    //   //   for (int z = 0; z < 2; z++) {
    //   //     for (int x = 0; x < 2; x++) {
    //   //       NodeStackItem e{};
    //   //       e.depth = depth + 1;
    //   //       e.pos = pos + ivec3{x, y, z} * off;
    //   //       // fmt::println("depth {} off {}", depth + 1, off);
    //   //       // TODO: check if child exists!
    //   //       uint32_t new_node_handle = nodes[e.depth].AllocNode();
    //   //       // nodes[depth].Get(node_idx)->data[i] = new_node_handle;
    //   //       e.node_idx = new_node_handle;
    //   //       node_queue.emplace(e);
    //   //       // i++;
    //   //     }
    //   //   }
    //   // }
    //
    //   // if height maps for children don't exist, make them
    //   // bilinear interpolate the floats to get 4 new ones
    //
    //   // if no mesh or terrain, make it
    // }
    // }

    if (chunk_mesh_uploads.size()) {
      auto old_count = mesh_handles.size();
      mesh_handles.resize(mesh_handles.size() + chunk_mesh_uploads.size());
      std::span<ChunkAllocHandle> s(mesh_handles.begin() + old_count, mesh_handles.size());
      ChunkMeshManager::Get().UploadChunkMeshes(chunk_mesh_uploads, s);
    }
    chunk_mesh_uploads.clear();
  }

 private:
  float freq_{0.005};
  int seed_{1};
  void FillNoise(HeightMapFloats& floats, ivec2 pos) const {
    noise.white_noise->GenUniformGrid2D(floats.data(), pos.x, pos.y, PCS, PCS, freq_, seed_);
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
