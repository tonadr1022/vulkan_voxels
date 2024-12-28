#pragma once

#include "Types.hpp"
namespace tvk::init {
VkVertexInputAttributeDescription VertexInputAttributeDescription(uint32_t location,
                                                                  uint32_t binding, VkFormat format,
                                                                  uint32_t offset);

VkVertexInputBindingDescription VertexInputBindingDescription(uint32_t binding, uint32_t stride,
                                                              VkVertexInputRate input_rate);

VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo(
    std::span<VkVertexInputBindingDescription> vertex_binding_descriptions,
    std::span<VkVertexInputAttributeDescription> vertex_attribute_descriptions);

VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queue_family_index,
                                              VkCommandPoolCreateFlags flags = 0);

VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1);

VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags = 0);

VkSemaphoreCreateInfo SemaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);

VkCommandBufferBeginInfo CommandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0);

VkImageSubresourceRange ImageSubresourceRange(VkImageAspectFlags aspect_mask = 0);

VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore);

VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd);

VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd);
VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd,
                         VkSemaphoreSubmitInfo* signal_semaphore_info,
                         VkSemaphoreSubmitInfo* wait_semaphore_info);

VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usage_flags, VkExtent3D extent,
                                  uint32_t mip_levels = 1, uint32_t array_layers = 1,
                                  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);

VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image,
                                          VkImageAspectFlags aspect_flags, uint32_t mip_levels = 1,
                                          uint32_t array_layers = 1);

VkRenderingAttachmentInfo DepthAttachmentInfo(
    VkImageView view, VkImageLayout layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
VkRenderingAttachmentInfo AttachmentInfo(
    VkImageView view, VkClearValue* clear = nullptr,
    VkImageLayout layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

VkRenderingInfo RenderingInfo(VkExtent2D render_extent, VkRenderingAttachmentInfo* color_attachment,
                              VkRenderingAttachmentInfo* depth_attachment = nullptr,
                              VkRenderingAttachmentInfo* stencil_attachment = nullptr);
VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
                                                              VkShaderModule shader_module);
VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo();

VkImageMemoryBarrier2 ImageBarrierUpdate(VkImage img, ImagePipelineState& src_state,
                                         ImagePipelineState& dst_state,
                                         VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                                         uint32_t base_mip_level = 0,
                                         uint32_t level_count = VK_REMAINING_MIP_LEVELS);
VkImageMemoryBarrier2 ImageBarrier(VkImage img, const ImagePipelineState& src_state,
                                   const ImagePipelineState& dst_state,
                                   VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   uint32_t base_mip_level = 0,
                                   uint32_t level_count = VK_REMAINING_MIP_LEVELS);

VkImageMemoryBarrier2 ImageBarrier(VkImage img, VkImageLayout curr_layout,
                                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                   VkImageLayout new_layout, VkPipelineStageFlags2 dst_stage,
                                   VkAccessFlags2 dst_access,
                                   VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   uint32_t base_mip_level = 0,
                                   uint32_t level_count = VK_REMAINING_MIP_LEVELS);
VkBufferMemoryBarrier2 BufferBarrier(VkBuffer buffer, uint32_t queue_idx,
                                     VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                     VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                                     VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
VkBufferMemoryBarrier2 BufferBarrier(VkBuffer buffer, uint32_t queue_idx);
VkImageMemoryBarrier2 ImageMemoryBarrier(VkImageLayout curr_layout, VkImageLayout new_layout,
                                         VkPipelineStageFlagBits2 src_stage,
                                         VkPipelineStageFlagBits2 dst_stage,
                                         VkAccessFlags2 src_access, VkAccessFlags2 dst_access);

VkSamplerCreateInfo SamplerCreateInfo(VkFilter filter, VkSamplerAddressMode address_mode);

}  // namespace tvk::init
