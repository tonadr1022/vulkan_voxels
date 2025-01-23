#pragma once

#include "ChunkMeshManager.hpp"
#include "Mesher.hpp"
#include "Pool.hpp"
#include "TaskPool.hpp"
#include "application/Timer.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Terrain.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

struct MeshTaskEnqueue {
  uint32_t chunk_handle;
};

struct MeshTaskResponse {
  uint32_t output_data_handle;
  uint32_t alg_data_handle;
  uint32_t chunk_handle;
  uint32_t staging_copy_idx;
  void Process();
};

struct TerrainGenTask {
  uint32_t chunk_handle;
  // HeightMapData* height_map;
};

struct TerrainGenResponse {
  uint32_t grid;
  ivec3 pos;
};

struct VoxelWorld {
  void Update(vec3 cam_pos);
  ivec3 CamPosToChunkPos(vec3 cam_pos);
  void Init();
  void Reset();
  void GenerateWorld(vec3 cam_pos);
  void Shutdown();
  void DrawImGuiStats();
  void FreeAllMeshes();

 private:
  void ResetPools();
  struct Stats {
    size_t tot_meshes{};
    size_t tot_quads{};
    size_t max_mesh_tasks{};
    size_t max_terrain_tasks{};
    size_t max_terrain_done_size{};
    size_t max_pool_size{};
    size_t max_pool_size2{};
    size_t max_pool_size3{};
  } stats_;

  size_t max_mesh_tasks_;
  size_t max_terrain_tasks_;
  std::vector<ivec3> to_gen_terrain_tasks_;

  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  TerrainGenResponse ProcessTerrainTask(const TerrainGenTask& task);
  MeshTaskResponse ProcessMeshTask(MeshTaskResponse& task);
  int seed_ = 1;

  PtrObjPool<Chunk> chunk_pool_;
  PtrObjPool<MeshAlgData> mesh_alg_pool_;
  PtrObjPool<MesherOutputData> mesher_output_data_pool_;
  PtrObjPool<HeightMapData> height_map_pool_;
  struct ChunkState {
    uint32_t mesh_handle{};
    enum State : uint8_t { None, TerrainGenerated, Meshed } state{};
  };
  std::unordered_map<ivec3, ChunkState> chunks;
  std::vector<ChunkAllocHandle> mesh_handle_alloc_buffer_;
  std::vector<uint32_t> meshes_to_delete;
  gen::FBMNoise noise_;

  HeightMapData* GetHeightMap(int x, int y);
  std::mutex height_map_mtx_;
  std::unordered_map<std::pair<int, int>, uint32_t> height_map_pool_idx_cache_;
  TaskPool<TerrainGenTask, TerrainGenResponse> terrain_tasks_;
  TaskPool<MeshTaskEnqueue, MeshTaskResponse> mesh_tasks_;

  Timer world_start_timer_;
  int world_gen_chunk_payload_{};
  int tot_chunks_loaded_{};
  int prev_world_start_finished_chunks_{};
  float world_load_time_{};

  vec3 curr_cam_pos_{};
  vec3 prev_cam_pos_{};
  int radius_{3};
  void ResetInternal();
  std::mutex reset_mtx_;
  bool initalized_{false};
};
