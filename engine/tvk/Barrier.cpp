#include "Barrier.hpp"

#include <vulkan/vulkan_core.h>

#include "Initializers.hpp"
#include "Types.hpp"

namespace tvk {

namespace {

void PipelineBarrierInternal(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                             VkBufferMemoryBarrier2* buffer_barriers, uint32_t buffer_barrier_cnt,
                             VkImageMemoryBarrier2* img_barriers, uint32_t img_barrier_cnt) {
  VkDependencyInfo dep_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                            .pNext = nullptr,
                            .dependencyFlags = dep_flags,
                            .memoryBarrierCount = 0,
                            .pMemoryBarriers = nullptr,
                            .bufferMemoryBarrierCount = buffer_barrier_cnt,
                            .pBufferMemoryBarriers = buffer_barriers,
                            .imageMemoryBarrierCount = img_barrier_cnt,
                            .pImageMemoryBarriers = img_barriers};
  vkCmdPipelineBarrier2(cmd, &dep_info);
}
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

void PipelineBarrier(VkCommandBuffer cmd, VkDependencyFlags dep_flags,
                     std::span<ImageAndState*> img_states) {
  std::array<VkImageMemoryBarrier2, MaxImgBarriers> barriers;
  for (int i = 0; i < img_states.size(); i++) {
    ImageAndState& b = *img_states[i];
    barriers[i] = init::ImageBarrierUpdate(b.img.image, b.curr_state, b.nxt_state, b.aspect);
  }
  PipelineBarrierInternal(cmd, dep_flags, nullptr, 0, barriers.data(), img_states.size());
}
}  // namespace tvk
