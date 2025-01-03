#pragma once

#include "ChunkMeshManager.hpp"
#include "Grid3D.hpp"
#include "Mesher.hpp"
#include "Pool.hpp"
#include "application/Timer.hpp"
#include "concurrentqueue.h"
#include "voxels/Chunk.hpp"
#include "voxels/Terrain.hpp"

struct GridAndPos {
  PaddedChunkGrid3D grid;
  ivec3 pos;
};
struct MeshTask {
  MesherOutputData* data;
  uint32_t alg_data_idx;
  GridAndPos* grid;
  void Process();
};

struct TerrainGenTask {
  GridAndPos* grid;
  ivec3 pos;
  gen::FBMNoise* noise;
};

struct TerrainGenResponse {
  GridAndPos* grid;
  ivec3 pos;
  gen::FBMNoise* noise;
};

template <typename T, typename D>
struct TaskPool {
  size_t in_flight{0};
  // moodycamel::ConcurrentQueue<T> to_complete_tasks;
  size_t to_complete_task_queue_size{};
  moodycamel::ConcurrentQueue<D> done_tasks;
  void Clear() {
    D cmp;
    while (to_complete_task_queue_size > 0) {
      if (!done_tasks.try_dequeue(cmp)) {
        break;
      }
      to_complete_task_queue_size--;
    }
    to_complete_task_queue_size = 0;
    in_flight = 0;
  }
};
struct VoxelWorld {
  void Update();
  vec3 position;
  void Init();
  void Reset();
  void GenerateWorld(int radius);
  void Shutdown();
  void DrawImGuiStats() const;

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

  std::vector<ChunkAllocHandle> mesh_handles_;
  size_t max_mesh_tasks_;
  size_t max_terrain_tasks_;
  std::vector<ivec3> to_gen_terrain_tasks_;

  std::vector<ChunkMeshUpload> chunk_mesh_uploads_;
  TerrainGenResponse ProcessTerrainTask(TerrainGenTask& task);
  MeshTask ProcessMeshTask(MeshTask& task);
  const size_t num_threads_{std::thread::hardware_concurrency()};
  int seed_ = 1;

  FixedSizePool<GridAndPos> grid_pool_;
  PtrObjPool<MeshAlgData> mesh_alg_pool_;
  FixedSizePool<MesherOutputData> mesher_output_data_pool_;
  FixedSizePool<gen::FBMNoise> noise_generator_pool_;

  TaskPool<TerrainGenTask, TerrainGenResponse> terrain_tasks_;
  TaskPool<MeshTask, MeshTask> mesh_tasks_;

  Timer world_start_timer_;
  int world_gen_chunk_payload_{};
  int world_start_finished_chunks_{};
  int prev_world_start_finished_chunks_{};
  float worldNload_time_{};

  bool initalized_{false};
};
