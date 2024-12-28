#pragma once

namespace tvk {
void PrintVkError(size_t x);

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData);
}  // namespace tvk

#ifndef NDEBUG
#define VK_CHECK(x)         \
  do {                      \
    ::tvk::PrintVkError(x); \
  } while (0)
#else
#define VK_CHECK(x)        \
  do {                     \
    ::vk::PrintVkError(x); \
  } while (0)
#endif
