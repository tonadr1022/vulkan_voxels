#pragma once

#include <vulkan/vulkan_core.h>

#include <list>
#include <span>

#include "GPUBufferAllocator.hpp"
#include "application/Renderer.hpp"
#include "voxels/Types.hpp"

struct StagingBufferPool;
struct VoxelRenderer;

class ChunkMeshManager {
 public:
  static ChunkMeshManager& Get();
  void Init(VoxelRenderer* renderer);
  void DrawImGuiStats();
  void Cleanup();
  void UploadChunkMeshes(std::span<ChunkMeshUpload> uploads);
  void Update();

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
    uint32_t first_instance;
    uint32_t base_vertex;
    uint32_t vertex_count;
    uint32_t quad_buf_alloc_handle;
  };
  struct ChunkMeshUploadBatch {
    std::vector<ChunkMeshUploadInternal> uploads;
  };
  VoxelRenderer* renderer_{};
  std::vector<VkDrawIndexedIndirectCommand> draw_indir_cmds_;
  tvk::AllocatedBuffer draw_indirect_staging_buf_;
  tvk::AllocatedBuffer draw_indir_gpu_buf_{};
  std::list<WaitingResource<ChunkMeshUploadBatch>> pending_mesh_uploads_;
  GPUBufferAllocator<uint64_t> chunk_quad_buffer_;
  tvk::AllocatedBuffer quad_index_buf_;
  void MakeDrawIndirectGPUBuf(size_t size);
};
