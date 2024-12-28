#pragma once

struct SDL_Window;
namespace tvk {
struct DeletionQueue;
}
namespace tvk::util {

void InitImGuiForVulkan(SDL_Window* window, DeletionQueue& deletion_queue, VkDevice device,
                        VkInstance instance, VkPhysicalDevice chosen_gpu, VkQueue queue,
                        VkFormat swapchain_image_format);
}  // namespace tvk::util
