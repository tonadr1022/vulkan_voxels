#include "Swapchain.hpp"

#include "Error.hpp"

VkFormat GetSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
  uint32_t format_count = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &format_count, nullptr));
  assert(format_count > 0);

  std::vector<VkSurfaceFormatKHR> formats(format_count);
  VK_CHECK(
      vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &format_count, formats.data()));

  if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
    return VK_FORMAT_R8G8B8A8_UNORM;
  }

  for (uint32_t i = 0; i < format_count; ++i) {
    if (formats[i].format == VK_FORMAT_R8G8B8A8_UNORM ||
        formats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
      return formats[i].format;
    }
  }

  return formats[0].format;
}

namespace {

VkSwapchainKHR CreateSwapchain(VkDevice device, VkSurfaceKHR surface,
                               VkSurfaceCapabilitiesKHR surfaceCaps, uint32_t familyIndex,
                               VkFormat format, uint32_t width, uint32_t height, bool vsync,
                               VkSwapchainKHR oldSwapchain) {
  VkCompositeAlphaFlagBitsKHR surface_composite =
      (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
          ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
      : (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
          ? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
      : (surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
          ? VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
          : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
  VkPresentModeKHR presentMode = VSYNC ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
#else
  VkPresentModeKHR present_mode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
#endif

  VkSwapchainCreateInfoKHR create_info = {};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface;
  create_info.minImageCount = std::max(2u, surfaceCaps.minImageCount);
  create_info.imageFormat = format;
  create_info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  create_info.imageExtent.width = width;
  create_info.imageExtent.height = height;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  create_info.queueFamilyIndexCount = 1;
  create_info.pQueueFamilyIndices = &familyIndex;
  create_info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  create_info.compositeAlpha = surface_composite;
  create_info.presentMode = present_mode;
  create_info.oldSwapchain = oldSwapchain;

  VkSwapchainKHR swapchain = nullptr;
  VK_CHECK(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain));

  return swapchain;
}
}  // namespace

void CreateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device,
                     VkSurfaceKHR surface, uint32_t familyIndex, uint32_t new_width,
                     uint32_t new_height, VkFormat format, bool vsync,
                     VkSwapchainKHR oldSwapchain) {
  VkSurfaceCapabilitiesKHR surface_caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surface_caps));

  VkSwapchainKHR swapchain = CreateSwapchain(
      device, surface, surface_caps, familyIndex, format, surface_caps.minImageExtent.width,
      surface_caps.minImageExtent.height, vsync, oldSwapchain);
  assert(swapchain);

  uint32_t image_count = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr));

  std::vector<VkImage> images(image_count);
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &image_count, images.data()));

  result.swapchain = swapchain;
  result.images = images;
  result.extent.width = new_width;
  result.extent.height = new_height;
  result.imageCount = image_count;
  result.vsync = vsync;
}

void DestroySwapchain(VkDevice device, const Swapchain& swapchain) {
  vkDestroySwapchainKHR(device, swapchain.swapchain, nullptr);
}

SwapchainStatus UpdateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device,
                                VkSurfaceKHR surface, uint32_t familyIndex, uint32_t new_width,
                                uint32_t new_height, VkFormat format, bool vsync) {
  if (new_width == 0 || new_height == 0) return SwapchainStatus::NotReady;

  if (result.extent.width == new_width && result.extent.height == new_height &&
      vsync == result.vsync) {
    return SwapchainStatus::Ready;
  }

  Swapchain old = result;

  CreateSwapchain(result, physicalDevice, device, surface, familyIndex, new_width, new_height,
                  format, vsync, old.swapchain);

  VK_CHECK(vkDeviceWaitIdle(device));

  DestroySwapchain(device, old);

  return SwapchainStatus::Resized;
}
