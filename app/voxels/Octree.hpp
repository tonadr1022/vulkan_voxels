#pragma once

#include <cstdint>

#include "voxels/Terrain.hpp"

struct MeshOctree {
  MeshOctree() { noise.Init(1, 0.005, 4); }
  void GenerateMesh(uvec3 pos, uint8_t ) {
    int scale = 1;
    HeightMapFloats<i8vec3{PCS}> heights;
    noise.FillNoise2D<i8vec3{PCS}>(heights,
                                   uvec2{(pos.x * CS / scale) - 1, (pos.y * CS / scale) - 1});
  }

  gen::FBMNoise noise;
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
  template <typename NodeT>
  struct NodeList {
    std::vector<uint32_t> free_list;
    std::vector<NodeT> nodes;
    uint32_t AllocNode() {
      if (!free_list.empty()) {
        auto idx = free_list.back();
        free_list.pop_back();
        return idx;
      }
      uint32_t h = free_list.size();
      Node& node = nodes.emplace_back();
      node.ClearAll();
      return h;
    }

    Node* Get(uint32_t idx) {
      EASSERT(idx < nodes.size());
      return &nodes[idx];
    }

    // TODO: destructor?
    void Free(uint32_t idx) {
      free_list.emplace_back(idx);
      nodes[idx] = {};
    }
  };

  std::array<NodeList<Node>, MaxDepth> nodes_;
};
