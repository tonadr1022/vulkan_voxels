#include "Initializers.hpp"

namespace tvk::init {

VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queue_family_index,
                                              VkCommandPoolCreateFlags flags) {
  // able to reset individual command buffers instead of just the entire pool
  return {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
          .pNext = nullptr,
          .flags = flags,
          .queueFamilyIndex = queue_family_index};
}

VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count) {
  return {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = nullptr,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = count,
  };
}

VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags) {
  return VkFenceCreateInfo{
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = flags};
}

VkSemaphoreCreateInfo SemaphoreCreateInfo(VkSemaphoreCreateFlags flags) {
  return {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = flags};
}

VkCommandBufferBeginInfo CommandBufferBeginInfo(VkCommandBufferUsageFlags flags) {
  return {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = nullptr,
      .flags = flags,
      .pInheritanceInfo = nullptr,
  };
}

VkImageSubresourceRange ImageSubresourceRange(VkImageAspectFlags aspect_mask) {
  return {.aspectMask = aspect_mask,
          .baseMipLevel = 0,
          .levelCount = VK_REMAINING_MIP_LEVELS,
          .baseArrayLayer = 0,
          .layerCount = VK_REMAINING_ARRAY_LAYERS};
}

VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore) {
  return {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .pNext = nullptr,
      .semaphore = semaphore,
      .value = 1,
      .stageMask = stage_mask,
      .deviceIndex = 0,
  };
}

VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd) {
  return {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
          .pNext = nullptr,
          .commandBuffer = cmd,
          .deviceMask = 0};
}

VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd) {
  return {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .pNext = nullptr,
      .flags = 0,
      .waitSemaphoreInfoCount = 0,
      .pWaitSemaphoreInfos = nullptr,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = cmd,
      .signalSemaphoreInfoCount = 0,
      .pSignalSemaphoreInfos = nullptr,
  };
}
VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd,
                         VkSemaphoreSubmitInfo* signal_semaphore_info,
                         VkSemaphoreSubmitInfo* wait_semaphore_info) {
  return {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .pNext = nullptr,
      .flags = 0,
      .waitSemaphoreInfoCount = static_cast<uint32_t>(wait_semaphore_info == nullptr ? 0 : 1),
      .pWaitSemaphoreInfos = wait_semaphore_info,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = cmd,
      .signalSemaphoreInfoCount = static_cast<uint32_t>(signal_semaphore_info == nullptr ? 0 : 1),
      .pSignalSemaphoreInfos = signal_semaphore_info,
  };
}

VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usage_flags, VkExtent3D extent,
                                  uint32_t mip_levels, uint32_t array_layers,
                                  VkSampleCountFlagBits samples) {
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.pNext = nullptr;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = format;
  info.extent = extent;
  info.mipLevels = mip_levels;
  info.arrayLayers = array_layers;
  info.samples = samples;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = usage_flags;
  return info;
}

VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image,
                                          VkImageAspectFlags aspect_flags, uint32_t mip_levels,
                                          uint32_t array_layers) {
  VkImageViewCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  info.pNext = nullptr;
  info.image = image;
  info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  info.format = format;
  info.subresourceRange.baseMipLevel = 0, info.subresourceRange.levelCount = mip_levels,
  info.subresourceRange.baseArrayLayer = 0, info.subresourceRange.layerCount = array_layers,
  info.subresourceRange.aspectMask = aspect_flags;
  return info;
}

VkRenderingAttachmentInfo DepthAttachmentInfo(VkImageView view, VkImageLayout layout) {
  VkRenderingAttachmentInfo attachment{};
  attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  attachment.pNext = nullptr;
  attachment.imageView = view;
  attachment.imageLayout = layout;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  // depth 0 is far value
  attachment.clearValue.depthStencil.depth = 0.f;
  return attachment;
}

VkRenderingAttachmentInfo AttachmentInfo(VkImageView view, VkClearValue* clear,
                                         VkImageLayout layout) {
  VkRenderingAttachmentInfo color_attachment{};
  color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  color_attachment.pNext = nullptr;
  color_attachment.imageView = view;
  color_attachment.imageLayout = layout;
  // controls what happens to the render target when used in a renderpass. LOAD keeps the data in
  // the image, clear sets it to the clear value at the start, dont_care replaces every pixel so the
  // GPU can skip loading from memory
  color_attachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  // store hardcoded since want draw commands to be saved
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  if (clear) {
    color_attachment.clearValue = *clear;
  }

  return color_attachment;
}
VkRenderingInfo RenderingInfo(VkExtent2D render_extent, VkRenderingAttachmentInfo* color_attachment,
                              VkRenderingAttachmentInfo* depth_attachment,
                              VkRenderingAttachmentInfo* stencil_attachment) {
  VkRenderingInfo render_info{};
  render_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
  render_info.pNext = nullptr;
  render_info.renderArea = VkRect2D{VkOffset2D{0, 0}, render_extent};
  render_info.layerCount = 1;
  render_info.colorAttachmentCount = color_attachment != nullptr ? 1 : 0;
  render_info.pColorAttachments = color_attachment;
  render_info.pDepthAttachment = depth_attachment;
  render_info.pStencilAttachment = stencil_attachment;
  return render_info;
}

VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
                                                              VkShaderModule shader_module) {
  VkPipelineShaderStageCreateInfo stage_info{};
  stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage_info.pNext = nullptr;
  stage_info.stage = stage;
  stage_info.module = shader_module;
  stage_info.pName = "main";
  return stage_info;
}
VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo() {
  VkPipelineLayoutCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  create_info.pNext = nullptr;
  return create_info;
}

