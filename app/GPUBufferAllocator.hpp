#pragma once

#include <vulkan/vulkan_core.h>

#include "DeletionQueue.hpp"
#include "Resource.hpp"
#include "Types.hpp"

struct NoneT {};

template <typename UT = NoneT>
struct Allocation {
  uint64_t handle{0};
  uint32_t offset{0};
  uint32_t size_bytes{0};
  UT user_data;
  explicit Allocation(UT data = {}) : user_data(data) {}
};

template <>
struct Allocation<void> {
  Allocation() = default;
  uint64_t handle{0};
  uint32_t offset{0};
  uint32_t size_bytes{0};
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

  void Init(uint32_t size_bytes, uint32_t alignment) {
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
        assert(0 && "no space");
        return 0;
      }
    }

    // create new allocation
    Allocation<UserT> new_alloc{user_data};
    new_alloc.handle = next_handle_++;
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
    offset = new_alloc.offset;
    return new_alloc.handle;
  }

  void Free(uint32_t handle) {
    ZoneScoped;
    if (handle == 0) return;
    auto it = allocs_.end();
    for (it = allocs_.begin(); it != allocs_.end(); it++) {
      if (it->handle == handle) break;
    }
    if (it == allocs_.end()) {
      return;
    }

    it->handle = 0;
    Coalesce(it);

    --num_active_allocs_;
  }

  [[nodiscard]] uint32_t NumActiveAllocs() const { return num_active_allocs_; }

 private:
  uint32_t alignment_{0};
  uint64_t next_handle_{1};
  uint32_t num_active_allocs_{0};

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

template <typename UserT = NoneT>
struct VertexPool {
  DynamicBuffer<UserT> draw_cmd_allocator;
  tvk::AllocatedBuffer quad_gpu_buf{};
  tvk::AllocatedBuffer draw_infos_staging{};
  tvk::AllocatedBuffer draw_infos_gpu_buf{};
  tvk::AllocatedBuffer draw_cmd_gpu_buf{};
  tvk::AllocatedBuffer draw_count_buffer{};
  uint32_t draw_cmds_count{};

  void CopyDrawsToStaging() {
    auto& allocs = draw_cmd_allocator.Allocs();
    memcpy(draw_infos_staging.data, allocs.data(), sizeof(Allocation<UserT>) * allocs.size());
  }

  void CopyDrawsStagingToGPU(VkCommandBuffer cmd) {
    VkBufferCopy copy{};
    copy.size = draw_infos_staging.size;
    vkCmdCopyBuffer(cmd, draw_infos_staging.buffer, draw_infos_gpu_buf.buffer, 1, &copy);
  }

  void ResizeBuffers(tvk::DeletionQueue& del_queue) {
    // TODO: this can be much cleaner
    EASSERT(draw_infos_staging.size && draw_infos_gpu_buf.size);
    EASSERT(draw_infos_staging.size == draw_infos_gpu_buf.size);
    if (draw_cmd_allocator.Allocs().size() > draw_infos_staging.size / sizeof(Allocation<UserT>)) {
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
    if (draw_cmd_allocator.Allocs().size() >
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
    auto& alloc = tvk::Allocator::Get();
    alloc.DestroyBuffer(quad_gpu_buf);
    alloc.DestroyBuffer(draw_infos_gpu_buf);
    alloc.DestroyBuffer(draw_infos_staging);
    alloc.DestroyBuffer(draw_count_buffer);
    alloc.DestroyBuffer(draw_cmd_gpu_buf);
  }

  void Init(uint32_t size_bytes, uint32_t alignment, VkBufferUsageFlagBits device_buffer_usage,
            uint32_t init_max_draw_cmds) {
    ZoneScoped;
    draw_cmd_allocator.Init(size_bytes, alignment);

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
  }

  void FreeMesh(uint32_t handle) {
    draw_cmds_count--;
    draw_cmd_allocator.Free(handle);
  }

  // TODO: for chunks specifically, can group the copies since the mesh from the mesher has all 6
  // faces combined contiguously
  uint32_t AddMesh(uint32_t size_bytes, void* data, uint32_t& offset, UserT user_data = {}) {
    ZoneScoped;
    auto handle = draw_cmd_allocator.Allocate(size_bytes, offset, user_data);
    copies.emplace_back(curr_copies_tot_size_bytes, offset, size_bytes);
    copy_data_ptrs.emplace_back(data);
    curr_copies_tot_size_bytes += size_bytes;
    draw_cmds_count++;
    return handle;
  }

  void CopyToStaging(tvk::AllocatedBuffer& staging) {
    ZoneScoped;
    assert(copies.size() == copy_data_ptrs.size());
    size_t staging_offset{0};
    for (size_t i = 0; i < copies.size(); i++) {
      memcpy(reinterpret_cast<uint8_t*>(staging.data) + staging_offset, copy_data_ptrs[i],
             copies[i].size);
      staging_offset += copies[i].size;
    }
  }

  void ExecuteCopy(tvk::AllocatedBuffer& staging, VkCommandBuffer cmd) {
    ZoneScoped;
    vkCmdCopyBuffer(cmd, staging.buffer, quad_gpu_buf.buffer, copies.size(), copies.data());
    copies.clear();
    copy_data_ptrs.clear();
    curr_copies_tot_size_bytes = 0;
  }

  [[nodiscard]] size_t CurrCopyOperationSize() const { return curr_copies_tot_size_bytes; }

  std::vector<VkBufferCopy> copies;
  size_t curr_copies_tot_size_bytes{};
  std::vector<void*> copy_data_ptrs;
};
