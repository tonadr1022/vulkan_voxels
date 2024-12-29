#pragma once

#include <functional>
#include <memory>

#include "Shader.hpp"
#include "Types.hpp"

namespace tvk {

struct DescriptorSetLayoutCache;
struct DeletionQueue;

struct Pipeline {
  using CreateFn = std::function<void(std::span<Shader>, PipelineAndLayout&)>;
  void Create(VkDevice device, DeletionQueue& del_queue, DescriptorSetLayoutCache& cache);
  void SetShaders(std::initializer_list<const char*> shader_names);
  std::vector<Shader> shaders;
  CreateFn create_fn;
  std::shared_ptr<PipelineAndLayout> pipeline;
  void BindCompute(VkCommandBuffer cmd);
  void BindGraphics(VkCommandBuffer cmd);
};

struct Shader;
class GraphicsPipelineBuilder {
 public:
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
  VkPipelineInputAssemblyStateCreateInfo input_assembly;
  VkPipelineRasterizationStateCreateInfo rasterizer;
  VkPipelineColorBlendAttachmentState color_blend_attachment;
  VkPipelineColorBlendStateCreateInfo color_blending;
  VkPipelineMultisampleStateCreateInfo multisampling;
  VkPipelineLayout pipeline_layout;
  VkPipelineDepthStencilStateCreateInfo depth_stencil;
  VkPipelineRenderingCreateInfo render_info;
  VkFormat color_attachment_format;
  VkPipelineVertexInputStateCreateInfo vertex_input_info{};
  std::vector<VkDynamicState> dynamic_states;
  bool default_vertex_input{false};

  void DefaultGraphicsPipeline(uint32_t msaa_count = 1);
  GraphicsPipelineBuilder() { Clear(); }
  void Clear();
  [[nodiscard]] VkPipeline BuildPipeline(VkDevice device);
  void SetShaders(VkShaderModule vertex_shader, VkShaderModule fragment_shader);
  void SetShaders(std::span<Shader> shaders);
  void AddVertexShader(VkShaderModule vertex_shader);
  void SetInputTopology(VkPrimitiveTopology topology);
  void SetVertexInput(const VkPipelineVertexInputStateCreateInfo& vertex_input);
  void SetDefaultVertexInput();
  void SetPolygonMode(VkPolygonMode mode);
  void SetLineWidth(float line_width);
  void SetCullMode(VkCullModeFlags cull_mode, VkFrontFace front_face);
  // void EnableMultisampling(int count = 2);
  void SetMultisamplingNone();
  void DisableBlending();
  void SetColorAttachmentFormat(VkFormat format);
  void SetDepthFormat(VkFormat format);
  void DisableDepthTest();
  void EnableDepthTest(bool depth_write_enable, VkCompareOp op);
  void EnableBlendingAdditive();
  void EnableBlendingAlphaBlend();
};

struct PipelineLayoutCreateInfo {
  std::vector<VkDescriptorSetLayout> desc_set_layouts;
  std::vector<VkPushConstantRange> pc_infos;
};

// PipelineAndLayout LoadComputePipeline(VkDevice device, const std::string& path,
//                                       VkPipelineLayout layout);
struct DescriptorSetLayoutCache;
void LoadComputePipeline(VkDevice device, std::span<Shader> shaders, PipelineAndLayout& p);
}  // namespace tvk
