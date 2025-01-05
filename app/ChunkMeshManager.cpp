#include "ChunkMeshManager.hpp"

#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "Error.hpp"
#include "GPUBufferAllocator.hpp"
#include "Resource.hpp"
#include "VoxelRenderer.hpp"
#include "application/CVar.hpp"
#include "application/Renderer.hpp"
#include "imgui.h"
#include "voxels/Common.hpp"

#define SINGLE_TRIANGLE_QUADS
void ChunkMeshManager::Cleanup() {
  if (transfers_.size()) {
    std::vector<VkFence> fences;
    fences.reserve(transfers_.size());
    for (const auto& t : transfers_) {
      fences.emplace_back(t.fence);
    }
    VK_CHECK(vkWaitForFences(renderer_->device_, fences.size(), fences.data(), true, UINT64_MAX));
    for (const auto& t : transfers_) {
      renderer_->ReturnFence(t.fence);
    }
  }
}

void ChunkMeshManager::Init(VoxelRenderer* renderer) {
  renderer_ = renderer;
  chunk_quad_buffer_.Init(MaxQuads, QuadSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MaxDrawCmds);
  chunk_uniform_gpu_buf_ = tvk::Allocator::Get().CreateBuffer(
      MaxDrawCmds * sizeof(ChunkUniformData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY);

  renderer_->main_deletion_queue_.PushFunc([this]() {
    renderer_->allocator_.DestroyBuffer(chunk_uniform_gpu_buf_);
    chunk_quad_buffer_.Destroy();
  });

#ifdef SINGLE_TRIANGLE_QUADS
  constexpr int MaxQuadsPerChunk = CS3 * 3;
#else
  constexpr int MaxQuadsPerChunk = CS3 * 6;
#endif
  constexpr int QuadsIndexBufSize = MaxQuadsPerChunk * sizeof(uint32_t);
  std::vector<uint32_t> indices;
  for (int i = 0; i < MaxQuadsPerChunk; i++) {
    indices.push_back((i << 2) | 2u);
    indices.push_back((i << 2) | 0u);
    indices.push_back((i << 2) | 1u);
#ifndef SINGLE_TRIANGLE_QUADS
    indices.push_back((i << 2) | 1u);
    indices.push_back((i << 2) | 3u);
    indices.push_back((i << 2) | 2u);
#endif
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

void ChunkMeshManager::FreeMeshes(std::span<ChunkAllocHandle> handles) {
  for (auto& h : handles) {
    quad_count_ -= chunk_quad_buffer_.FreeMesh(h) / sizeof(QuadSize);
  }
}

void ChunkMeshManager::UploadChunkMeshes(std::span<ChunkMeshUpload> uploads,
                                         std::span<ChunkAllocHandle> handles) {
  ZoneScoped;
  size_t tot_upload_size_bytes{0};
  size_t idx = 0;
  for (const auto& [pos, counts, staging_copy_idx] : uploads) {
    ChunkDrawUniformData d{};
    int mult = *CVarSystem::Get().GetIntCVar("chunks.chunk_mult");
    d.position = ivec4(pos, mult << 3);
    for (int i = 0; i < 6; i++) {
      quad_count_ += counts[i];
      d.vertex_counts[i] = counts[i];
    }
    ChunkAllocHandle handle = chunk_quad_buffer_.AddMesh(staging_copy_idx, d);
    handles[idx++] = handle;
  }
  if (!tot_upload_size_bytes) return;
}

ChunkMeshManager& ChunkMeshManager::Get() {
  static ChunkMeshManager instance;
  return instance;
}

void ChunkMeshManager::Update() {
  ZoneScopedN("ChunkMeshManager Update");
  if (chunk_quad_buffer_.copies.size()) {
    // transfers_.emplace_back(renderer_->TransferSubmitAsync(
    //     [this](VkCommandBuffer cmd) { chunk_quad_buffer_.ExecuteCopy(cmd); }));

    // renderer_->TransferSubmit([this](VkCommandBuffer cmd) { chunk_quad_buffer_.ExecuteCopy(cmd);
    // });

    transfers_.emplace_back(renderer_->TransferSubmitAsync(
        [this](VkCommandBuffer cmd) { chunk_quad_buffer_.ExecuteCopy(cmd); }));
  }
  for (auto it = transfers_.begin(); it != transfers_.end();) {
    auto& transfer = *it;
    auto status = vkGetFenceStatus(renderer_->device_, transfer.fence);
    EASSERT(status != VK_ERROR_DEVICE_LOST);
    if (status == VK_ERROR_DEVICE_LOST) {
      fmt::println("failed!");
      exit(1);
    }
    if (status == VK_SUCCESS) {
      renderer_->fence_pool_.AddFreeFence(transfer.fence);
      it = transfers_.erase(it);
    } else {
      // fmt::println("fence wait");
      it++;
    }
  }
}

void ChunkMeshManager::DrawImGuiStats() const {
  ImGui::Text("Draw cmds: %ld", chunk_quad_buffer_.draw_cmds_count);
  ImGui::Text("size %ld", sizeof(Allocation<ChunkDrawUniformData>));
}

void ChunkMeshManager::CopyDrawBuffers() {
  ZoneScoped;
  chunk_quad_buffer_.ResizeBuffers(renderer_->GetCurrentFrame().deletion_queue);
  // TODO: refactor
  if (chunk_quad_buffer_.draw_cmds_count > chunk_uniform_gpu_buf_.size / sizeof(ChunkUniformData)) {
    auto old = chunk_uniform_gpu_buf_;
    renderer_->GetCurrentFrame().deletion_queue.PushFunc(
        [old]() { tvk::Allocator::Get().DestroyBuffer(old); });
    chunk_uniform_gpu_buf_ = tvk::Allocator::Get().CreateBuffer(chunk_uniform_gpu_buf_.size * 1.5,
                                                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                VMA_MEMORY_USAGE_GPU_ONLY);
  }

  chunk_quad_buffer_.CopyDrawsToStaging();

  // renderer_->ImmediateSubmit(
  //     [this](VkCommandBuffer cmd) { chunk_quad_buffer_.CopyDrawsStagingToGPU(cmd); });
}

uint32_t ChunkMeshManager::CopyChunkToStaging(uint64_t* data, uint32_t cnt) {
  return chunk_quad_buffer_.vertex_staging.Copy(data, cnt);
}
