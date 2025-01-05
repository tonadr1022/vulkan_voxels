#pragma once

#include <vulkan/vulkan_core.h>

#include <list>
#include <span>

#include "GPUBufferAllocator.hpp"
#include "Types.hpp"
#include "application/Renderer.hpp"
#include "voxels/Types.hpp"

struct StagingBufferPool;
struct VoxelRenderer;

struct ChunkUniformData {
  ivec4 pos;
};

struct ChunkDrawUniformData {
  ivec4 position;
  uint vertex_counts[8];
};

using ChunkAllocHandle = uint32_t;
class ChunkMeshManager {
 public:
  static ChunkMeshManager& Get();
  void Init(VoxelRenderer* renderer);
  void DrawImGuiStats() const;
  void Cleanup();
  [[nodiscard]] uint32_t CopyChunkToStaging(uint64_t* data, uint32_t cnt);
  void UploadChunkMeshes(std::span<ChunkMeshUpload> uploads, std::span<ChunkAllocHandle> handles);
  void FreeMeshes(std::span<ChunkAllocHandle> handles);
  void Update();
  void CopyDrawBuffers();

  static constexpr const uint32_t MaxQuads{1000000000};
  static constexpr const uint32_t MaxDrawCmds{256 * 256 * 10};
  static constexpr const uint32_t QuadSize = sizeof(uint64_t);

  [[nodiscard]] size_t QuadCount() const { return quad_count_; }

 private:
  friend struct VoxelRenderer;
  VoxelRenderer* renderer_{};

  size_t quad_count_{};
  std::vector<AsyncTransfer> transfers_;
  VertexPool<ChunkDrawUniformData> chunk_quad_buffer_;
  tvk::AllocatedBuffer quad_index_buf_;
  tvk::AllocatedBuffer chunk_uniform_gpu_buf_;
};
