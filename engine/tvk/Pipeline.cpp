#include "Pipeline.hpp"

#include "DeletionQueue.hpp"
#include "Error.hpp"
#include "Initializers.hpp"

namespace tvk {

void Pipeline::SetShaders(std::initializer_list<const char*> shader_names) {
  for (const auto& shader_name : shader_names) {
    shaders.emplace_back(shader_name);
  }
}

void Pipeline::Create(VkDevice device, DeletionQueue& del_queue, DescriptorSetLayoutCache& cache) {
  if (pipeline) {
    if (pipeline->pipeline == VK_NULL_HANDLE) {
      assert(pipeline->layout == VK_NULL_HANDLE);
    } else {
      VkPipelineLayout del_layout = pipeline->layout;
      VkPipeline del_pipeline = pipeline->pipeline;
      assert(del_layout != VK_NULL_HANDLE && del_pipeline != VK_NULL_HANDLE);
      del_queue.PushFunc([del_layout, del_pipeline, device]() {
        vkDestroyPipelineLayout(device, del_layout, nullptr);
        vkDestroyPipeline(device, del_pipeline, nullptr);
      });
    }
  }
  PipelineAndLayout p{};
  if (!ReflectShader(device, cache, shaders, &p.layout)) {
    fmt::println("Failed to load shader");
  } else {
    create_fn(shaders, p);
    DestroyModules(device, shaders);
  }
  if (p.pipeline == VK_NULL_HANDLE) {
    fmt::print("Failed to load shader: ");
  } else {
    if (!pipeline) {
      pipeline = std::make_shared<PipelineAndLayout>();
    }
    pipeline->layout = p.layout;
    pipeline->pipeline = p.pipeline;

    fmt::print("Loaded shader: ");
  }
  size_t i = 0;
  for (const auto& shader : shaders) {
    fmt::print("{}", shader.path);
    if (i < shaders.size() - 1) {
      fmt::print(", ");
    }
  }
  fmt::println("");
}
void GraphicsPipelineBuilder::Clear() {
  input_assembly = {};
  default_vertex_input = false;
  color_blending = {};
  color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  rasterizer = {};
  rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.lineWidth = 1.f;
  color_blend_attachment = {};
  multisampling = {};
  multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  pipeline_layout = {};
  depth_stencil = {};
  depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  render_info = {};
  render_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  color_attachment_format = {};
  shader_stages.clear();
}

void GraphicsPipelineBuilder::SetVertexInput(
    const VkPipelineVertexInputStateCreateInfo& vertex_input) {
  vertex_input_info = vertex_input;
}

VkPipeline GraphicsPipelineBuilder::BuildPipeline(VkDevice device) {
  // make viewport state.
  // currently don't support
  // multiple viewports or
  // scissors
  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.pNext = nullptr;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  // setup dummy color blending.

  vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineDynamicStateCreateInfo dynamic_info{};
  dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  VkDynamicState default_dynamic_state[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  if (dynamic_states.empty()) {
    // dynamic state
    dynamic_info.pDynamicStates = default_dynamic_state;
    dynamic_info.dynamicStateCount = 2;
  } else {
    dynamic_info.pDynamicStates = dynamic_states.data();
    dynamic_info.dynamicStateCount = dynamic_states.size();
  }

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.pNext = &render_info;
  pipeline_info.stageCount = static_cast<uint32_t>(shader_stages.size());
  pipeline_info.pStages = shader_stages.data();
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.pDynamicState = &dynamic_info;

  VkPipeline pipeline{};
  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) !=
      VK_SUCCESS) {
    fmt::println(
        "failed to create "
        "pipeline");
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

void GraphicsPipelineBuilder::AddVertexShader(VkShaderModule vertex_shader) {
  shader_stages.clear();
  shader_stages.emplace_back(
      init::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader));
}

void GraphicsPipelineBuilder::SetShaders(std::span<Shader> shaders) {
  shader_stages.clear();
  for (const auto& shader : shaders) {
    shader_stages.emplace_back(init::PipelineShaderStageCreateInfo(shader.stage, shader.module));
  }
}
void GraphicsPipelineBuilder::SetShaders(VkShaderModule vertex_shader,
                                         VkShaderModule fragment_shader) {
  shader_stages.clear();
  shader_stages.emplace_back(
      init::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertex_shader));
  shader_stages.emplace_back(
      init::PipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader));
}

void GraphicsPipelineBuilder::SetLineWidth(float line_width) { rasterizer.lineWidth = line_width; }

