#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan_core.h>

namespace tvk {

struct PipelineAndLayout {
  VkPipeline pipeline{};
  VkPipelineLayout layout{};
  bool operator==(const PipelineAndLayout& other) const {
    return pipeline == other.pipeline && layout == other.layout;
  }
};

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

struct ImagePipelineState {
  VkImageLayout layout;
  VkPipelineStageFlags2 stage;
  VkAccessFlags2 access;
  bool operator==(const ImagePipelineState& other) const {
    return layout == other.layout && stage == other.stage && access == other.access;
  }
};
struct ImageAndState {
  ImagePipelineState curr_state;
  ImagePipelineState nxt_state;
  VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
  AllocatedImage& img;
};

}  // namespace tvk
