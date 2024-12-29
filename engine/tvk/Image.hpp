#pragma once

namespace tvk {
struct Pipeline;
struct AllocatedImage;
}  // namespace tvk

namespace tvk::util {

void BlitImage(VkCommandBuffer& cmd, AllocatedImage& src, AllocatedImage& dst);
void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size);
void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size, VkImageAspectFlags src_aspect_mask,
               VkImageAspectFlags dst_aspect_mask);
void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size, VkImageAspectFlags src_aspect_mask,
               VkImageAspectFlags dst_aspect_mask, VkFilter filter);

void GenerateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D image_size);

}  // namespace tvk::util
