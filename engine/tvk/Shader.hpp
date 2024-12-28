#pragma once

#include <vulkan/vulkan_core.h>

namespace tvk {

struct DescriptorSetLayoutCache;
struct Shader {
  explicit Shader(const std::string& path);
  Shader() = default;
  std::string path;
  VkShaderModule module;
  VkShaderStageFlagBits stage;
};
void DestroyModules(VkDevice device, std::span<Shader> shaders);
void DestroyModules(VkDevice device, Shader& shader);
[[nodiscard]] bool ReflectShader(VkDevice device, DescriptorSetLayoutCache& cache,
                                 std::span<Shader> shaders, VkPipelineLayout* out_pipeline_layout);
bool LoadShaderModule(VkShaderModule& module, const char* filepath, VkDevice device);
bool LoadShaderModule(VkShaderModule& module, const std::vector<uint32_t>& code, VkDevice device);

}  // namespace tvk