VkVertexInputAttributeDescription VertexInputAttributeDescription(uint32_t location,
                                                                  uint32_t binding, VkFormat format,
                                                                  uint32_t offset) {
  return {.location = location, .binding = binding, .format = format, .offset = offset};
}

VkVertexInputBindingDescription VertexInputBindingDescription(uint32_t binding, uint32_t stride,
                                                              VkVertexInputRate input_rate) {
  return {.binding = binding, .stride = stride, .inputRate = input_rate};
}

VkPipelineVertexInputStateCreateInfo PipelineVertexInputStateCreateInfo(
    std::span<VkVertexInputBindingDescription> vertex_binding_descriptions,
    std::span<VkVertexInputAttributeDescription> vertex_attribute_descriptions) {
  VkPipelineVertexInputStateCreateInfo out{};
  out.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  out.vertexBindingDescriptionCount = vertex_binding_descriptions.size();
  out.pVertexBindingDescriptions = vertex_binding_descriptions.data();
  out.vertexAttributeDescriptionCount = vertex_attribute_descriptions.size();
  out.pVertexAttributeDescriptions = vertex_attribute_descriptions.data();
  return out;
}

VkBufferMemoryBarrier2 BufferBarrier(VkBuffer buffer, uint32_t queue_idx) {
  VkBufferMemoryBarrier2 ret{};
  ret.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
  ret.pNext = nullptr;
  ret.buffer = buffer;
  // same queue
  ret.srcQueueFamilyIndex = queue_idx;
  ret.dstQueueFamilyIndex = queue_idx;
  // whole buffer by default
  ret.size = VK_WHOLE_SIZE;
  return ret;
}

VkSamplerCreateInfo SamplerCreateInfo(VkFilter filter, VkSamplerAddressMode address_mode) {
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.minFilter = filter;
  sampler_info.magFilter = filter;
  sampler_info.addressModeU = address_mode;
  sampler_info.addressModeV = address_mode;
  sampler_info.addressModeW = address_mode;
  return sampler_info;
}
VkImageMemoryBarrier2 ImageMemoryBarrier(VkImageLayout curr_layout, VkImageLayout new_layout,
                                         VkPipelineStageFlagBits2 src_stage,
                                         VkPipelineStageFlagBits2 dst_stage,
                                         VkAccessFlags2 src_access, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 img_barrier{};
  img_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  img_barrier.pNext = nullptr;
  img_barrier.srcStageMask = src_stage;
  img_barrier.dstStageMask = dst_stage;
  img_barrier.srcAccessMask = src_access;
  img_barrier.dstAccessMask = dst_access;
  img_barrier.oldLayout = curr_layout;
  img_barrier.newLayout = new_layout;
  return img_barrier;
}

VkImageMemoryBarrier2 ImageBarrier(VkImage img, const ImagePipelineState& src_state,
                                   const ImagePipelineState& dst_state,
                                   VkImageAspectFlags aspect_mask, uint32_t base_mip_level,
                                   uint32_t level_count) {
  return ImageBarrier(img, src_state.layout, src_state.stage, src_state.access, dst_state.layout,
                      dst_state.stage, dst_state.access, aspect_mask, base_mip_level, level_count);
}
VkImageMemoryBarrier2 ImageBarrier(VkImage img, VkImageLayout curr_layout,
                                   VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                   VkImageLayout new_layout, VkPipelineStageFlags2 dst_stage,
                                   VkAccessFlags2 dst_access, VkImageAspectFlags aspect_mask,
                                   uint32_t base_mip_level, uint32_t level_count) {
  return VkImageMemoryBarrier2{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                               .pNext = nullptr,
                               .srcStageMask = src_stage,
                               .srcAccessMask = src_access,
                               .dstStageMask = dst_stage,
                               .dstAccessMask = dst_access,
                               .oldLayout = curr_layout,
                               .newLayout = new_layout,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = img,
                               .subresourceRange = {.aspectMask = aspect_mask,
                                                    .baseMipLevel = base_mip_level,
                                                    .levelCount = level_count,
                                                    .baseArrayLayer = 0,
                                                    .layerCount = VK_REMAINING_ARRAY_LAYERS}};
}
VkBufferMemoryBarrier2 BufferBarrier(VkBuffer buffer, uint32_t queue_idx,
                                     VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                                     VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                                     VkDeviceSize offset, VkDeviceSize size) {
  return VkBufferMemoryBarrier2{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                .pNext = nullptr,
                                .srcStageMask = src_stage,
                                .srcAccessMask = src_access,
                                .dstStageMask = dst_stage,
                                .dstAccessMask = dst_access,
                                .srcQueueFamilyIndex = queue_idx,
                                .dstQueueFamilyIndex = queue_idx,
                                .buffer = buffer,
                                .offset = offset,
                                .size = size};
}

VkImageMemoryBarrier2 ImageBarrierUpdate(VkImage img, ImagePipelineState& src_state,
                                         ImagePipelineState& dst_state,
                                         VkImageAspectFlags aspect_mask, uint32_t base_mip_level,
                                         uint32_t level_count) {
  auto barrier = ImageBarrier(img, src_state, dst_state, aspect_mask, base_mip_level, level_count);
  src_state = dst_state;
  return barrier;
}

}  // namespace tvk::init
