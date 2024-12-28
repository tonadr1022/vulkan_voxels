#pragma once

#include <vulkan/vulkan_core.h>
namespace tvk::resource {

// TODO: manage lifetimes instead of clearing all
struct SamplerCache {
  static SamplerCache& Get();
  VkSampler GetSampler(const VkSamplerCreateInfo& info);
  static void Init(VkDevice device);
  void Clear();

 private:
  VkDevice device_;
  std::unordered_map<uint64_t, VkSampler> sampler_cache_;
};

}  // namespace tvk::resource
