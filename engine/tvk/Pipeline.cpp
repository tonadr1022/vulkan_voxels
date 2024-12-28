#include "Pipeline.hpp"

#include "DeletionQueue.hpp"

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
}  // namespace tvk
