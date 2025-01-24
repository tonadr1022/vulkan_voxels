#pragma once

#include <chrono>
#include <cstdint>

#include "EAssert.hpp"
#include "Pool.hpp"
#include "RingBuffer.hpp"
#include "TaskPool.hpp"
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
  void Reset();
  void Update(vec3 cam_pos);
  void OnImGui();

 private:
  struct ChunkState {
    using DataT = uint32_t;
    uint32_t mesh_handle{0};
    uint32_t num_solid{0};
    DataT data{DataFlagsNeedsGenMeshing | DataFlagsChunkInRange | DataFlagsTerrainGenDirty};
    [[nodiscard]] bool GetNeedsGenOrMeshing() const { return data & DataFlagsNeedsGenMeshing; }
    void SetNeedsGenOrMeshing(bool v) {
      data ^= (-static_cast<DataT>(v) ^ data) & DataFlagsNeedsGenMeshing;
    }
    void SetFlags(DataT flags, bool v) { data ^= (-static_cast<DataT>(v) ^ data) & flags; }

    constexpr static DataT DataFlagsNeedsGenMeshing = 1 << 0;
    constexpr static DataT DataFlagsChunkInRange = 2 << 0;
    constexpr static DataT DataFlagsTerrainGenDirty = 3 << 0;
  };

  struct Node {
    static constexpr std::array<uint32_t, 8> Mask = {1 << 0, 1 << 1, 1 << 2, 1 << 3,
                                                     1 << 4, 1 << 5, 1 << 6, 1 << 7};
    void ClearMask(uint8_t idx) { mask &= ~Mask[idx]; }
    [[nodiscard]] bool IsSet(uint8_t child) const { return mask & Mask[child]; }
    void ClearMask() { mask = 0; }
    void SetData(uint8_t child, uint32_t val) {
      data[child] = val;
      mask |= Mask[child];
    }
    std::array<uint32_t, 8> data;
    uint32_t chunk_state_handle;
    uint8_t mask;
  };
  struct NodeQueueItem {
    uint32_t node_idx;
    ivec3 pos;
    uint32_t lod;
  };
  struct NodeKey {
    uint32_t lod;
    uint32_t idx;
  };
  struct OctreeHeightMapData {
    // HeightMapData data;
    std::chrono::steady_clock::time_point access;
    uint32_t height_map_pool_handle;
  };
  struct MeshGenTask {
    NodeKey node_key;
    uint32_t chunk_gen_data_handle;
    uint32_t chunk_state_handle;
    uint32_t staging_copy_idx;
    uint32_t vert_count;
    uint32_t vert_counts[6];
  };
  struct TerrainGenTask {
    NodeKey node_key;
    uint32_t chunk_gen_data_handle;
    uint32_t chunk_state_handle;
  };

  static constexpr int AbsoluteMaxDepth = 25;
  static constexpr uint32_t MaxChunks = 100000;
  std::vector<NodeQueueItem> to_mesh_queue_;
  gen::FBMNoise noise_;
  uint32_t max_depth_ = 25;
  std::vector<uint32_t> lod_bounds_;
  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  std::vector<NodeKey> chunk_mesh_node_keys_;
  // TODO: refactor
  std::vector<uint32_t> mesh_handle_upload_buffer_;
  std::vector<uint32_t> meshes_to_free_;
  std::vector<NodeQueueItem> node_queue_;
  std::array<NodeList<Node>, AbsoluteMaxDepth + 1> nodes_;
  std::vector<NodeQueueItem> child_free_stack_;
  std::unordered_map<ivec3, OctreeHeightMapData> height_maps_;
  std::mutex height_map_mtx_;
  // TODO: not pointers
  PtrObjPool<Chunk> chunk_pool_;
  PtrObjPool<HeightMapData> height_map_pool_;
  std::chrono::steady_clock::time_point last_height_map_cleanup_time_;
  ObjPool<ChunkState> chunk_states_pool_;
  TaskPool2<TerrainGenTask, MeshGenTask> terrain_tasks_;
  std::mutex mesh_alg_data_mtx_;
  RingBuffer<MeshAlgData> mesh_alg_buf_;
  RingBuffer<MesherOutputData> mesher_output_data_buf_;
  ivec3 prev_cam_chunk_pos_;
  ivec3 curr_cam_chunk_pos_;
  vec3 curr_cam_pos_;
  float freq_{0.005};
  int seed_{1};

  void ProcessTerrainTask(TerrainGenTask& task);
  void ProcessMeshGenTask(MeshGenTask& task);
  [[nodiscard]] uint32_t GetOffset(uint32_t depth) const { return (1 << depth) * CS; }
  void ClearOldHeightMaps(const std::chrono::steady_clock::time_point& now);
  void FreeNode(uint32_t depth, uint32_t idx) { nodes_[depth].Free(idx); }
  Node* GetNode(uint32_t depth, uint32_t idx) { return GetNode(NodeKey{depth, idx}); }
  [[nodiscard]] const Node* GetNode(uint32_t depth, uint32_t idx) const {
    return GetNode(NodeKey{depth, idx});
  }
  Node* GetNode(const NodeKey& loc) { return &nodes_[loc.lod].nodes[loc.idx]; }
  [[nodiscard]] const Node* GetNode(const NodeKey& loc) const {
    return &nodes_[loc.lod].nodes[loc.idx];
  }
  bool HasChildren(NodeKey node_key) { return GetNode(node_key)->mask != 0; }
  uint32_t ChunkLenFromDepth(uint32_t depth) { return PCS * (1 << (AbsoluteMaxDepth - depth)); }
  void FillNoise(HeightMapFloats& floats, ivec2 pos) const {
    noise_.white_noise->GenUniformGrid2D(floats.data(), pos.x, pos.y, PCS, PCS, freq_, seed_);
  }
  // uint32_t GetHeightMap(int x, int z, int lod);
  HeightMapData& GetHeightMap(int x, int z, int lod);
  void UpdateLodBounds();
  void Validate();
  bool ChunkInHeightMapRange(ivec2 hm_range, int lod, ivec3 chunk_pos) {
    return chunk_pos.y <= hm_range[1] &&
           ((static_cast<int>(ChunkLenFromDepth(lod)) + chunk_pos.y) >= hm_range[0]);
  }
  void FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx, uint32_t depth,
                    ivec3 pos);
  ivec3 ChunkCenter(ivec3 pos, int lod) const { return pos + ((1 << (max_depth_ - lod)) * HALFCS); }
  bool ShouldMeshChunk(ivec3 pos, uint32_t lod);
  bool MeshCurrTest(ivec3 pos, uint32_t lod);
};

using VoxelData = uint8_t;
