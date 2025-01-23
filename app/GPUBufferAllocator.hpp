#pragma once

#include <vulkan/vulkan_core.h>

#include <span>
#include <tracy/Tracy.hpp>

#include "DeletionQueue.hpp"
#include "Resource.hpp"
#include "RingBuffer.hpp"
#include "Types.hpp"

struct NoneT {};

template <typename UT = NoneT>
struct Allocation {
  uint32_t handle{0};
  uint32_t offset{0};
  uint32_t size_bytes{0};
  uint32_t pad;
  UT user_data;
  explicit Allocation(UT data = {}) : user_data(data) {}
};

template <>
struct Allocation<void> {
  Allocation() = default;
  uint32_t handle{0};
  uint32_t offset{0};
  uint32_t size_bytes{0};
  uint32_t pad;
};
/*
 * glBuffer allocator. Can allocate and free blocks. The Allocation struct contains metadata for
 * each block, including user defined data via templating. The current primary use of templating
 * user data is to attach data to a vertex buffer for GPU compute culling.
 */
template <typename UserT = NoneT>
class DynamicBuffer {
 public:
  DynamicBuffer() = default;

  void PrintHandles(bool skip_zeros = false, bool print = true) {
    int c = 0;
    int b = 0;
    std::vector<uint32_t> s;
    for (auto i = 0u; i < MaxSeenActiveAllocs(); i++) {
      auto handle = allocs_[i].handle;
      if (skip_zeros && !handle) continue;
      if (print) fmt::print("{} ", handle);
      if (handle) {
        c++;
        if (i < allocs_.size()) {
          b++;
        } else {
          s.emplace_back(handle);
        }
      }
    }
    fmt::println("c {}, allocs_.size() {}", c, b);
    for (auto h : s) {
      fmt::print("{} ", h);
    }
    fmt::println("done");
  }

  void Init(uint32_t size_bytes, uint32_t alignment, uint32_t element_reserve_count) {
    free_handles_.reserve(element_reserve_count);
    allocs_.reserve(element_reserve_count);
    // memset(allocs_.data(), 0, sizeof(Allocation<UserT>) * allocs_.capacity());
    alignment_ = alignment;
    // align the size
    size_bytes += (alignment_ - (size_bytes % alignment_)) % alignment_;

    // create one large free block
    Allocation<UserT> empty_alloc{};
    empty_alloc.size_bytes = size_bytes;
    empty_alloc.offset = 0;
    empty_alloc.handle = 0;
    allocs_.push_back(empty_alloc);
  }

  DynamicBuffer(DynamicBuffer& other) = delete;
  DynamicBuffer& operator=(DynamicBuffer& other) = delete;
  DynamicBuffer(DynamicBuffer&& other) noexcept {
    if (&other == this) return *this;
    allocs_ = std::move(other.allocs_);
    alignment_ = other.alignment_;
    next_handle_ = other.next_handle_;
    num_active_allocs_ = other.num_active_allocs_;
    return *this;
  }

  DynamicBuffer& operator=(DynamicBuffer&& other) noexcept { *this = std::move(other); }

  [[nodiscard]] constexpr uint32_t AllocSize() const { return sizeof(Allocation<UserT>); }
  [[nodiscard]] const auto& Allocs() const { return allocs_; }
  [[nodiscard]] auto& Allocs() { return allocs_; }

  // Updates the offset parameter
  [[nodiscard]] uint32_t Allocate(uint32_t size_bytes, uint32_t& offset, UserT user_data = {}) {
    ZoneScoped;
    // align the size
    size_bytes += (alignment_ - (size_bytes % alignment_)) % alignment_;
    auto smallest_free_alloc = allocs_.end();
    {
      ZoneScopedN("smallest free alloc");
      // find the smallest free allocation that is large enough
      for (auto it = allocs_.begin(); it != allocs_.end(); it++) {
        // adequate if free and size fits
        if (it->handle == 0 && it->size_bytes >= size_bytes) {
          // if it's the first or it's smaller, set it to the new smallest free alloc
          if (smallest_free_alloc == allocs_.end() ||
              it->size_bytes < smallest_free_alloc->size_bytes) {
            smallest_free_alloc = it;
          }
        }
      }
      // if there isn't an allocation small enough, return 0, null handle
      if (smallest_free_alloc == allocs_.end()) {
        EASSERT(0 && "no space");
        return 0;
      }
    }

    // create new allocation
    Allocation<UserT> new_alloc{user_data};
    new_alloc.handle = GetHandle();
    new_alloc.offset = smallest_free_alloc->offset;
    new_alloc.size_bytes = size_bytes;

    // update free allocation
    smallest_free_alloc->size_bytes -= size_bytes;
    smallest_free_alloc->offset += size_bytes;

    // if smallest free alloc is now empty, replace it, otherwise insert it
    if (smallest_free_alloc->size_bytes == 0) {
      *smallest_free_alloc = new_alloc;
    } else {
      ZoneScopedN("Insert");
      allocs_.insert(smallest_free_alloc, new_alloc);
    }

    ++num_active_allocs_;
    max_seen_active_allocs_ = std::max<uint32_t>(max_seen_active_allocs_, allocs_.size());
    offset = new_alloc.offset;
    return new_alloc.handle;
  }

