#include "StagingBufferPool.hpp"

std::unique_ptr<tvk::AllocatedBuffer> StagingBufferPool::GetBuffer(size_t size_bytes) {
  std::unique_ptr<tvk::AllocatedBuffer> buf;
  for (auto it = free_staging_buffers.begin(); it != free_staging_buffers.end(); it++) {
    if (it->get()->size >= size_bytes) {
      buf = std::move(*it);
      free_staging_buffers.erase(it);
      break;
    }
  }
  if (!buf) {
    buf = std::make_unique<tvk::AllocatedBuffer>(allocator_.CreateStagingBuffer(size_bytes));
  }
  in_use_cnt++;
  return buf;
}

void StagingBufferPool::ReturnBuffer(std::unique_ptr<tvk::AllocatedBuffer> buf) {
  in_use_cnt--;
  free_staging_buffers.emplace_back(std::move(buf));
}

void StagingBufferPool::Init(std::span<size_t>) {
  // for (const auto size : initial_buffer_sizes) {
  //   free_staging_buffers.emplace_back(
  //       std::make_unique<tvk::AllocatedBuffer>(allocator_.CreateStagingBuffer(size)));
  // }
}

void StagingBufferPool::Init(std::initializer_list<size_t>) {
  // for (const auto size : initial_buffer_sizes) {
  //   free_staging_buffers.emplace_back(
  //       std::make_unique<tvk::AllocatedBuffer>(allocator_.CreateStagingBuffer(size)));
  // }
}

StagingBufferPool::StagingBufferPool(tvk::Allocator& allocator) : allocator_(allocator) {}

void StagingBufferPool::Cleanup() {
  for (auto& b : free_staging_buffers) {
    allocator_.DestroyBuffer(*b);
  }
  free_staging_buffers.clear();
}
