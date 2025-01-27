#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_set>

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

template <typename T>
struct TSSet {
  std::mutex free_mt;
  std::unordered_set<T> data;
  void Add(T val) {
    std::lock_guard<std::mutex> lock(free_mt);
    data.insert(val);
  }
  bool Contains(T val) {
    std::lock_guard<std::mutex> lock(free_mt);
    return data.contains(val);
  }
  void Remove(T val) {
    std::lock_guard<std::mutex> lock(free_mt);
    data.erase(val);
  }
};
template <typename NodeT>
struct NodeList {
  struct NodeData {
    NodeT user_data;
    uint32_t generation{};
  };
  std::vector<uint32_t> free_list;
  std::vector<NodeData> nodes;
  uint32_t AllocNode() {
    if (!free_list.empty()) {
      auto idx = free_list.back();
      free_list.pop_back();
      // nodes[idx].generation++;
      nodes[idx].user_data = {};
      return idx;
    }
    uint32_t h = nodes.size();
    nodes.emplace_back(NodeData{});
    return h;
  }

  [[nodiscard]] size_t Size() const { return nodes.size() - free_list.size(); }

  NodeT* Get(uint32_t idx) {
    EASSERT(idx < nodes.size());
    return &nodes[idx].user_data;
  }
  // uint32_t GetGeneration(uint32_t idx) {
  //   EASSERT(idx < nodes.size());
  //   return nodes[idx].generation;
  // }

  // TODO: destructor?
  void Free(uint32_t idx) {
    nodes[idx].user_data = {};
    free_list.push_back(idx);
  }
  void Clear() {
    free_list.clear();
    // while (free_list.size()) free_list.pop_back();
    nodes.clear();
  }
};

struct NodeKey {
  uint32_t lod;
  uint32_t idx;
};
template <typename NodeT, int Depth>
struct MultiLevelNodeList {
  std::array<NodeList<NodeT>, Depth> nodes;
  void FreeNode(uint32_t depth, uint32_t idx) { nodes[depth].Free(idx); }
  uint32_t& GetGeneration(uint32_t depth, uint32_t idx) {
    return nodes[depth].nodes[idx].generation;
  }

  NodeT* GetNode(uint32_t depth, uint32_t idx) { return GetNode(NodeKey{depth, idx}); }
  [[nodiscard]] const NodeT* GetNode(uint32_t depth, uint32_t idx) const {
    return GetNode(NodeKey{depth, idx});
  }
  NodeT* GetNode(const NodeKey& loc) { return &nodes[loc.lod].nodes[loc.idx].user_data; }
  [[nodiscard]] const NodeT* GetNode(const NodeKey& loc) const {
    return &nodes[loc.lod].nodes[loc.idx];
  }
  bool HasChildren(NodeKey node_key) { return GetNode(node_key)->mask != 0; }
};

struct MeshOctree {
  void Init();
  void Reset();
  void Update(vec3 cam_pos);
  void OnImGui();

 private:
  struct Node {
    static constexpr std::array<uint32_t, 8> Mask = {1 << 0, 1 << 1, 1 << 2, 1 << 3,
                                                     1 << 4, 1 << 5, 1 << 6, 1 << 7};
    void ClearMask(uint8_t idx) { flags &= ~Mask[idx]; }
    [[nodiscard]] bool IsSet(uint8_t child) const { return flags & Mask[child]; }
    void ClearMask() { flags &= 0xFFFFFF00; }
    void SetData(uint8_t idx, uint32_t val) {
      data[idx] = val;
      flags |= Mask[idx];
    }
    std::array<uint32_t, 8> data;
    uint32_t num_solid{0};
    uint32_t mesh_handle{};
    uint32_t flags{DefaultFlags};

