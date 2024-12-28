#pragma once

#include <vulkan/vulkan_core.h>

struct SDL_Window;

enum class SwapchainStatus : uint8_t {
  Ready,
  Resized,
  NotReady,
};

struct Swapchain {
  VkSwapchainKHR swapchain;
  VkFormat image_format;
  std::vector<VkImage> images;
  std::vector<VkImageView> img_views;
  VkImageUsageFlags img_usage;
  VkExtent2D extent;
  uint32_t imageCount;
  bool vsync;
};

VkFormat GetSwapchainFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
void CreateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device,
                     VkSurfaceKHR surface, uint32_t familyIndex, uint32_t new_width,
                     uint32_t new_height, VkFormat format, bool vsync,
                     VkSwapchainKHR oldSwapchain = nullptr);
void DestroySwapchain(VkDevice device, const Swapchain& swapchain);

SwapchainStatus UpdateSwapchain(Swapchain& result, VkPhysicalDevice physicalDevice, VkDevice device,
                                VkSurfaceKHR surface, uint32_t familyIndex, uint32_t new_width,
                                uint32_t new_height, VkFormat format, bool vsync);