  // returns number of bytes freed
  uint32_t Free(uint32_t handle) {
    ZoneScoped;
    EASSERT(handle != 0);
    if (handle == 0) return 0;
    auto it = allocs_.end();
    for (it = allocs_.begin(); it != allocs_.end(); it++) {
      if (it->handle == handle) break;
    }
    if (it == allocs_.end()) {
      fmt::println("alloc not found {}", handle);
      EASSERT(0 && "Alloc not found");
      return 0;
    }

    it->handle = 0;
    uint32_t ret = it->size_bytes;
    Coalesce(it);

    free_handles_.emplace_back(handle);
    --num_active_allocs_;
    return ret;
  }

  [[nodiscard]] uint32_t NumActiveAllocs() const { return num_active_allocs_; }
  [[nodiscard]] uint32_t MaxSeenActiveAllocs() const { return max_seen_active_allocs_; }

 private:
  uint32_t alignment_{0};
  uint32_t next_handle_{1};
  uint32_t num_active_allocs_{0};
  uint32_t max_seen_active_allocs_{0};

  std::vector<uint32_t> free_handles_;
  uint32_t GetHandle() {
    if (!free_handles_.empty()) {
      auto h = free_handles_.back();
      free_handles_.pop_back();
      EASSERT(h);
      return h;
    }
    return next_handle_++;
  }
  std::vector<Allocation<UserT>> allocs_;

  using Iterator = decltype(allocs_.begin());
  void Coalesce(Iterator& it) {
    ZoneScoped;
    EASSERT_MSG(it != allocs_.end(), "Don't coalesce a non-existent allocation");

    bool remove_it = false;
    bool remove_next = false;

    // merge with next alloc
    if (it != allocs_.end() - 1) {
      auto next = it + 1;
      if (next->handle == 0) {
        it->size_bytes += next->size_bytes;
        remove_next = true;
      }
    }

    // merge with previous alloc
    if (it != allocs_.begin()) {
      auto prev = it - 1;
      if (prev->handle == 0) {
        prev->size_bytes += it->size_bytes;
        remove_it = true;
      }
    }

    // erase merged allocations
    if (remove_it && remove_next) {
      allocs_.erase(it, it + 2);  // curr and next
    } else if (remove_it) {
      allocs_.erase(it);  // only curr
    } else if (remove_next) {
      allocs_.erase(it + 1);  // only next
    }
  }
};

struct TSVertexUploadRingBuffer {
 private:
  struct Block {
    size_t offset;
    size_t size;
  };

 public:
  void Init(size_t size) {
    staging_ = tvk::Allocator::Get().CreateStagingBuffer(size);
    ring_buf_.Init(size);
  }

  template <typename T>
  [[nodiscard]] uint32_t Copy(const T* data, uint32_t size_bytes) {
    ZoneScoped;
    EASSERT(data && size_bytes > 0);
    size_t offset;
    size_t alloc_size_bytes = size_bytes;
    {
      ZoneScopedN("acquire offset in copy");
      std::lock_guard<std::mutex> lock(mtx);
      offset = ring_buf_.Allocate(alloc_size_bytes);
    }
    auto* start = reinterpret_cast<unsigned char*>(staging_.data);
    memcpy(start + offset, data, alloc_size_bytes);

    uint32_t copy_idx;
    {
      ZoneScopedN("get copy index lock");
      std::lock_guard<std::mutex> lock(mtx);
      if (free_copy_indices_.size()) {
        copy_idx = free_copy_indices_.back();
        free_copy_indices_.pop_back();
      } else {
        copy_idx = copies_.size();
        copies_.emplace_back();
      }
    }

    copies_[copy_idx] = Block{.offset = offset, .size = alloc_size_bytes};
    return copy_idx;
  }