    using DataT = uint32_t;
    void SetFlags(DataT flags, bool v) { flags ^= (-static_cast<DataT>(v) ^ flags) & flags; }
    [[nodiscard]] bool GetNeedsGenOrMeshing() const { return flags & FlagsNotQueuedForMeshing; }
    void SetNeedsGenOrMeshing(bool v) {
      flags ^= (-static_cast<DataT>(v) ^ flags) & FlagsNotQueuedForMeshing;
    }
    void Reset() {
      flags = {DefaultFlags};
      num_solid = 0;
    }
    constexpr static DataT FlagsNotQueuedForMeshing = 1 << 8;
    constexpr static DataT DataFlagsChunkInRange = 1 << 9;
    constexpr static DataT DataFlagsTerrainGenDirty = 1 << 10;
    constexpr static DataT DataFlagsActive = 1 << 11;
    constexpr static DataT DefaultFlags = FlagsNotQueuedForMeshing | DataFlagsChunkInRange |
                                          DataFlagsTerrainGenDirty | DataFlagsActive;
  };
  struct NodeQueueItem {
    uint32_t node_idx;
    ivec3 pos;
    uint32_t lod;
    uint32_t node_generation;
  };
  struct OctreeHeightMapData {
    // HeightMapData data;
    std::chrono::steady_clock::time_point access;
    uint32_t height_map_pool_handle;
  };
  struct MeshGenTask {
    NodeKey node_key;
    Chunk* chunk;
    uint32_t staging_copy_idx;
    uint32_t vert_count;
    uint32_t vert_counts[6];
  };
  struct TerrainGenTask {
    NodeKey node_key;
    Chunk* chunk;
  };

  static constexpr int AbsoluteMaxDepth = 25;
  static constexpr uint32_t MaxChunks = 100000;

  struct NodeQueueItem2 {
    uint32_t node_idx;
    ivec3 pos;
    uint32_t lod;
    uint32_t node_generation;
  };
  std::queue<NodeQueueItem2> to_mesh_queue_;
  gen::FBMNoise noise_;
  uint32_t max_depth_ = 25;
  std::vector<uint32_t> lod_bounds_;
  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  std::vector<NodeKey> chunk_mesh_node_keys_;
  // TODO: refactor
  std::vector<uint32_t> mesh_handle_upload_buffer_;
  std::vector<uint32_t> meshes_to_free_;
  std::vector<NodeQueueItem> node_queue_;
  // std::array<NodeList<Node>, AbsoluteMaxDepth + 1> nodes_;
  MultiLevelNodeList<Node, AbsoluteMaxDepth + 1> nodes_;
  std::vector<NodeQueueItem> child_free_stack_;
  std::unordered_map<ivec3, OctreeHeightMapData> height_maps_;
  std::mutex height_map_mtx_;
  PtrObjPool<HeightMapData> height_map_pool_;
  std::chrono::steady_clock::time_point last_height_map_cleanup_time_;
  std::chrono::steady_clock::time_point last_octree_update_time_;
  TaskPool2<TerrainGenTask, MeshGenTask> terrain_tasks_;
  std::mutex mesh_alg_data_mtx_;
  RingBuffer<Chunk> chunk_pool_;
  RingBuffer<MeshAlgData> mesh_alg_buf_;
  RingBuffer<MesherOutputData> mesher_output_data_buf_;
  ivec3 prev_cam_chunk_pos_;
  ivec3 curr_cam_chunk_pos_;
  vec3 curr_cam_pos_;
  float freq_{0.005};
  int seed_{1};
  bool chunk_pos_dirty_{false};

  uint32_t AllocNode(uint32_t lod);
  void FreeNode(uint32_t lod, uint32_t idx) {
    auto* node = nodes_.GetNode(lod, idx);
    node->SetFlags(Node::DataFlagsActive, false);
    nodes_.FreeNode(lod, idx);
  }
  void ProcessTerrainTask(TerrainGenTask& task);
  void ProcessMeshGenTask(MeshGenTask& task);
  [[nodiscard]] uint32_t GetOffset(uint32_t depth) const { return (1 << depth) * CS; }
  void ClearOldHeightMaps(const std::chrono::steady_clock::time_point& now);
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

  // TSSet<uint32_t> freed_;
  void FreeChildren(std::vector<uint32_t>& meshes_to_free, uint32_t node_idx, uint32_t depth,
                    ivec3 pos);
  ivec3 ChunkCenter(ivec3 pos, int lod) const { return pos + ((1 << (max_depth_ - lod)) * HALFCS); }
  bool ShouldMeshChunk(ivec3 pos, uint32_t lod);
  bool MeshCurrTest(ivec3 pos, uint32_t lod);
  void DispatchTasks();
};

using VoxelData = uint8_t;