void GraphicsPipelineBuilder::SetPolygonMode(VkPolygonMode mode) { rasterizer.polygonMode = mode; }

void GraphicsPipelineBuilder::SetInputTopology(VkPrimitiveTopology topology) {
  input_assembly.topology = topology;
  // for triangle strips and line strips
  input_assembly.primitiveRestartEnable = VK_FALSE;
}

void GraphicsPipelineBuilder::SetCullMode(VkCullModeFlags cull_mode, VkFrontFace front_face) {
  rasterizer.cullMode = cull_mode;
  rasterizer.frontFace = front_face;
}

// void GraphicsPipelineBuilder::EnableMultisampling(int count) {
//   multisampling.sampleShadingEnable = VK_TRUE;
//   // 1 sample by default
//   multisampling.rasterizationSamples = util::ToVkSampleCount(count);
//   multisampling.minSampleShading = 1.0f;
//   multisampling.pSampleMask = nullptr;
//   // no alpha to coverage
//   multisampling.alphaToCoverageEnable = VK_FALSE;
//   multisampling.alphaToOneEnable = VK_FALSE;
// }

void GraphicsPipelineBuilder::SetMultisamplingNone() {
  multisampling.sampleShadingEnable = VK_FALSE;
  // 1 sample by default
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading = 1.0f;
  multisampling.pSampleMask = nullptr;
  // no alpha to coverage
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable = VK_FALSE;
}

void GraphicsPipelineBuilder::DisableBlending() {
  // default write mask
  color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  // no blending
  color_blend_attachment.blendEnable = VK_FALSE;
}

void GraphicsPipelineBuilder::SetColorAttachmentFormat(VkFormat format) {
  color_attachment_format = format;
  // connect format to render info structure
  render_info.colorAttachmentCount = 1;
  render_info.pColorAttachmentFormats = &color_attachment_format;
}

void GraphicsPipelineBuilder::SetDepthFormat(VkFormat format) {
  render_info.depthAttachmentFormat = format;
}

void GraphicsPipelineBuilder::EnableDepthTest(bool depth_write_enable, VkCompareOp op) {
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = depth_write_enable;
  depth_stencil.depthCompareOp = op;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;
  depth_stencil.front = {};
  depth_stencil.back = {};
  depth_stencil.minDepthBounds = 0.f;
  depth_stencil.maxDepthBounds = 1.f;
}

void GraphicsPipelineBuilder::DisableDepthTest() {
  depth_stencil.depthTestEnable = VK_FALSE;
  depth_stencil.depthWriteEnable = VK_FALSE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_NEVER;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable = VK_FALSE;
  depth_stencil.front = {};
  depth_stencil.back = {};
  depth_stencil.minDepthBounds = 0.f;
  depth_stencil.maxDepthBounds = 1.f;
}

void GraphicsPipelineBuilder::EnableBlendingAdditive() {
  color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void GraphicsPipelineBuilder::EnableBlendingAlphaBlend() {
  color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void GraphicsPipelineBuilder::DefaultGraphicsPipeline(uint32_t msaa_count) {
  SetInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  SetPolygonMode(VK_POLYGON_MODE_FILL);
  SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
  if (msaa_count == 1) {
    SetMultisamplingNone();
  } else {
    // EnableMultisampling(msaa_count);
  }
  DisableBlending();

  color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.pNext = nullptr;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.logicOp = VK_LOGIC_OP_COPY;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;
}

void LoadComputePipeline(VkDevice device, std::span<Shader> shaders, PipelineAndLayout& p) {
  VkPipelineShaderStageCreateInfo stage_info{};
  stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stage_info.pNext = nullptr;
  stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  stage_info.module = shaders[0].module;
  stage_info.pName = "main";
  VkComputePipelineCreateInfo compute_pipeline_create_info{};
  compute_pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  compute_pipeline_create_info.pNext = nullptr;
  compute_pipeline_create_info.layout = p.layout;
  compute_pipeline_create_info.stage = stage_info;
  VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &compute_pipeline_create_info,
                                    nullptr, &p.pipeline));
  DestroyModules(device, shaders);
}

void GraphicsPipelineBuilder::SetDefaultVertexInput() { default_vertex_input = true; }
void Pipeline::BindGraphics(VkCommandBuffer cmd) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

void Pipeline::BindCompute(VkCommandBuffer cmd) {
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->pipeline);
}

}  // namespace tvk
