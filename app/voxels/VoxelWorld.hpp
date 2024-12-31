#pragma once

#include "ChunkMeshManager.hpp"
#include "Grid3D.hpp"
#include "Mesher.hpp"
#include "Pool.hpp"
#include "application/Timer.hpp"
#include "concurrentqueue.h"
#include "voxels/Chunk.hpp"
#include "voxels/Terrain.hpp"

struct MeshTask {
  MesherOutputData* data;
  uint32_t alg_data_idx;
  size_t grid_idx;
  void Process();
};

struct TerrainGenTask {
  size_t grid_idx;
  ivec3 pos;
  gen::FBMNoise* noise;
};

struct TerrainGenResponse {
  size_t grid_idx;
  ivec3 pos;
  gen::FBMNoise* noise;
};

template <typename T, typename D>
struct TaskPool {
  size_t in_flight{0};
  moodycamel::ConcurrentQueue<T> to_complete_tasks;
  size_t to_complete_task_queue_size{};
  moodycamel::ConcurrentQueue<D> done_tasks;
};
struct VoxelWorld {
  void Update();
  vec3 position;
  void Init();
  void GenerateWorld();
  int world_gen_chunk_payload{};
  int done{};
  int prev_done{};
  float world_load_time{};
  Timer t;
  void DrawImGuiStats() const;

 private:
  size_t max_mesh_tasks_;
  size_t max_terrain_tasks_;
  std::vector<ivec3> to_gen_terrain_tasks_;

  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  TerrainGenResponse ProcessTerrainTask(TerrainGenTask& task);
  MeshTask ProcessMeshTask(MeshTask& task);
  const size_t num_threads_{std::thread::hardware_concurrency()};
  int seed_ = 1;
  std::vector<std::unique_ptr<PaddedChunkGrid3D>> grids_;
  std::vector<ivec3> grid_positions_;
  FixedSizePtrPool<MeshAlgData> mesh_alg_pool_;
  FixedSizePool<MesherOutputData> mesher_output_data_pool_;
  FixedSizePool<gen::FBMNoise> noise_generator_pool_;

  moodycamel::ConcurrentQueue<MeshTask> mesh_tasks_;
  TaskPool<TerrainGenTask, TerrainGenResponse> terrain_tasks_;
  TaskPool<MeshTask, MeshTask> mesh_ts_;
};
