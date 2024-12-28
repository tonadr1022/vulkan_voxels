#pragma once

namespace tvk {

struct PipelineAndLayout {
  VkPipeline pipeline{};
  VkPipelineLayout layout{};
  bool operator==(const PipelineAndLayout& other) const {
    return pipeline == other.pipeline && layout == other.layout;
  }
};

struct ImagePipelineState {
  VkImageLayout layout;
  VkPipelineStageFlags2 stage;
  VkAccessFlags2 access;
  bool operator==(const ImagePipelineState& other) const {
    return layout == other.layout && stage == other.stage && access == other.access;
  }
};

}  // namespace tvk
