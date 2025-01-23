#pragma once

#include <cstdint>

#include "EAssert.hpp"
#include "Pool.hpp"
#include "voxels/Types.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

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
  void Init();

  ChunkMeshUpload PrepareChunkMeshUpload(const MeshAlgData& alg_data, const MesherOutputData& data,
                                         ivec3 pos, uint32_t depth) const;

  void FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx, uint32_t depth,
                    ivec3 pos);

  void Reset();
  void Update(vec3 cam_pos);

  void OnImGui();

 private:
  // vec3 ChunkPosToAbsPos(ivec3 chunk_pos) { return vec3{chunk_pos} * vec3{PCS}; }
  [[nodiscard]] uint32_t GetOffset(uint32_t depth) const { return (1 << depth) * CS; }
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
    uint32_t chunk_state_handle;
    // TODO: refactor
    uint8_t leaf_mask;
  };

  // TODO: memory pool
  struct NodeQueueItem {
    uint32_t node_idx;
    ivec3 pos;
    uint32_t depth;
  };

  float freq_{0.005};
  int seed_{1};
  std::vector<NodeQueueItem> to_mesh_queue_;
  gen::FBMNoise noise_;
  static constexpr int AbsoluteMaxDepth = 25;
  static constexpr uint32_t MaxChunks = 20000;
  uint32_t max_depth_ = 3;
  std::vector<uint32_t> lod_bounds_;
  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  struct NodeKey {
    uint32_t depth;
    uint32_t idx;
  };
  std::vector<NodeKey> chunk_mesh_node_keys_;
  // TODO: refactor
  std::vector<uint32_t> mesh_handles_;
  std::vector<uint32_t> meshes_to_free_;
  std::vector<NodeQueueItem> node_queue_;
  std::array<NodeList<Node>, AbsoluteMaxDepth + 1> nodes_;
  std::vector<NodeQueueItem> child_free_stack_;
  std::unordered_map<ivec3, HeightMapData> height_maps_;

  struct ChunkState {
    using DataT = uint8_t;
    uint32_t mesh_handle{0};
    DataT data{NeedsGenMeshingFlag};
    [[nodiscard]] bool GetNeedsGenOrMeshing() const { return data & NeedsGenMeshingFlag; }
    void SetNeedsGenOrMeshing(bool v) {
      data ^= (-static_cast<DataT>(v) ^ data) & NeedsGenMeshingFlag;
    }

   private:
    constexpr static DataT NeedsGenMeshingFlag = 1 << 0;
  };
  ObjPool<ChunkState> chunk_states_;

  void FreeNode(uint32_t depth, uint32_t idx) { nodes_[depth].Free(idx); }
  Node* GetNode(uint32_t depth, uint32_t idx) { return GetNode(NodeKey{depth, idx}); }
  [[nodiscard]] const Node* GetNode(uint32_t depth, uint32_t idx) const {
    return GetNode(NodeKey{depth, idx});
  }
  Node* GetNode(const NodeKey& loc) { return &nodes_[loc.depth].nodes[loc.idx]; }
  [[nodiscard]] const Node* GetNode(const NodeKey& loc) const {
    return &nodes_[loc.depth].nodes[loc.idx];
  }

  bool HasChildren(NodeKey node_key) { return GetNode(node_key)->leaf_mask != 0; }
  uint32_t ChunkLenFromDepth(uint32_t depth) { return PCS * (1 << (AbsoluteMaxDepth - depth)); }
  void FillNoise(HeightMapFloats& floats, ivec2 pos) const {
    noise_.white_noise->GenUniformGrid2D(floats.data(), pos.x, pos.y, PCS, PCS, freq_, seed_);
  }
  HeightMapData& GetHeightMap(int x, int z, int lod);
  void UpdateLodBounds();
  void Validate();
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
