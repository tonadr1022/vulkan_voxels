#include "ChunkMeshManager.hpp"

#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <tracy/TracyVulkan.hpp>

#include "Error.hpp"
#include "GPUBufferAllocator.hpp"
#include "Resource.hpp"
#include "VoxelRenderer.hpp"
#include "application/Renderer.hpp"
#include "imgui.h"
#include "voxels/Common.hpp"

#define SINGLE_TRIANGLE_QUAD
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

#ifdef SINGLE_TRIANGLE_QUAD
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
#ifndef SINGLE_TRIANGLE_QUAD
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
  quad_count_ -= chunk_quad_buffer_.FreeMeshes(handles) / sizeof(QuadSize);
}

void ChunkMeshManager::UploadChunkMeshes(std::span<ChunkMeshUpload> uploads,
                                         std::vector<ChunkAllocHandle>& handles) {
  ZoneScoped;
  for (const auto& [pos, mult, counts, staging_copy_idx, stale] : uploads) {
    ChunkDrawUniformData d{};
    // int mult = *CVarSystem::Get().GetIntCVar("chunks.chunk_mult");
    d.position = ivec4(pos, mult << 3);
    // d.position = ivec4(pos, mult << 3);
    int quad_cnt{};
    for (int i = 0; i < 6; i++) {
      quad_cnt += counts[i];
      d.vertex_counts[i] = counts[i];
    }
    EASSERT(quad_cnt);
    ChunkAllocHandle handle = chunk_quad_buffer_.AddMesh(staging_copy_idx, d, stale);
    if (!stale) {
      handles.emplace_back(handle);
      quad_count_ += quad_cnt;
    }
  }
  // {
  //   ZoneScopedN("copy to gpu");
  //   if (chunk_quad_buffer_.copies.size()) {
  //     renderer_->ImmediateSubmit(
  //         [this](VkCommandBuffer cmd) { chunk_quad_buffer_.ExecuteCopy(cmd); });
  //   }
  // }
}

ChunkMeshManager& ChunkMeshManager::Get() {
  static ChunkMeshManager instance;
  return instance;
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

uint32_t ChunkMeshManager::CopyChunkToStaging(const uint8_t* data, uint32_t quad_cnt) {
#ifndef PACK_QUAD
  EASSERT(0);
#endif
  return chunk_quad_buffer_.vertex_staging.Copy(data, quad_cnt * QuadSize);
}

uint32_t ChunkMeshManager::CopyChunkToStaging(const uint64_t* data, uint32_t quad_cnt) {
#ifdef PACK_QUAD
  EASSERT(0);
#endif
  return chunk_quad_buffer_.vertex_staging.Copy(data, quad_cnt * QuadSize);
}
