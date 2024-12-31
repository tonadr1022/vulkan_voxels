#pragma once

#include <vulkan/vulkan_core.h>

#include "Resource.hpp"
struct NoneT {};

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

  template <typename UT = UserT>
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
struct GPUBufferAllocator {
  DynamicBuffer<UserT> buffer_allocator;
  tvk::AllocatedBuffer device_buffer{};

  void Destroy() { allocator->DestroyBuffer(device_buffer); }

  void Init(uint32_t size_bytes, uint32_t alignment, VkBufferUsageFlagBits device_buffer_usage,
            const tvk::Allocator* allocator) {
    ZoneScoped;
    this->allocator = allocator;
    buffer_allocator.Init(size_bytes, alignment);
    device_buffer =
        allocator->CreateBuffer(size_bytes, device_buffer_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_ONLY);
  }

  uint32_t AddCopy(uint32_t size_bytes, void* data, uint32_t& offset, UserT user_data = {}) {
    ZoneScoped;
    auto handle = buffer_allocator.Allocate(size_bytes, offset, user_data);
    copies.emplace_back(curr_copies_tot_size_bytes, offset, size_bytes);
    copy_data_ptrs.emplace_back(data);
    curr_copies_tot_size_bytes += size_bytes;
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
    vkCmdCopyBuffer(cmd, staging.buffer, device_buffer.buffer, copies.size(), copies.data());
    copies.clear();
    copy_data_ptrs.clear();
    curr_copies_tot_size_bytes = 0;
  }

  tvk::AllocatedBuffer CopyToStaging() {
    ZoneScoped;
    // TODO: pool this and multithread the copies
    tvk::AllocatedBuffer staging = allocator->CreateStagingBuffer(curr_copies_tot_size_bytes);
    CopyToStaging(staging);
    return staging;
  }
  [[nodiscard]] size_t CurrCopyOperationSize() const { return curr_copies_tot_size_bytes; }
  const tvk::Allocator* allocator;
  std::vector<VkBufferCopy> copies;
  size_t curr_copies_tot_size_bytes{};
  std::vector<void*> copy_data_ptrs;
};
