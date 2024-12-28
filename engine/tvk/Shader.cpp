#include "Shader.hpp"

#include <spirv_reflect.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <fstream>
#include <utility>

#include "Descriptors.hpp"
#include "Error.hpp"
#include "Initializers.hpp"

namespace tvk {

namespace {

// TODO: change if needed
constexpr const int MaxDescriptorSets = 4;
constexpr const int MaxSetLayoutBindings = 256;

bool LoadShaderBytes(const std::string& path, std::vector<uint32_t>& result) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    fmt::println("Failed to load bytes {}", path);
    return false;
  }
  file.seekg(0, std::ios::end);
  auto len = file.tellg();
  result.resize(len / sizeof(uint32_t));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(result.data()), len);
  file.close();
  return true;
}

struct DescriptorSetLayoutData {
  uint32_t set_number;
  VkDescriptorSetLayoutCreateInfo create_info;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
};

struct ShaderCreateData {
  // NOTE: expand these if ever needed
  static constexpr const int MaxPCRanges = 4;
  std::array<DescriptorSetLayoutData, MaxDescriptorSets> set_layouts;
  uint32_t set_layout_cnt{0};
  std::array<VkPushConstantRange, MaxPCRanges> pc_ranges;
  uint32_t pc_range_cnt{0};
  VkShaderStageFlags shader_stages;
  uint32_t shader_stage_cnt{0};
};

#define VALIDATE(x)                        \
  if ((x) != SPV_REFLECT_RESULT_SUCCESS) { \
    return false;                          \
  }

bool ReflectShaderModule(Shader& shader, const std::vector<uint32_t>& code,
                         ShaderCreateData& out_data) {
  SpvReflectShaderModule module = {};
  VALIDATE(spvReflectCreateShaderModule(code.size() * sizeof(uint32_t), code.data(), &module));
  uint32_t cnt = 0;
  VALIDATE(spvReflectEnumerateDescriptorSets(&module, &cnt, nullptr));

  std::vector<SpvReflectDescriptorSet*> sets(cnt);
  VALIDATE(spvReflectEnumerateDescriptorSets(&module, &cnt, sets.data()));

  VALIDATE(spvReflectEnumeratePushConstantBlocks(&module, &cnt, nullptr));
  std::vector<SpvReflectBlockVariable*> refl_pc_blocks(cnt);
  VALIDATE(spvReflectEnumeratePushConstantBlocks(&module, &cnt, refl_pc_blocks.data()));
  shader.stage = static_cast<VkShaderStageFlagBits>(module.shader_stage);

  if (cnt > 1) {
    fmt::println("Shader module has > 1 push constant range, not handled");
  }
  if (cnt > 0) {
    out_data.pc_ranges[out_data.pc_range_cnt++] =
        VkPushConstantRange{.stageFlags = static_cast<VkShaderStageFlags>(module.shader_stage),
                            .offset = refl_pc_blocks[0]->offset,
                            .size = refl_pc_blocks[0]->size};
  }

  // vertex input
  // VALIDATE(spvReflectEnumerateInputVariables(&module, &cnt, nullptr));
  // std::vector<SpvReflectInterfaceVariable*> refl_int_vars(cnt);
  // VALIDATE(spvReflectEnumerateInputVariables(&module, &cnt, refl_int_vars.data()));
  // for (auto& v : refl_int_vars) {
  //   fmt::println("name: {},loc: {},component: {}", v->name, v->location, v->component);
  // }

  for (const SpvReflectDescriptorSet* set : sets) {
    const SpvReflectDescriptorSet& refl_set = *set;
    DescriptorSetLayoutData& layout = out_data.set_layouts[out_data.set_layout_cnt++];
    layout.bindings.resize(refl_set.binding_count);
    for (uint32_t binding_idx = 0; binding_idx < refl_set.binding_count; binding_idx++) {
      auto& binding = layout.bindings[binding_idx];
      const auto& refl_binding = *(refl_set.bindings[binding_idx]);
      binding.binding = refl_binding.binding;
      binding.descriptorType = static_cast<VkDescriptorType>(refl_binding.descriptor_type);
      binding.descriptorCount = 1;
      for (uint32_t dim_idx = 0; dim_idx < refl_binding.array.dims_count; dim_idx++) {
        binding.descriptorCount *= refl_binding.array.dims[dim_idx];
      }
      binding.stageFlags = static_cast<VkShaderStageFlagBits>(module.shader_stage);
    }
    layout.set_number = refl_set.set;
    layout.create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.create_info.bindingCount = refl_set.binding_count;
    // NOTE: must move the vector and not copy, or the pBindings will be invalidated
    layout.create_info.pBindings = layout.bindings.data();
  }

  out_data.shader_stages |= module.shader_stage;
  out_data.shader_stage_cnt++;
  return true;

}  // namespace

}  // namespace

namespace detail {

struct PipelineManagerImpl {
  VkDevice device;
};

}  // namespace detail

