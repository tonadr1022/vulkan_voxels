#pragma once

#include <vulkan/vulkan_core.h>

#include <list>
#include <span>

#include "GPUBufferAllocator.hpp"
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
  void UploadChunkMeshes(std::span<ChunkMeshUpload> uploads, std::span<ChunkAllocHandle> handles);
  void Update();
  void CopyDrawBuffers();

  static constexpr const uint32_t MaxQuads{1000000000};
  static constexpr const uint32_t MaxDrawCmds{256 * 256 * 10};
  static constexpr const uint32_t QuadSize = sizeof(uint64_t);

 private:
  friend struct VoxelRenderer;
  template <typename T>
  struct WaitingResource {
    T data;
    AsyncTransfer transfer;
    std::unique_ptr<tvk::AllocatedBuffer> staging_buf;
  };
  struct ChunkMeshUploadInternal {
    // ChunkUniformData uniform;
    // ChunkAllocHandle handle;
    // uint32_t base_vertex;
    // uint32_t vertex_count;
  };
  struct ChunkMeshUploadBatch {
    // std::vector<ChunkMeshUploadInternal> uploads;
  };
  VoxelRenderer* renderer_{};
  // std::vector<DIIC> draw_indir_cmds_;
  // std::vector<ChunkUniformData> chunk_uniforms_;

  std::list<WaitingResource<ChunkMeshUploadBatch>> pending_mesh_uploads_;
  VertexPool<ChunkDrawUniformData> chunk_quad_buffer_;
  tvk::AllocatedBuffer quad_index_buf_;
  tvk::AllocatedBuffer chunk_uniform_gpu_buf_;

  // void MakeDrawIndirectGPUBuf(size_t size);
  // void MakeChunkUniformGPUBuf(size_t size);
};
