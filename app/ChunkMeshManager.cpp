#include "ChunkMeshManager.hpp"

#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "Resource.hpp"
#include "StagingBufferPool.hpp"
#include "VoxelRenderer.hpp"
#include "application/Renderer.hpp"

void ChunkMeshManager::Draw() {}

void ChunkMeshManager::Cleanup() {}

void ChunkMeshManager::Init(VoxelRenderer* renderer) {
  renderer_ = renderer;
  chunk_quad_buffer_.Init(MaxQuads, sizeof(uint64_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &renderer_->allocator_);
  draw_indirect_staging_buf_ =
      renderer_->allocator_.CreateStagingBuffer(sizeof(VkDrawIndexedIndirectCommand) * MaxDrawCmds);
  renderer_->main_deletion_queue_.PushFunc([this]() {
    if (draw_indir_gpu_buf_.buffer) {
      renderer_->allocator_.DestroyBuffer(draw_indir_gpu_buf_);
    }
    renderer_->allocator_.DestroyBuffer(draw_indirect_staging_buf_);

    chunk_quad_buffer_.Destroy();
  });
}

void ChunkMeshManager::UploadChunkMeshes(std::span<ChunkMeshUpload> uploads) {
  ZoneScoped;
  WaitingResource<ChunkMeshUploadBatch> new_mesh_upload_batch;

  size_t tot_upload_size_bytes{0};
  for (auto& [count, first_instance, data] : uploads) {
    auto size_bytes = count * sizeof(uint64_t);
    tot_upload_size_bytes += size_bytes;
    ChunkMeshUploadInt upload{};
    upload.first_instance = first_instance;
    upload.vertex_count = count;
    upload.quad_buf_alloc_handle =
        chunk_quad_buffer_.InitCopy(size_bytes, data, upload.base_vertex);
    new_mesh_upload_batch.data.uploads.emplace_back(upload);
  }
  std::unique_ptr<tvk::AllocatedBuffer> buf =
      renderer_->staging_buffer_pool_.GetBuffer(tot_upload_size_bytes);
  chunk_quad_buffer_.CopyToStaging(*buf);
  {
    ZoneScopedN("submit upload chunk mesh");
    auto* staging = buf.get();
    new_mesh_upload_batch.transfer =
        renderer_->TransferSubmitAsync([this, staging](VkCommandBuffer cmd) {
          TracyVkZone(renderer_->graphics_queue_ctx_, cmd, "Upload chunk quads");
          chunk_quad_buffer_.FlushToGPU(*staging, cmd);
          // transfer ownership to renderer?
        });
    new_mesh_upload_batch.staging_buf = std::move(buf);
  }
  pending_mesh_uploads_.emplace_back(std::move(new_mesh_upload_batch));
}

ChunkMeshManager& ChunkMeshManager::Get() {
  static ChunkMeshManager instance;
  return instance;
}

void ChunkMeshManager::Update() {
  ZoneScoped;
  for (auto it = pending_mesh_uploads_.begin(); it != pending_mesh_uploads_.end();) {
    auto& upload = *it;
    auto status = vkGetFenceStatus(renderer_->device_, upload.transfer.fence);
    assert(status != VK_ERROR_DEVICE_LOST);
    if (status == VK_ERROR_DEVICE_LOST) {
      fmt::println("failed!");
      exit(1);
    }
    if (status == VK_SUCCESS) {
      renderer_->staging_buffer_pool_.ReturnBuffer(std::move(upload.staging_buf));
      // mesh upload finished, add the draw command
      for (auto& b : upload.data.uploads) {
        VkDrawIndexedIndirectCommand cmd;
        cmd.indexCount = b.vertex_count * 6;
        cmd.instanceCount = 1;
        cmd.firstIndex = 0;
        cmd.firstInstance = b.first_instance;
        cmd.vertexOffset = (b.base_vertex / sizeof(uint64_t)) << 2;
        draw_indir_cmds_.emplace_back(cmd);
      }
      renderer_->free_transfer_cmd_buffers_.emplace_back(upload.transfer.cmd);
      renderer_->fence_pool_.AddFreeFence(upload.transfer.fence);
      it = pending_mesh_uploads_.erase(it);
    } else {
      fmt::println("waiting on fence");
      it++;
    }
  }
  if (!draw_indir_cmds_.empty()) {
    ZoneScopedN("draw indirect update");
    auto copy_size = sizeof(VkDrawIndexedIndirectCommand) * draw_indir_cmds_.size();
    auto* data = reinterpret_cast<VkDrawIndexedIndirectCommand*>(draw_indirect_staging_buf_.data);
    memcpy(data, draw_indir_cmds_.data(), copy_size);
    if (copy_size > draw_indir_gpu_buf_.size) {
      auto old = draw_indir_gpu_buf_;
      renderer_->GetCurrentFrame().deletion_queue.PushFunc(
          [this, old]() { renderer_->allocator_.DestroyBuffer(old); });
      draw_indir_gpu_buf_ = renderer_->allocator_.CreateBuffer(
          copy_size,
          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          VMA_MEMORY_USAGE_GPU_ONLY);
    }
  }
}
