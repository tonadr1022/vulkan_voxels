#include "Barrier.hpp"

#include <vulkan/vulkan_core.h>

namespace tvk {

namespace {

void PipelineBarrierInternal(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                             std::span<VkBufferMemoryBarrier2> buffer_barriers,
                             std::span<VkImageMemoryBarrier2> img_barriers) {
  VkDependencyInfo dep_info{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .pNext = nullptr,
      .dependencyFlags = dep_flags,
      .memoryBarrierCount = 0,
      .pMemoryBarriers = nullptr,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(buffer_barriers.size()),
      .pBufferMemoryBarriers = buffer_barriers.data(),
      .imageMemoryBarrierCount = static_cast<uint32_t>(img_barriers.size()),
      .pImageMemoryBarriers = img_barriers.data()};
  vkCmdPipelineBarrier2(cmd, &dep_info);
}
}  // namespace

void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     std::span<VkBufferMemoryBarrier2> buffer_barriers,
                     std::span<VkImageMemoryBarrier2> img_barriers) {
  PipelineBarrierInternal(cmd, dep_flags, buffer_barriers, img_barriers);
}

void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkImageMemoryBarrier2 img_barrier) {
  std::span<VkImageMemoryBarrier2> barrier(&img_barrier, 1);
  PipelineBarrierInternal(cmd, dep_flags, {}, barrier);
}

void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkBufferMemoryBarrier2 img_barrier) {
  std::span<VkBufferMemoryBarrier2> barrier(&img_barrier, 1);
  PipelineBarrierInternal(cmd, dep_flags, barrier, {});
}

}  // namespace tvk
