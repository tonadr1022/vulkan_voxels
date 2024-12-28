#include "Image.hpp"

#include "Barrier.hpp"
#include "Initializers.hpp"

namespace tvk::util {

void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size, VkImageAspectFlags src_aspect_mask,
               VkImageAspectFlags dst_aspect_mask, VkFilter filter) {
  VkImageBlit2 blit_region{};
  blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
  blit_region.pNext = nullptr;

  blit_region.srcOffsets[1].x = src_size.width;
  blit_region.srcOffsets[1].y = src_size.height;
  blit_region.srcOffsets[1].z = 1;

  blit_region.dstOffsets[1].x = dst_size.width;
  blit_region.dstOffsets[1].y = dst_size.height;
  blit_region.dstOffsets[1].z = 1;

  blit_region.srcSubresource.aspectMask = src_aspect_mask;
  blit_region.srcSubresource.baseArrayLayer = 0;
  blit_region.srcSubresource.layerCount = 1;
  blit_region.srcSubresource.mipLevel = 0;

  blit_region.dstSubresource.aspectMask = dst_aspect_mask;
  blit_region.dstSubresource.baseArrayLayer = 0;
  blit_region.dstSubresource.layerCount = 1;
  blit_region.dstSubresource.mipLevel = 0;

  VkBlitImageInfo2 blit_info{};
  blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
  blit_info.pNext = nullptr;
  blit_info.dstImage = dest;
  blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  blit_info.srcImage = source;
  blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  blit_info.filter = filter;
  blit_info.regionCount = 1;
  blit_info.pRegions = &blit_region;
  vkCmdBlitImage2(cmd, &blit_info);
}
void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size, VkImageAspectFlags src_aspect_mask,
               VkImageAspectFlags dst_aspect_mask) {
  BlitImage(cmd, source, dest, src_size, dst_size, src_aspect_mask, dst_aspect_mask,
            VK_FILTER_LINEAR);
}

void BlitImage(VkCommandBuffer cmd, VkImage source, VkImage dest, VkExtent2D src_size,
               VkExtent2D dst_size) {
  BlitImage(cmd, source, dest, src_size, dst_size, VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);
}

void GenerateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D image_size) {
  int mip_levels =
      static_cast<int>(std::floor(std::log2(std::max(image_size.width, image_size.height)))) + 1;
  for (int mip = 0; mip < mip_levels; mip++) {
    VkExtent2D half_size = image_size;
    half_size.width /= 2;
    half_size.height /= 2;

    // wait for given mip level to be ready to transfer to
    VkImageMemoryBarrier2 image_barrier = init::ImageBarrier(
        image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
        VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT);

    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier.subresourceRange = init::ImageSubresourceRange(aspect_mask);
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.baseMipLevel = mip;
    image_barrier.image = image;

    PipelineBarrier(cmd, 0, image_barrier);

    if (mip < mip_levels - 1) {
      // set the region to blit
      VkImageBlit2 blit_region{};
      blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
      blit_region.srcOffsets[1].x = image_size.width;
      blit_region.srcOffsets[1].y = image_size.height;
      blit_region.srcOffsets[1].z = 1;
      blit_region.dstOffsets[1].x = half_size.width;
      blit_region.dstOffsets[1].y = half_size.height;
      blit_region.dstOffsets[1].z = 1;
      blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      blit_region.srcSubresource.baseArrayLayer = 0;
      blit_region.srcSubresource.layerCount = 1;
      blit_region.srcSubresource.mipLevel = mip;

      blit_region.dstSubresource = blit_region.srcSubresource;
      blit_region.dstSubresource.mipLevel++;

      // set the info
      VkBlitImageInfo2 blit_info{};
      blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
      blit_info.dstImage = image;
      blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      blit_info.srcImage = image;
      blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      blit_info.filter = VK_FILTER_LINEAR;
      blit_info.regionCount = 1;
      blit_info.pRegions = &blit_region;

      vkCmdBlitImage2(cmd, &blit_info);
      image_size = half_size;
    }
  }
  PipelineBarrier(cmd, 0,
                  init::ImageBarrier(
                      image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT));
}

}  // namespace tvk::util