bool ReflectShader(VkDevice device, DescriptorSetLayoutCache& layout_cache,
                   std::span<Shader> shaders, VkPipelineLayout* out_pipeline_layout) {
  if (shaders.empty()) {
    fmt::println("Need at least one shader to reflect");
    return false;
  }
  ShaderCreateData data;
  for (auto& shader : shaders) {
    std::vector<uint32_t> shader_bytes;
    if (!LoadShaderBytes(shader.path, shader_bytes)) {
      fmt::println("failed to load shader data: {}", shader.path);
      return false;
    }
    if (!ReflectShaderModule(shader, shader_bytes, data)) {
      fmt::println("failed to reflect shader module: {}", shader.path);
      return false;
    }

    if (!LoadShaderModule(shader.module, shader_bytes, device)) {
      fmt::println("failed to load shader module: {}", shader.path);
      return false;
    }
  }

  // for each set, merge the bindings together
  std::array<DescriptorSetLayoutData, MaxDescriptorSets> merged_layout_datas{};
  std::array<VkDescriptorSetLayout, MaxDescriptorSets> combined_set_layouts{};
  std::array<uint64_t, MaxDescriptorSets> merged_layout_hashes{};
  // go through each of the possible set indices
  for (uint32_t set_number = 0; set_number < MaxDescriptorSets; set_number++) {
    // set merged layout info
    auto& merged_layout = merged_layout_datas[set_number];
    merged_layout.set_number = set_number;

    std::array<std::pair<VkDescriptorSetLayoutBinding, bool>, MaxSetLayoutBindings> used_bindings{};
    for (uint32_t unmerged_set_ly_idx = 0; unmerged_set_ly_idx < data.set_layout_cnt;
         unmerged_set_ly_idx++) {
      const auto& unmerged_set_layout = data.set_layouts[unmerged_set_ly_idx];
      // if the unmerged set corresponds, merge it
      if (unmerged_set_layout.set_number != set_number) continue;

      // for each of the bindings, if it's already used, add the shader stage since it doesn't need
      // to be duplicated, otherwise mark used and set it
      for (const auto& binding : unmerged_set_layout.bindings) {
        if (binding.binding >= MaxSetLayoutBindings) {
          fmt::println("exceed max layout bindings: {}", shaders[0].path);
          return false;
        }
        if (used_bindings[binding.binding].second) {
          used_bindings[binding.binding].first.stageFlags |= binding.stageFlags;
        } else {
          used_bindings[binding.binding].second = true;
          used_bindings[binding.binding].first = binding;
        }
      }
    }
    // add the used bindings to the merged layout
    for (uint32_t i = 0; i < MaxSetLayoutBindings; i++) {
      if (used_bindings[i].second) {
        merged_layout.bindings.push_back(used_bindings[i].first);
      }
    }
    std::ranges::sort(merged_layout.bindings,
                      [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b) {
                        return a.binding < b.binding;
                      });
    merged_layout.create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    merged_layout.create_info.bindingCount = merged_layout.bindings.size();
    merged_layout.create_info.pBindings = merged_layout.bindings.data();
    merged_layout.create_info.flags = 0;
    merged_layout.create_info.pNext = nullptr;
    if (merged_layout.create_info.bindingCount > 0) {
      auto res = layout_cache.CreateLayout(device, merged_layout.create_info);
      merged_layout_hashes[set_number] = res.hash;
      combined_set_layouts[set_number] = res.layout;
    } else {
      merged_layout_hashes[set_number] = 0;
      combined_set_layouts[set_number] = VK_NULL_HANDLE;
    }
  }

  // use dummy layouts for the case when sets are akin to: [null, 1, null, null]
  // set cnt is the last valid set, so in the above case, 1
  uint32_t set_cnt = 0;
  for (uint32_t i = 0; i < MaxDescriptorSets; i++) {
    if (combined_set_layouts[i] != VK_NULL_HANDLE) {
      set_cnt = i + 1;
    } else {
      combined_set_layouts[i] = layout_cache.DummyLayout();
    }
  }
  VkPipelineLayoutCreateInfo pipeline_layout_info = init::PipelineLayoutCreateInfo();
  // TODO: flags?
  pipeline_layout_info.flags = 0;
  pipeline_layout_info.setLayoutCount = set_cnt;
  pipeline_layout_info.pSetLayouts = set_cnt == 0 ? nullptr : combined_set_layouts.data();
  pipeline_layout_info.pushConstantRangeCount = data.pc_range_cnt;
  pipeline_layout_info.pPushConstantRanges = data.pc_ranges.data();
  VK_CHECK(vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr, out_pipeline_layout));

  return true;
}

bool LoadShaderModule(VkShaderModule& module, const char* filepath, VkDevice device) {
  // open file with cursor at end
  std::ifstream file(filepath, std::ios::ate | std::ios::binary);
  if (!file.is_open()) {
    fmt::println("Failed to open file: {}", filepath);
    return {};
  }

  size_t file_size = file.tellg();
  // spriv expects uint32_t
  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));
  file.seekg(0);
  file.read(reinterpret_cast<char*>(buffer.data()), file_size);
  file.close();
  return LoadShaderModule(module, buffer, device);
}

bool LoadShaderModule(VkShaderModule& module, const std::vector<uint32_t>& code, VkDevice device) {
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.pNext = nullptr;

  create_info.codeSize = code.size() * sizeof(uint32_t);
  create_info.pCode = code.data();

  return vkCreateShaderModule(device, &create_info, nullptr, &module) == VK_SUCCESS;
}

void DestroyModules(VkDevice device, std::span<Shader> shaders) {
  for (auto& shader : shaders) {
    vkDestroyShaderModule(device, shader.module, nullptr);
    shader.module = VK_NULL_HANDLE;
  }
}

Shader::Shader(const std::string& p) : path(SHADER_PATH(p + ".spv")) {}

void DestroyModules(VkDevice device, Shader& shader) {
  vkDestroyShaderModule(device, shader.module, nullptr);
  shader.module = VK_NULL_HANDLE;
}
}  // namespace tvk