  // worker threads add copies to the staging buffer
  // staging buffer is flushed to the GPU every frame
  void GetBlock(uint32_t copy_idx, size_t& offset, size_t& size) {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(mtx);
    EASSERT(copy_idx < copies_.size());
    offset = copies_[copy_idx].offset;
    size = copies_[copy_idx].size;
  }

  void Reset() {
    std::lock_guard<std::mutex> lock(mtx);
    free_copy_indices_.reserve(free_copy_indices_.size() + in_use_.size());
    free_copy_indices_.insert(free_copy_indices_.end(), in_use_.begin(), in_use_.end());
    in_use_.clear();
  }

  const tvk::AllocatedBuffer& Staging() { return staging_; }

  void Destroy() {
    std::lock_guard<std::mutex> lock(mtx);
    tvk::Allocator::Get().DestroyBuffer(staging_);
  }

  void AddInUseCopy(uint32_t copy) {
    std::lock_guard<std::mutex> lock(mtx);
    in_use_.emplace_back(copy);
  }

  std::mutex mtx;

 private:
  std::vector<uint32_t> in_use_;
  NonOwningRingBuffer ring_buf_;
  tvk::AllocatedBuffer staging_;
  std::vector<uint32_t> free_copy_indices_;
  std::vector<Block> copies_;
};

template <typename UserT = NoneT>
struct VertexPool {
  DynamicBuffer<UserT> draw_cmd_allocator;
  tvk::AllocatedBuffer quad_gpu_buf{};
  tvk::AllocatedBuffer draw_infos_staging{};
  tvk::AllocatedBuffer draw_infos_gpu_buf{};
  tvk::AllocatedBuffer draw_cmd_gpu_buf{};
  tvk::AllocatedBuffer draw_count_buffer{};
  size_t draw_cmds_count{};
  TSVertexUploadRingBuffer vertex_staging;
  // TSVertexUploadRingBuffer<uint8_t> vertex_staging;
  std::vector<VkBufferCopy> copies;
  size_t curr_copies_tot_size_bytes{};
  // TODO: refactor

 private:
  std::mutex mtx_;
  uint32_t curr_draw_info_copy_size_{};
  bool draws_dirty_{false};

 public:
  [[nodiscard]] bool DrawsDirty() const { return draws_dirty_; }

  void CopyDrawsToStaging() {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(mtx_);
    auto& allocs = draw_cmd_allocator.Allocs();
    auto active_size_bytes = sizeof(Allocation<UserT>) * draw_cmd_allocator.Allocs().size();
    curr_draw_info_copy_size_ = active_size_bytes;
    memcpy(draw_infos_staging.data, allocs.data(), active_size_bytes);
    if (draw_cmd_allocator.MaxSeenActiveAllocs() > draw_cmd_allocator.Allocs().size()) {
      auto empty_space_size =
          sizeof(Allocation<UserT>) *
          (draw_cmd_allocator.MaxSeenActiveAllocs() - draw_cmd_allocator.Allocs().size());
      EASSERT(empty_space_size + active_size_bytes <= draw_infos_staging.size);
      memset(static_cast<uint8_t*>(draw_infos_staging.data) + active_size_bytes, 0,
             empty_space_size);
      curr_draw_info_copy_size_ += empty_space_size;
    }
  }

