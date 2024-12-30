#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

namespace tvk {

struct AllocatedBuffer {
  VkBuffer buffer{VK_NULL_HANDLE};
  size_t size{0};
  VmaAllocation allocation{};
  void* data{};
  [[nodiscard]] VkDescriptorBufferInfo GetInfo(VkDeviceSize offset = 0) const {
    assert(buffer != VK_NULL_HANDLE);
    return {.buffer = buffer, .offset = offset, .range = size};
  }
};

struct AllocatedImage {
  VkImage image;
  VkImageView view;
  VmaAllocation allocation;
  VkExtent3D extent;
  VkFormat format;
  [[nodiscard]] VkExtent2D Extent2D() const;
};

struct Allocator {
  void Init(VkDevice device, VmaAllocator allocator);
  [[nodiscard]] AllocatedBuffer CreateBuffer(size_t alloc_size, VkBufferUsageFlags usage,
                                             VmaMemoryUsage memory_usage,
                                             VmaAllocationCreateFlags flags = 0,
                                             VkMemoryPropertyFlags req_mem_flags = 0) const;
  [[nodiscard]] AllocatedBuffer CreateStagingBuffer(
      size_t size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_CPU_ONLY,
      VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      VkMemoryPropertyFlags req_mem_flags = 0) const;
  AllocatedBuffer CreateStagingBuffer(
      void* data, size_t size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_CPU_ONLY,
      VmaAllocationCreateFlags flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
      VkMemoryPropertyFlags req_mem_flags = 0) const;
  void DestroyBuffer(const AllocatedBuffer& buffer) const;
  void DestroyImageAndView(const AllocatedImage& image) const;
  void DestroyImage() const;

  [[nodiscard]] VmaAllocator GetAllocator() const { return allocator_; }
  void CreateImage(AllocatedImage& img, VkImageCreateInfo& create_info,
                   VmaAllocationCreateInfo& alloc_info) const;
  [[nodiscard]] AllocatedImage CreateImage2D(VkExtent2D size, VkFormat format,
                                             VkImageUsageFlags usage, bool mipmapped = false,
                                             bool make_view = true,
                                             VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                                             uint32_t array_layers = 1,
                                             VkMemoryPropertyFlags req_flags = 0) const;

 private:
  VkDevice device_;
  VmaAllocator allocator_;
};

void CopyBuffer(VkCommandBuffer cmd, AllocatedBuffer& src, AllocatedBuffer& dst, size_t size,
                size_t dst_offset = 0, size_t src_offset = 0);
uint32_t GetMipLevels(VkExtent2D size);

struct FencePool {
  void AddFreeFence(VkFence fence);
  VkFence GetFence();
  void Init(VkDevice device);
  void Cleanup();

 private:
  VkDevice device_;
  std::vector<VkFence> fences_;
};

}  // namespace tvk
