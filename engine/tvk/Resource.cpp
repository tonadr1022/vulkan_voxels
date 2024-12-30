#include "Resource.hpp"

#include "Error.hpp"
#include "Initializers.hpp"

namespace tvk {

AllocatedBuffer Allocator::CreateBuffer(size_t alloc_size, VkBufferUsageFlags usage,
                                        VmaMemoryUsage memory_usage, VmaAllocationCreateFlags flags,
                                        VkMemoryPropertyFlags req_mem_flags) const {
  ZoneScoped;
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.pNext = nullptr;
  buffer_info.size = alloc_size;
  buffer_info.usage = usage;
  VmaAllocationCreateInfo vma_alloc_info{};
  vma_alloc_info.usage = memory_usage;
  vma_alloc_info.flags = flags;
  vma_alloc_info.requiredFlags = req_mem_flags;

  VmaAllocationInfo info;
  AllocatedBuffer new_buffer;
  VK_CHECK(vmaCreateBuffer(allocator_, &buffer_info, &vma_alloc_info, &new_buffer.buffer,
                           &new_buffer.allocation, &info));
  new_buffer.data = info.pMappedData;
  new_buffer.size = alloc_size;
  return new_buffer;
}

AllocatedImage Allocator::CreateImage2D(VkExtent2D size, VkFormat format, VkImageUsageFlags usage,
                                        bool mipmapped, bool make_view,
                                        VkSampleCountFlagBits samples, uint32_t array_layers,
                                        VkMemoryPropertyFlags req_flags) const {
  AllocatedImage new_image{};
  new_image.format = format;
  new_image.extent = {size.width, size.height, 1};

  VkImageCreateInfo img_info = init::ImageCreateInfo(
      format, usage, new_image.extent, mipmapped ? GetMipLevels(size) : 1, array_layers, samples);
  // allocate image on dedicated gpu memory
  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_info.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | req_flags;
  VK_CHECK(vmaCreateImage(allocator_, &img_info, &alloc_info, &new_image.image,
                          &new_image.allocation, nullptr));

  // if format is depth use that flag
  VkImageAspectFlags aspect_flag = VK_IMAGE_ASPECT_COLOR_BIT;
  if (format == VK_FORMAT_D32_SFLOAT) {
    aspect_flag = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  if (make_view) {
    // create image view
    VkImageViewCreateInfo view_info = init::ImageViewCreateInfo(
        format, new_image.image, aspect_flag, img_info.mipLevels, img_info.arrayLayers);
    VK_CHECK(vkCreateImageView(device_, &view_info, nullptr, &new_image.view));
  }

  return new_image;
}

void Allocator::Init(VkDevice device, VmaAllocator allocator) {
  assert(device != VK_NULL_HANDLE);
  device_ = device;
  allocator_ = allocator;
}

uint32_t GetMipLevels(VkExtent2D size) {
  return static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
}

void Allocator::DestroyBuffer(const AllocatedBuffer& buffer) const {
  vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
}

void Allocator::DestroyImage() const {}
void Allocator::DestroyImageAndView(const AllocatedImage& image) const {
  vkDestroyImageView(device_, image.view, nullptr);
  vmaDestroyImage(allocator_, image.image, image.allocation);
}
void Allocator::CreateImage(AllocatedImage& img, VkImageCreateInfo& create_info,
                            VmaAllocationCreateInfo& alloc_info) const {
  VK_CHECK(
      vmaCreateImage(allocator_, &create_info, &alloc_info, &img.image, &img.allocation, nullptr));
}

AllocatedBuffer Allocator::CreateStagingBuffer(size_t size, VkBufferUsageFlags usage,
                                               VmaMemoryUsage memory_usage,
                                               VmaAllocationCreateFlags flags,
                                               VkMemoryPropertyFlags req_mem_flags) const {
  ZoneScoped;
  return CreateBuffer(size, usage, memory_usage, flags, req_mem_flags);
}

AllocatedBuffer Allocator::CreateStagingBuffer(void* data, size_t size, VkBufferUsageFlags usage,
                                               VmaMemoryUsage memory_usage,
                                               VmaAllocationCreateFlags flags,
                                               VkMemoryPropertyFlags req_mem_flags) const {
  AllocatedBuffer ret = CreateBuffer(size, usage, memory_usage, flags, req_mem_flags);
  memcpy(ret.data, data, size);
  return ret;
}

VkFence FencePool::GetFence() {
  if (!fences_.empty()) {
    VkFence ret = fences_.back();
    fences_.pop_back();
    return ret;
  }
  VkFence fence;
  auto info = init::FenceCreateInfo();
  VK_CHECK(vkCreateFence(device_, &info, nullptr, &fence));
  return fence;
}

void FencePool::AddFreeFence(VkFence fence) { fences_.emplace_back(fence); }

void FencePool::Init(VkDevice device) { device_ = device; }

void FencePool::Cleanup() {
  for (VkFence fence : fences_) {
    vkDestroyFence(device_, fence, nullptr);
  }
}
VkExtent2D AllocatedImage::Extent2D() const { return {extent.width, extent.height}; }
void CopyBuffer(VkCommandBuffer cmd, AllocatedBuffer& src, AllocatedBuffer& dst, size_t size,
                size_t dst_offset, size_t src_offset) {
  VkBufferCopy vertex_copy{};
  vertex_copy.dstOffset = dst_offset;
  vertex_copy.srcOffset = src_offset;
  vertex_copy.size = size;
  vkCmdCopyBuffer(cmd, src.buffer, dst.buffer, 1, &vertex_copy);
}
}  // namespace tvk
