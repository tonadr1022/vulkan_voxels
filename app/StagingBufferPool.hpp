#pragma once

#include <list>
#include <span>

#include "Resource.hpp"
struct StagingBufferPool {
  explicit StagingBufferPool(tvk::Allocator& allocator);
  std::unique_ptr<tvk::AllocatedBuffer> GetBuffer(size_t size_bytes);
  tvk::AllocatedBuffer* GetBuffer2(size_t size_bytes);
  void ReturnBuffer(std::unique_ptr<tvk::AllocatedBuffer> buf);
  void Init(std::span<size_t> initial_buffer_sizes);
  void Init(std::initializer_list<size_t> initial_buffer_sizes);
  void Cleanup();
  std::list<std::unique_ptr<tvk::AllocatedBuffer>> free_staging_buffers;
  size_t in_use_cnt{0};

 private:
  tvk::Allocator& allocator_;
};
