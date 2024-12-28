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
};

}  // namespace tvk