  void CopyDrawsStagingToGPU(VkCommandBuffer cmd) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (curr_draw_info_copy_size_) {
      VkBufferCopy copy{0, 0, curr_draw_info_copy_size_};
      vkCmdCopyBuffer(cmd, draw_infos_staging.buffer, draw_infos_gpu_buf.buffer, 1, &copy);
      // auto* data = reinterpret_cast<Allocation<UserT>*>(draw_infos_staging.data);
      // int c = 0;
      // for (size_t i = 0; i < draw_cmd_allocator.MaxSeenActiveAllocs(); i++) {
      //   if (data[i].handle != 0) {
      //     c++;
      //   }
      // }
      // fmt::println("ggg {}, {}", curr_draw_info_copy_size_ / sizeof(Allocation<UserT>), c);
      // draw_cmd_allocator.PrintHandles(false, false);
    }
  }

  void ResizeBuffers(tvk::DeletionQueue& del_queue) {
    // TODO: this can be much cleaner
    std::lock_guard<std::mutex> lock(mtx_);
    EASSERT(draw_infos_staging.size && draw_infos_gpu_buf.size);
    EASSERT(draw_infos_staging.size == draw_infos_gpu_buf.size);
    if (draw_cmd_allocator.MaxSeenActiveAllocs() >
        draw_infos_staging.size / sizeof(Allocation<UserT>)) {
      auto old_size = draw_infos_staging.size;
      auto old = draw_infos_staging;
      auto old_gpu = draw_infos_gpu_buf;
      del_queue.PushFunc([old, old_gpu]() {
        tvk::Allocator::Get().DestroyBuffer(old_gpu);
        tvk::Allocator::Get().DestroyBuffer(old);
      });
      auto new_size = old_size * 1.5;
      draw_infos_gpu_buf = tvk::Allocator::Get().CreateBuffer(
          new_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          VMA_MEMORY_USAGE_GPU_ONLY);
      draw_infos_staging = tvk::Allocator::Get().CreateStagingBuffer(new_size);
    }
    if (draw_cmd_allocator.MaxSeenActiveAllocs() >
        draw_infos_gpu_buf.size / sizeof(VkDrawIndexedIndirectCommand)) {
      auto old = draw_infos_gpu_buf;
      del_queue.PushFunc([old]() { tvk::Allocator::Get().DestroyBuffer(old); });
      auto new_size = draw_infos_gpu_buf.size * 1.5;
      draw_cmd_gpu_buf = tvk::Allocator::Get().CreateBuffer(
          new_size * sizeof(VkDrawIndexedIndirectCommand),
          VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          VMA_MEMORY_USAGE_GPU_ONLY);
    }
  }

  void Destroy() {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& alloc = tvk::Allocator::Get();
    alloc.DestroyBuffer(quad_gpu_buf);
    alloc.DestroyBuffer(draw_infos_gpu_buf);
    alloc.DestroyBuffer(draw_infos_staging);
    alloc.DestroyBuffer(draw_count_buffer);
    alloc.DestroyBuffer(draw_cmd_gpu_buf);
    vertex_staging.Destroy();
  }

  void Init(uint32_t size_bytes, uint32_t alignment, VkBufferUsageFlagBits device_buffer_usage,
            uint32_t init_max_draw_cmds) {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(mtx_);
    draw_cmd_allocator.Init(size_bytes, alignment, 10000);

    quad_gpu_buf = tvk::Allocator::Get().CreateBuffer(
        size_bytes, device_buffer_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    size_t draw_info_initial_size = sizeof(Allocation<UserT>) * init_max_draw_cmds;
    draw_infos_gpu_buf = tvk::Allocator::Get().CreateBuffer(
        draw_info_initial_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    draw_infos_staging = tvk::Allocator::Get().CreateStagingBuffer(draw_info_initial_size);
    draw_count_buffer = tvk::Allocator::Get().CreateBuffer(sizeof(uint32_t),
                                                           VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                           VMA_MEMORY_USAGE_GPU_ONLY);

    draw_cmd_gpu_buf = tvk::Allocator::Get().CreateBuffer(
        init_max_draw_cmds * sizeof(VkDrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    vertex_staging.Init(sizeof(uint64_t) * 100 * 10000);
  }

  uint32_t FreeMeshes(std::span<uint32_t> handles) {
    std::lock_guard<std::mutex> lock(mtx_);
    EASSERT(draw_cmds_count >= handles.size());
    draws_dirty_ = true;
    draw_cmds_count -= handles.size();
    uint32_t freed_size = 0;
    for (auto h : handles) {
      freed_size += draw_cmd_allocator.Free(h);
    }
    return freed_size;
  }
  // returns num bytes freed
  uint32_t FreeMesh(uint32_t handle) {
    std::lock_guard<std::mutex> lock(mtx_);
    draws_dirty_ = true;
    EASSERT(draw_cmds_count > 0);
    draw_cmds_count--;
    return draw_cmd_allocator.Free(handle);
  }

  uint32_t AddMesh(size_t copy_idx, UserT user_data) {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(mtx_);
    VkBufferCopy copy;
    vertex_staging.AddInUseCopy(copy_idx);
    draws_dirty_ = true;
    vertex_staging.GetBlock(copy_idx, copy.srcOffset, copy.size);
    uint32_t dst_offset;
    auto handle = draw_cmd_allocator.Allocate(copy.size, dst_offset, user_data);

    copy.dstOffset = dst_offset;
    copies.emplace_back(copy);
    draw_cmds_count++;
    return handle;
  }

  void ExecuteCopy(VkCommandBuffer cmd) {
    ZoneScoped;
    std::lock_guard<std::mutex> lock(mtx_);
    draws_dirty_ = false;
    vkCmdCopyBuffer(cmd, vertex_staging.Staging().buffer, quad_gpu_buf.buffer, copies.size(),
                    copies.data());
    copies.clear();
    vertex_staging.Reset();
  }

  [[nodiscard]] size_t CurrCopyOperationSize() const { return curr_copies_tot_size_bytes; }
};
