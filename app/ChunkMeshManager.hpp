#pragma once

#include <vulkan/vulkan_core.h>

#include <list>
#include <span>

#include "GPUBufferAllocator.hpp"
#include "Resource.hpp"
#include "application/Renderer.hpp"

struct StagingBufferPool;
struct VoxelRenderer;

class ChunkMeshManager {
 public:
  static ChunkMeshManager& Get();
  void Init(VoxelRenderer* renderer);
  void Cleanup();
  struct ChunkMeshUpload {
    uint32_t count;
    uint32_t first_instance;
    void* data;
  };
  void UploadChunkMeshes(std::span<ChunkMeshUpload> uploads);
  void Update();

  static constexpr const uint32_t MaxQuads{1000000000};
  static constexpr const uint32_t MaxDrawCmds{256 * 256 * 10};

 private:
  friend struct VoxelRenderer;
  template <typename T>
  struct WaitingResource {
    T data;
    AsyncTransfer transfer;
    std::unique_ptr<tvk::AllocatedBuffer> staging_buf;
  };
  struct ChunkMeshUploadInt {
    uint32_t first_instance;
    uint32_t base_vertex;
    uint32_t vertex_count;
    uint32_t quad_buf_alloc_handle;
  };
  struct ChunkMeshUploadBatch {
    std::vector<ChunkMeshUploadInt> uploads;
  };
  VoxelRenderer* renderer_{};
  std::vector<VkDrawIndexedIndirectCommand> draw_indir_cmds_;
  tvk::AllocatedBuffer draw_indirect_staging_buf_;
  tvk::AllocatedBuffer draw_indir_gpu_buf_{};
  std::list<WaitingResource<ChunkMeshUploadBatch>> pending_mesh_uploads_;
  GPUBufferAllocator<uint64_t> chunk_quad_buffer_;
  tvk::AllocatedBuffer quad_index_buf_;
};
