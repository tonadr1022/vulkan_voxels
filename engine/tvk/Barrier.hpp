#pragma once

namespace tvk {

void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkImageMemoryBarrier2 img_barrier);
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkBufferMemoryBarrier2 img_barrier);
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     std::span<VkBufferMemoryBarrier2> buffer_barriers,
                     std::span<VkImageMemoryBarrier2> img_barriers);
}  // namespace tvk
