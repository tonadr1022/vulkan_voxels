#pragma once

#include <unordered_set>

#include "tvk/DeletionQueue.hpp"
#include "tvk/Descriptors.hpp"
#include "tvk/ImGuiUtil.hpp"
#include "tvk/Image.hpp"
#include "tvk/Pipeline.hpp"
#include "tvk/Resource.hpp"
#include "tvk/ShaderWatcher.hpp"
#include "tvk/Swapchain.hpp"

struct FrameData {
  VkCommandPool command_pool;
  VkCommandBuffer main_command_buffer;
  VkSemaphore swapchain_semaphore, render_semaphore;
  VkFence render_fence;
  tvk::DeletionQueue deletion_queue;
  tvk::DescriptorAllocatorGrowable frame_descriptors;
};

namespace tracy {
class VkCtx;
}
class Window;

struct Renderer {
  void Screenshot(const std::string& path);
  virtual void Draw(bool draw_imgui) = 0;
  virtual void Cleanup();
  virtual void Init(Window* window);
  void ReloadShaders();
  void DrawImGui();

  virtual ~Renderer();

 protected:
  void EndMainCommandBufferSubmitAndPresent();
  bool UpdateSwapchainAndCheckIfReady();
  void FlushFrameData();
  void WaitForMainRenderFence();
  bool AcquireNextImage();
  struct DefaultData {
    tvk::AllocatedImage white_image;
    tvk::AllocatedImage black_image;
    tvk::AllocatedImage gray_image;
    tvk::AllocatedImage error_checkerboard_image;
    VkSampler default_sampler_linear;
    VkSampler default_sampler_nearest;
    VkSampler depth_sampler;
  } default_data_;

  tvk::DescriptorSetLayoutCache descriptor_set_layout_cache_;
  FrameData frames_[2];
  uint64_t frame_num_{0};
  VkInstance instance_;
  VkDebugUtilsMessengerEXT debug_messenger_;
  VkPhysicalDevice chosen_gpu_;
  VkSurfaceKHR surface_;
  tvk::DeletionQueue main_deletion_queue_;
  tvk::TimeDeletionQueue time_del_queue_;

  tvk::Allocator allocator_;
  VkDevice device_;
  VkQueue transfer_queue_;
  VkQueue graphics_queue_;
  uint32_t graphics_queue_family_;
  uint32_t transfer_queue_family_;
  Window* window_;
  ShaderWatcher shader_watcher_;
  std::unordered_set<std::string> dirty_shaders_;
  std::vector<tvk::Pipeline*> pipelines_;
  float shader_hot_reload_wait_time_ = 200;
  Swapchain swapchain_;
  uint32_t swapchain_img_idx_{0};
  VkCommandPool imm_command_pool_;
  VkCommandBuffer imm_command_buffer_;
  VkCommandPool transfer_command_pool_;
  VkCommandBuffer transfer_command_buffer_;
  // TODO: remove
  VkFence transfer_fence_;
  VkFence imm_fence_;
  tvk::DescriptorSetLayoutCache set_cache_;
  tvk::DescriptorAllocatorGrowable global_descriptor_allocator_;
  bool is_initialized_{false};
  tracy::VkCtx* graphics_queue_ctx_{nullptr};

  tvk::AllocatedImage draw_image_;
  tvk::AllocatedImage depth_image_;
  tvk::AllocatedImage ui_draw_img_;

  FrameData& GetCurrentFrame();

  void LoadShaders(bool force, bool async);

  void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
  tvk::AllocatedImage CreateImage2D(void* data, VkExtent2D size, VkFormat format,
                                    VkImageUsageFlags usage, bool mipmapped = false);
  void InitDefaultData();
  void InitPipelines();
  void InitCommands();
  void InitDescriptors();
  void InitSyncStructures();
  void InitSwapchain();
  void InitVulkan();
  void MakeSwapchainImageViews();
  virtual void LoadDrawImages();
  virtual void DestroyDrawImages();

  void CreatePipeline(tvk::Pipeline* pipeline, bool force);

  void InitShaderWatcher(float wait_time);

  void DrawImGui(VkCommandBuffer cmd, VkImageView target_img_view, VkExtent2D draw_extent);
};
