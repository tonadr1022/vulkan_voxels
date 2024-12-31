#pragma once

#include <span>

#include "Types.hpp"
namespace tvk {

struct BufferAndState {};
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkImageMemoryBarrier2 img_barrier);
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     VkBufferMemoryBarrier2 img_barrier);
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     std::span<VkBufferMemoryBarrier2> buffer_barriers,
                     std::span<VkImageMemoryBarrier2> img_barriers);

constexpr int MaxImgBarriers = 15;
void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     std::span<ImageAndState*> img_states);

}  // namespace tvk
