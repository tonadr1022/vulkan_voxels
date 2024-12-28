#include "Error.hpp"

#include <vulkan/vk_enum_string_helper.h>

namespace tvk {

void PrintVkError(size_t x) {
  auto err = static_cast<VkResult>(x);
  if (err) {
    fmt::println("Detected Vulkan error: {}", string_VkResult(err));
    exit(1);
  }
}
VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, [[maybe_unused]] void* pUserData) {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    fmt::println("Debug Callback err: {}", pCallbackData->pMessage);
    // #ifndef NDEBUG
    exit(1);
    // #endif
  }
  return VK_FALSE;
}

}  // namespace tvk
