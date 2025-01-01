#include "ChunkMeshManager.hpp"

#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "Resource.hpp"
#include "StagingBufferPool.hpp"
#include "VoxelRenderer.hpp"
#include "application/Renderer.hpp"
#include "imgui.h"
#include "voxels/Common.hpp"

void ChunkMeshManager::Cleanup() {}

void ChunkMeshManager::Init(VoxelRenderer* renderer) {
  renderer_ = renderer;
  chunk_quad_buffer_.Init(MaxQuads, QuadSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                          &renderer_->allocator_);

  auto initial_draw_indirect_buf_size = sizeof(DIIC) * MaxDrawCmds;
  draw_indirect_staging_buf_ =
      renderer_->allocator_.CreateStagingBuffer(initial_draw_indirect_buf_size);
  MakeDrawIndirectGPUBuf(initial_draw_indirect_buf_size);

  auto initial_uniform_buf_size = sizeof(ChunkUniformData) * MaxDrawCmds;
  chunk_uniform_staging_buf_ = renderer_->allocator_.CreateStagingBuffer(initial_uniform_buf_size);
  MakeChunkUniformGPUBuf(initial_uniform_buf_size);

  renderer_->main_deletion_queue_.PushFunc([this]() {
    renderer_->allocator_.DestroyBuffer(draw_indir_gpu_buf_);
    renderer_->allocator_.DestroyBuffer(chunk_uniform_staging_buf_);
    renderer_->allocator_.DestroyBuffer(draw_indirect_staging_buf_);
    renderer_->allocator_.DestroyBuffer(chunk_uniform_gpu_buf_);

    chunk_quad_buffer_.Destroy();
  });
  constexpr int MaxQuadsPerChunk = CS3 * 6;
  constexpr int QuadsIndexBufSize = MaxQuadsPerChunk * sizeof(uint32_t);
  std::vector<uint32_t> indices;
  for (int i = 0; i < MaxQuadsPerChunk; i++) {
    indices.push_back((i << 2) | 2u);
    indices.push_back((i << 2) | 0u);
    indices.push_back((i << 2) | 1u);
    indices.push_back((i << 2) | 1u);
    indices.push_back((i << 2) | 3u);
    indices.push_back((i << 2) | 2u);
  }

  quad_index_buf_ = renderer_->allocator_.CreateBuffer(
      QuadsIndexBufSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
  renderer_->main_deletion_queue_.PushFunc(
      [this]() { renderer_->allocator_.DestroyBuffer(quad_index_buf_); });
  tvk::AllocatedBuffer staging = renderer_->allocator_.CreateStagingBuffer(QuadsIndexBufSize);
  memcpy(staging.data, indices.data(), QuadsIndexBufSize);
  renderer_->ImmediateSubmit([&staging, this](VkCommandBuffer cmd) {
    VkBufferCopy vertex_copy{};
    vertex_copy.dstOffset = 0;
    vertex_copy.srcOffset = 0;
    vertex_copy.size = QuadsIndexBufSize;
    vkCmdCopyBuffer(cmd, staging.buffer, quad_index_buf_.buffer, 1, &vertex_copy);
  });
  renderer_->allocator_.DestroyBuffer(staging);
}

void ChunkMeshManager::UploadChunkMeshes(std::span<ChunkMeshUpload> uploads) {
  ZoneScoped;
  WaitingResource<ChunkMeshUploadBatch> new_mesh_upload_batch;

  size_t tot_upload_size_bytes{0};
  for (const auto& [pos, count, first_instance, data] : uploads) {
    if (!count) continue;
    auto size_bytes = count * QuadSize;
    tot_upload_size_bytes += size_bytes;
    static int i = 0;
    ChunkMeshUploadInternal upload{};
    upload.first_instance = i++;
    upload.vertex_count = count;
    uint32_t offset;
    upload.quad_buf_alloc_handle = chunk_quad_buffer_.AddCopy(size_bytes, data, offset);
    upload.base_vertex = (offset / QuadSize) << 2;
    upload.pos = ivec4(pos, first_instance >> 24);
    new_mesh_upload_batch.data.uploads.emplace_back(upload);
  }
  if (!tot_upload_size_bytes) return;
  std::unique_ptr<tvk::AllocatedBuffer> buf =
      renderer_->staging_buffer_pool_.GetBuffer(tot_upload_size_bytes);
  chunk_quad_buffer_.CopyToStaging(*buf);
  {
    ZoneScopedN("submit upload chunk mesh");
    auto* staging = buf.get();
    new_mesh_upload_batch.transfer =
        renderer_->TransferSubmitAsync([this, staging](VkCommandBuffer cmd) {
          TracyVkZone(renderer_->graphics_queue_ctx_, cmd, "Upload chunk quads");
          chunk_quad_buffer_.ExecuteCopy(*staging, cmd);
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
  ZoneScopedN("ChunkMeshManager Update");
  for (auto it = pending_mesh_uploads_.begin(); it != pending_mesh_uploads_.end();) {
    auto& upload = *it;
    auto status = vkGetFenceStatus(renderer_->device_, upload.transfer.fence);
    EASSERT(status != VK_ERROR_DEVICE_LOST);
    if (status == VK_ERROR_DEVICE_LOST) {
      fmt::println("failed!");
      exit(1);
    }
    if (status == VK_SUCCESS) {
      renderer_->staging_buffer_pool_.ReturnBuffer(std::move(upload.staging_buf));
      // mesh upload finished, add the draw command
      for (auto& b : upload.data.uploads) {
        DIIC cmd;
        cmd.cmd.indexCount = b.vertex_count * 6;
        cmd.cmd.instanceCount = 1;
        cmd.cmd.firstIndex = 0;
        cmd.cmd.firstInstance = b.first_instance;
        cmd.cmd.vertexOffset = b.base_vertex;
        draw_indir_cmds_.emplace_back(cmd);
        chunk_uniforms_.emplace_back(b.pos);
      }
      renderer_->free_transfer_cmd_buffers_.emplace_back(upload.transfer.cmd);
      renderer_->fence_pool_.AddFreeFence(upload.transfer.fence);
      it = pending_mesh_uploads_.erase(it);
    } else {
      // fmt::println("waiting on fence");
      it++;
    }
  }

  // TODO: this is bad
  if (!draw_indir_cmds_.empty()) {
    ZoneScopedN("draw indirect update");
    auto copy_size = sizeof(DIIC) * draw_indir_cmds_.size();
    auto* data = reinterpret_cast<DIIC*>(draw_indirect_staging_buf_.data);
    // TODO: also  very bad
    auto uniform_copy_size = sizeof(ChunkUniformData) * chunk_uniforms_.size();
    {
      memcpy(reinterpret_cast<ChunkUniformData*>(chunk_uniform_staging_buf_.data),
             chunk_uniforms_.data(), chunk_uniforms_.size() * sizeof(ChunkUniformData));

      if (uniform_copy_size > chunk_uniform_gpu_buf_.size) {
        auto old = chunk_uniform_gpu_buf_;
        renderer_->GetCurrentFrame().deletion_queue.PushFunc(
            [this, old]() { renderer_->allocator_.DestroyBuffer(old); });
        auto new_size = old.size * 1.5;
        MakeChunkUniformGPUBuf(new_size);
      }
    }
    {
      ZoneScopedN("memcpy to staging");
      memcpy(data, draw_indir_cmds_.data(), copy_size);
    }
    if (copy_size > draw_indir_gpu_buf_.size) {
      auto old = draw_indir_gpu_buf_;
      renderer_->GetCurrentFrame().deletion_queue.PushFunc(
          [this, old]() { renderer_->allocator_.DestroyBuffer(old); });
      MakeDrawIndirectGPUBuf(
          static_cast<size_t>(static_cast<double>(draw_indir_gpu_buf_.size) * 1.5));
    }
    {
      ZoneScopedN("Submit");
      renderer_->ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy copy{};
        copy.dstOffset = 0;
        copy.srcOffset = 0;
        copy.size = copy_size;
        vkCmdCopyBuffer(cmd, draw_indirect_staging_buf_.buffer, draw_indir_gpu_buf_.buffer, 1,
                        &copy);
        {
          copy.size = uniform_copy_size;
          vkCmdCopyBuffer(cmd, chunk_uniform_staging_buf_.buffer, chunk_uniform_gpu_buf_.buffer, 1,
                          &copy);
        }
      });
    }
  }
}

void ChunkMeshManager::DrawImGuiStats() { ImGui::Text("Draw cmds: %ld", draw_indir_cmds_.size()); }
void ChunkMeshManager::MakeDrawIndirectGPUBuf(size_t size) {
  ZoneScoped;
  draw_indir_gpu_buf_ = renderer_->allocator_.CreateBuffer(size,
                                                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                           VMA_MEMORY_USAGE_GPU_ONLY);
}
void ChunkMeshManager::MakeChunkUniformGPUBuf(size_t size) {
  ZoneScoped;
  chunk_uniform_gpu_buf_ = renderer_->allocator_.CreateBuffer(
      size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);
}
