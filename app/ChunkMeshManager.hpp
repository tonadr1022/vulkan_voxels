#pragma once

#include <vulkan/vulkan_core.h>

#include <span>

#include "GPUBufferAllocator.hpp"
#include "Types.hpp"
#include "application/Renderer.hpp"
#include "voxels/Common.hpp"
#include "voxels/Types.hpp"

struct StagingBufferPool;
struct VoxelRenderer;

struct ChunkUniformData {
  ivec4 pos;
};

struct ChunkDrawUniformData {
  ivec4 position;
  uint vertex_counts[8];
  bool operator==(const ChunkDrawUniformData& other) const { return position == other.position; }
};
struct UploadUniformParams {
  ChunkDrawUniformData data;
  uint32_t staging_copy_idx;
  bool stale;
};

using ChunkAllocHandle = uint32_t;
class ChunkMeshManager {
 public:
  static ChunkMeshManager& Get();
  void Init(VoxelRenderer* renderer);
  void DrawImGuiStats() const;
  void Cleanup();
  [[nodiscard]] uint32_t CopyChunkToStaging(const uint8_t* data, uint32_t quad_cnt);
  [[nodiscard]] uint32_t CopyChunkToStaging(const uint64_t* data, uint32_t quad_cnt);
  void UploadChunkMeshes(std::span<ChunkMeshUpload> uploads,
                         std::vector<ChunkAllocHandle>& handles);
  void FreeMeshes(std::span<ChunkAllocHandle> handles);
  // void Update();
  void CopyDrawBuffers();

  static constexpr const uint32_t MaxQuads{1000000000};
  static constexpr const uint32_t MaxDrawCmds{256 * 256 * 6};
#ifdef PACK_QUAD
  static constexpr const uint32_t QuadSize = sizeof(uint8_t) * 5;
#else
  static constexpr const uint32_t QuadSize = sizeof(uint8_t) * 8;
#endif

  [[nodiscard]] size_t QuadCount() const { return quad_count_; }

  VertexPool<ChunkDrawUniformData> chunk_quad_buffer_;

 private:
  friend struct VoxelRenderer;
  VoxelRenderer* renderer_{};

  size_t quad_count_{};
  std::vector<AsyncTransfer> transfers_;
  tvk::AllocatedBuffer quad_index_buf_;
  tvk::AllocatedBuffer chunk_uniform_gpu_buf_;
  std::vector<UploadUniformParams> upload_uniform_params_;
};
