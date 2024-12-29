#include "Renderer.hpp"

#include <vulkan/vk_enum_string_helper.h>
#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "Config.hpp"
#include "Descriptors.hpp"
#include "Pipeline.hpp"
#include "SDL3/SDL_vulkan.h"
#include "Swapchain.hpp"
#include "Util.hpp"
#include "VkBootstrap.h"
#include "application/CVar.hpp"
#include "application/Window.hpp"
#include "glm/packing.hpp"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "tvk/Barrier.hpp"
#include "tvk/Error.hpp"
#include "tvk/ImGuiUtil.hpp"
#include "tvk/Initializers.hpp"
#include "tvk/SamplerCache.hpp"
#include "tvk/Util.hpp"

struct Settings {
  inline static AutoCVarInt vsync{"renderer.vsync", "display vsync", 1, CVarFlags::EditCheckbox};
};

void Renderer::Init(Window* window) {
  assert(!is_initialized_ && "Cannot initialize twice");
  is_initialized_ = true;
  this->window_ = window;
  fmt::println("VK_ICD_FILENAMES: {}", ::util::GetEnvVar("VK_ICD_FILENAMES"));
  InitShaderWatcher(shader_hot_reload_wait_time_);
  InitVulkan();
  InitSwapchain();
  InitCommands();
  InitSyncStructures();
  InitDescriptors();
  InitPipelines();
  LoadShaders(true, true);
  tvk::resource::SamplerCache::Init(device_);
  InitDefaultData();
  set_cache_.Init(device_);

  tvk::util::InitImGuiForVulkan(window->GetContext(), main_deletion_queue_, device_, instance_,
                                chosen_gpu_, graphics_queue_, swapchain_.image_format);
  graphics_queue_ctx_ =
      TracyVkContext(chosen_gpu_, device_, graphics_queue_, frames_[0].main_command_buffer);
}

void Renderer::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) {
  VK_CHECK(vkResetFences(device_, 1, &imm_fence_));
  VK_CHECK(vkResetCommandBuffer(imm_command_buffer_, 0));

  VkCommandBuffer cmd = imm_command_buffer_;
  VkCommandBufferBeginInfo cmd_begin_info =
      tvk::init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

  function(cmd);

  VK_CHECK(vkEndCommandBuffer(cmd));

  VkCommandBufferSubmitInfo cmd_info = tvk::init::CommandBufferSubmitInfo(cmd);
  VkSubmitInfo2 submit = tvk::init::SubmitInfo(&cmd_info, nullptr, nullptr);

  // submit command buffer to the queue and execute it.
  VK_CHECK(vkQueueSubmit2(graphics_queue_, 1, &submit, imm_fence_));
  VK_CHECK(vkWaitForFences(device_, 1, &imm_fence_, true, 99999999999));
}

tvk::AllocatedImage Renderer::CreateImage2D(void* data, VkExtent2D size, VkFormat format,
                                            VkImageUsageFlags usage, bool mipmapped) {
  ZoneScoped;
  // need temporal staging buffer, copy pixels to it, then submit it from staging buffer to the
  // GPU
  size_t data_size = static_cast<size_t>(size.width) * size.height * 4;
  // make staging buffer
  tvk::AllocatedBuffer staging_buffer =
      allocator_.CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
  auto* staging_mapped_data = staging_buffer.data;
  // copy data to staging buffer
  memcpy(staging_mapped_data, data, data_size);

  tvk::AllocatedImage new_image = allocator_.CreateImage2D(
      size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      mipmapped);

  // submit the copy command
  ImmediateSubmit([&, size](VkCommandBuffer cmd) {
    // make image writeable
    tvk::PipelineBarrier(
        cmd, 0,
        tvk::init::ImageBarrier(new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT));
    // specify where in the image to copy to
    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0;
    copy_region.bufferRowLength = 0;
    copy_region.bufferImageHeight = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageExtent = {.width = size.width, .height = size.height, .depth = 1};

    // copy buffer into the image
    vkCmdCopyBufferToImage(cmd, staging_buffer.buffer, new_image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_region);
    if (mipmapped) {
      ::tvk::util::GenerateMipmaps(cmd, new_image.image,
                                   VkExtent2D{new_image.extent.width, new_image.extent.height});
    } else {
      auto dst_stage = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                       : (usage & VK_IMAGE_USAGE_STORAGE_BIT) ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                              : VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
      tvk::PipelineBarrier(
          cmd, 0,
          tvk::init::ImageBarrier(new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, dst_stage,
                                  VK_ACCESS_SHADER_READ_BIT));
    }
  });

  allocator_.DestroyBuffer(staging_buffer);

  return new_image;
}

void Renderer::InitDefaultData() {
  // make default images
  /*
   *VK_IMAGE_USAGE_SAMPLED_BIT specifies that the image can be used to create a VkImageView
   *suitable for occupying a VkDescriptorSet slot either of type VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
   *or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, and be sampled by a shader.
   */
  uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
  default_data_.white_image = CreateImage2D(static_cast<void*>(&white), {1, 1},
                                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

  uint32_t gray = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
  default_data_.gray_image = CreateImage2D(static_cast<void*>(&gray), {1, 1},
                                           VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
  uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
  default_data_.black_image = CreateImage2D(static_cast<void*>(&black), {1, 1},
                                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
  // make checkerboard
  uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
  std::array<uint32_t, 16ul * 16> pixels;
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 16; y++) {
      pixels[(y * 16) + x] = ((x % 2) ^ (y % 2) ? magenta : black);
    }
  }
  default_data_.error_checkerboard_image =
      CreateImage2D(pixels.data(), {16, 16}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

  // Create default samplers
  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  default_data_.default_sampler_linear =
      tvk::resource::SamplerCache::Get().GetSampler(sampler_info);
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  default_data_.default_sampler_nearest =
      tvk::resource::SamplerCache::Get().GetSampler(sampler_info);

  auto depth_sampler_info =
      tvk::init::SamplerCreateInfo(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  depth_sampler_info.compareEnable = VK_TRUE;
  depth_sampler_info.compareOp = VK_COMPARE_OP_LESS;
  default_data_.depth_sampler = tvk::resource::SamplerCache::Get().GetSampler(depth_sampler_info);

  // destroy images
  main_deletion_queue_.PushFunc([this]() {
    allocator_.DestroyImageAndView(default_data_.white_image);
    allocator_.DestroyImageAndView(default_data_.gray_image);
    allocator_.DestroyImageAndView(default_data_.black_image);
    allocator_.DestroyImageAndView(default_data_.error_checkerboard_image);
  });
}

void Renderer::InitDescriptors() {
  // descriptor pool will hold 10 sets with 1 image each
  std::vector<tvk::DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
      tvk::DescriptorAllocatorGrowable::PoolSizeRatio{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                      .ratio = 1}};

  global_descriptor_allocator_.Init(device_, 10, sizes);

  main_deletion_queue_.PushFunc([this]() { global_descriptor_allocator_.DestroyPools(device_); });

  // create per-frame descriptor pools
  for (auto& i : frames_) {
    auto& frame = i;
    std::vector<tvk::DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .ratio = 3},
        {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .ratio = 3},
        {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .ratio = 3},
        {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 4},
    };
    frame.frame_descriptors.Init(device_, 1000, frame_sizes);

    main_deletion_queue_.PushFunc([this, &i]() {
      // destroy uniform buffer for global scene descriptor
      i.frame_descriptors.DestroyPools(device_);
    });
  }
}

void Renderer::InitSyncStructures() {
  // create sync structures
  // one fence to control when gpu has finished rendering the frame
  // 2 semaphores to sync rendering with swapchain
  // fence starts signaled so we can wait on it during first frame
  auto fence_create_info = tvk::init::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
  auto sem_create_info = tvk::init::SemaphoreCreateInfo();
  for (auto& frame : frames_) {
    VK_CHECK(vkCreateFence(device_, &fence_create_info, nullptr, &frame.render_fence));
    VK_CHECK(vkCreateSemaphore(device_, &sem_create_info, nullptr, &frame.swapchain_semaphore));
    VK_CHECK(vkCreateSemaphore(device_, &sem_create_info, nullptr, &frame.render_semaphore));
  }

  VK_CHECK(vkCreateFence(device_, &fence_create_info, nullptr, &transfer_fence_));
  VK_CHECK(vkCreateFence(device_, &fence_create_info, nullptr, &imm_fence_));
  main_deletion_queue_.PushFunc([this]() {
    vkDestroyFence(device_, transfer_fence_, nullptr);
    vkDestroyFence(device_, imm_fence_, nullptr);
  });
}

void Renderer::MakeSwapchainImageViews() {
  swapchain_.img_views.resize(swapchain_.imageCount);
  for (uint32_t i = 0; i < swapchain_.imageCount; i++) {
    if (swapchain_.img_views[i]) {
      vkDestroyImageView(device_, swapchain_.img_views[i], nullptr);
    }
    auto info = tvk::init::ImageViewCreateInfo(swapchain_.image_format, swapchain_.images[i],
                                               VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(device_, &info, nullptr, &swapchain_.img_views[i]));
  }
}

Renderer::~Renderer() = default;

void Renderer::InitCommands() {
  // flag: able to reset individual command buffers instead of just the entire pool
  VkCommandPoolCreateInfo command_pool_info = tvk::init::CommandPoolCreateInfo(
      graphics_queue_family_, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  for (auto& frame_data : frames_) {
    VK_CHECK(vkCreateCommandPool(device_, &command_pool_info, nullptr, &frame_data.command_pool));
    VkCommandBufferAllocateInfo cmd_alloc_info =
        tvk::init::CommandBufferAllocateInfo(frame_data.command_pool);
    VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_alloc_info, &frame_data.main_command_buffer));
  }

  // create command pool and command buffer for immediate submits
  VK_CHECK(vkCreateCommandPool(device_, &command_pool_info, nullptr, &imm_command_pool_));
  VkCommandBufferAllocateInfo cmd_alloc_info =
      tvk::init::CommandBufferAllocateInfo(imm_command_pool_);
  VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_alloc_info, &imm_command_buffer_));

  command_pool_info = tvk::init::CommandPoolCreateInfo(
      transfer_queue_family_, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  VK_CHECK(vkCreateCommandPool(device_, &command_pool_info, nullptr, &transfer_command_pool_));
  cmd_alloc_info = tvk::init::CommandBufferAllocateInfo(transfer_command_pool_);
  VK_CHECK(vkAllocateCommandBuffers(device_, &cmd_alloc_info, &transfer_command_buffer_));
  main_deletion_queue_.PushFunc([this]() {
    vkDestroyCommandPool(device_, transfer_command_pool_, nullptr);
    vkDestroyCommandPool(device_, imm_command_pool_, nullptr);
  });
}

void Renderer::DestroyDrawImages() {
  allocator_.DestroyImageAndView(draw_image_);
  allocator_.DestroyImageAndView(depth_image_);
  allocator_.DestroyImageAndView(ui_draw_img_);
}

void Renderer::LoadDrawImages() {
  if (draw_image_.image != VK_NULL_HANDLE) {
    assert(depth_image_.image && ui_draw_img_.image);
    DestroyDrawImages();
  }
  VkExtent2D size{swapchain_.extent};
  draw_image_ = allocator_.CreateImage2D(size, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                             VK_IMAGE_USAGE_STORAGE_BIT);

  depth_image_ = allocator_.CreateImage2D(
      size, VK_FORMAT_D32_SFLOAT,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

  ui_draw_img_ =
      allocator_.CreateImage2D(size, swapchain_.image_format,
                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
}

void Renderer::InitSwapchain() {
  swapchain_.img_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  glm::uvec2 dims = window_->UpdateWindowSize();
  swapchain_.image_format = GetSwapchainFormat(chosen_gpu_, surface_);
  CreateSwapchain(swapchain_, chosen_gpu_, device_, surface_, graphics_queue_family_, dims.x,
                  dims.y, swapchain_.image_format, Settings::vsync.Get(), swapchain_.swapchain);
  MakeSwapchainImageViews();
  LoadDrawImages();
  main_deletion_queue_.PushFunc([this]() { DestroyDrawImages(); });
}

void Renderer::CreatePipeline(tvk::Pipeline* pipeline, bool force) {
#ifdef SHADER_HOT_RELOAD
  if (!force) {
    bool dirty = false;
    for (const auto& shader_name : pipeline->shaders) {
      if (dirty_shaders_.contains(shader_name.path)) {
        dirty = true;
        break;
      }
    }
    if (!dirty) return;
  }
#endif
  pipeline->Create(device_, GetCurrentFrame().deletion_queue, set_cache_);
}

void Renderer::InitShaderWatcher(float wait_time) {
#ifdef COMPILE_SHADERS
  std::string glslang_validator_path = tvk::util::FindGlslangValidator();
  tvk::util::CompileShadersToSPIRV(SHADER_DIR, glslang_validator_path, false);
  shader_watcher_.Init(
      util::GetCacheFilePath("vulkan_renderer1"), SHADER_DIR,
      [this, glslang_validator_path](const std::string& glsl_path) {
        std::string spv_path = tvk::util::GlslToSpvPath(glsl_path);
        tvk::util::CompileToSPIRV(glslang_validator_path, glsl_path, spv_path);
        dirty_shaders_.insert(spv_path);
#ifdef SHADER_HOT_RELOAD
        for (const auto& p : pipelines_) {
          for (const tvk::Shader& shader : p->shaders) {
            if (shader.path == spv_path) {
              GetCurrentFrame().deletion_queue.PushFunc([p, this]() { CreatePipeline(p, true); });
            }
          }
        }
#endif
      },
      wait_time);
  shader_watcher_.StartWatching();
#endif
}

void Renderer::Cleanup() {
  if (is_initialized_) {
#ifdef COMPILE_SHADERS
    shader_watcher_.StopWatching();
#endif
    vkDeviceWaitIdle(device_);
    TracyVkDestroy(graphics_queue_ctx_);
    tvk::resource::SamplerCache::Get().Clear();

    for (auto& p : pipelines_) {
      if (p && p->pipeline && p->pipeline->pipeline) {
        vkDestroyPipelineLayout(device_, p->pipeline->layout, nullptr);
        vkDestroyPipeline(device_, p->pipeline->pipeline, nullptr);
      }
    }
    set_cache_.Cleanup();

    for (auto& frame : frames_) {
      vkDestroyCommandPool(device_, frame.command_pool, nullptr);
      vkDestroyFence(device_, frame.render_fence, nullptr);
      vkDestroySemaphore(device_, frame.render_semaphore, nullptr);
      vkDestroySemaphore(device_, frame.swapchain_semaphore, nullptr);
      frame.deletion_queue.Flush();
    }

    main_deletion_queue_.Flush();

    for (auto& v : swapchain_.img_views) {
      vkDestroyImageView(device_, v, nullptr);
    }
    DestroySwapchain(device_, swapchain_);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkb::destroy_debug_utils_messenger(instance_, debug_messenger_);
    vkDestroyInstance(instance_, nullptr);
    window_->Shutdown();
    is_initialized_ = false;
  }
}

void Renderer::DrawImGui(VkCommandBuffer cmd, VkImageView target_img_view, VkExtent2D draw_extent) {
  VkRenderingAttachmentInfo color_attachment = tvk::init::AttachmentInfo(target_img_view, nullptr);
  VkRenderingInfo render_info =
      tvk::init::RenderingInfo(draw_extent, &color_attachment, nullptr, nullptr);
  vkCmdBeginRendering(cmd, &render_info);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
  vkCmdEndRendering(cmd);
}

void Renderer::InitVulkan() {
  ZoneScoped;
#ifdef USE_VALIDATION_LAYERS
  bool use_validation_layers = true;
#else
  bool use_validation_layers = false;
#endif

  fmt::println("Using Validation Layers: {}", use_validation_layers);

  vkb::InstanceBuilder builder;
  auto& vkb_instance_builder = builder.set_app_name("Example Vulkan App")
                                   .request_validation_layers(use_validation_layers)
                                   .require_api_version(1, 3, 0);
#ifndef NDEBUG
  vkb_instance_builder.set_debug_callback(tvk::DebugCallback);
#else
  vkb_instance_builder.use_default_debug_messenger();
#endif

  uint32_t cnt;
  const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&cnt);
  vkb_instance_builder.enable_extensions(cnt, exts);
  const auto& vkb_instance_ret = vkb_instance_builder.build();
  if (!vkb_instance_ret.has_value()) {
    fmt::println("Failed to create Vulkan Instance: {}", vkb_instance_ret.error().message());
    exit(1);
  }

  vkb::Instance vkb_instance = vkb_instance_ret.value();
  instance_ = vkb_instance.instance;
  debug_messenger_ = vkb_instance.debug_messenger;

  // Ensure surface is created
  if (!SDL_Vulkan_CreateSurface(window_->GetContext(), instance_, nullptr, &surface_)) {
    fmt::println("Failed to create Vulkan surface: {}", SDL_GetError());
    exit(1);
  }
  VkPhysicalDeviceVulkan13Features features13{};
  VkPhysicalDeviceVulkan12Features features12{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
  features12.bufferDeviceAddress = VK_TRUE;
  features12.drawIndirectCount = VK_TRUE;
  VkPhysicalDeviceFeatures features{};
  features.multiDrawIndirect = VK_TRUE;
  features.drawIndirectFirstInstance = VK_TRUE;
  features.pipelineStatisticsQuery = VK_TRUE;
  features.depthClamp = VK_TRUE;
  features.shaderStorageImageMultisample = VK_TRUE;
  features.sampleRateShading = VK_TRUE;

  vkb::PhysicalDeviceSelector selector{vkb_instance};
  auto phys_device_ret = selector.set_minimum_version(1, 3)
                             .set_required_features_13(features13)
                             .set_required_features(features)
                             .set_required_features_12(features12)
                             .set_surface(surface_)
                             .select();
  if (!phys_device_ret.has_value()) {
    fmt::println("Failed to create Vulkan Physical Device: ", phys_device_ret.error().message());
    exit(1);
  }

  const auto& phys_device = phys_device_ret.value();
  vkb::DeviceBuilder device_builder{phys_device};
  vkb::Device vkb_device = device_builder.build().value();
  device_ = vkb_device.device;
  assert(device_ != VK_NULL_HANDLE);
  chosen_gpu_ = phys_device.physical_device;

  fmt::println("Instance version: {}", vkb_device.instance_version);
  for (const auto& p : phys_device.get_queue_families()) {
    fmt::println("Queue: count: {}, flags: {}", p.queueCount, string_VkQueueFlags(p.queueFlags));
  }
  // TODO: handle not available
  transfer_queue_ = vkb_device.get_dedicated_queue(vkb::QueueType::transfer).value();
  transfer_queue_family_ = vkb_device.get_queue_index(vkb::QueueType::transfer).value();
  graphics_queue_ = vkb_device.get_queue(vkb::QueueType::graphics).value();
  graphics_queue_family_ = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

  VmaAllocatorCreateInfo allocator_info{};
  allocator_info.physicalDevice = chosen_gpu_;
  allocator_info.device = device_;
  allocator_info.instance = instance_;
  allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  VmaAllocator vma_allocator;
  vmaCreateAllocator(&allocator_info, &vma_allocator);
  allocator_.Init(device_, vma_allocator);
  main_deletion_queue_.PushFunc([vma_allocator]() { vmaDestroyAllocator(vma_allocator); });
}

void Renderer::LoadShaders(bool force, bool async) {
  for (auto& pipeline : pipelines_) {
    if (async) {
      GetCurrentFrame().deletion_queue.PushFunc(
          [pipeline, force, this]() { CreatePipeline(pipeline, force); });
    } else {
      CreatePipeline(pipeline, force);
    }
  }
}

void Renderer::DrawImGui() {}

void Renderer::ReloadShaders() { LoadShaders(false, true); }

FrameData& Renderer::GetCurrentFrame() { return frames_[frame_num_ % 2]; }

void Renderer::EndMainCommandBufferSubmitAndPresent() {
  ZoneScoped;
  VkCommandBuffer cmd = GetCurrentFrame().main_command_buffer;
  TracyVkCollect(graphics_queue_ctx_, cmd);

  VK_CHECK(vkEndCommandBuffer(cmd));

  // prepare submission to the QueueTypewait on present semaphore which is signaled when swapchain
  // request_validation_layers signal render semaphore to signal rendering has finished

  VkCommandBufferSubmitInfo cmd_info = tvk::init::CommandBufferSubmitInfo(cmd);
  VkSemaphoreSubmitInfo wait_info = tvk::init::SemaphoreSubmitInfo(
      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, GetCurrentFrame().swapchain_semaphore);
  VkSemaphoreSubmitInfo signal_info = tvk::init::SemaphoreSubmitInfo(
      VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, GetCurrentFrame().render_semaphore);

  VkSubmitInfo2 submit = tvk::init::SubmitInfo(&cmd_info, &signal_info, &wait_info);

  // submit command buffer to the queue and execute it
  // render fence will block until graphic commands finish execution
  VK_CHECK(vkQueueSubmit2(graphics_queue_, 1, &submit, GetCurrentFrame().render_fence));

  // prepare present
  // put image we just rendered into the visible window
  // wait on render semaphore for drawing commands to finish before image is displayed
  VkPresentInfoKHR present_info{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .pNext = nullptr,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &GetCurrentFrame().render_semaphore,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain_.swapchain,
                                .pImageIndices = &swapchain_img_idx_,
                                .pResults = nullptr};
  VkResult present_result = vkQueuePresentKHR(graphics_queue_, &present_info);
  // resize swapchain needed
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR) {
    // remake_swapchain_requested_ = true;
    return;
  }

  frame_num_++;
}

void Renderer::WaitForMainRenderFence() {
  ZoneScoped;
  auto timeout = 1000000000;  // one second
  // wait until gpu finished last frame
  VK_CHECK(vkWaitForFences(device_, 1, &GetCurrentFrame().render_fence, true, timeout));
}

bool Renderer::UpdateSwapchainAndCheckIfReady() {
  glm::uvec2 dims = window_->UpdateWindowSize();
  auto status = UpdateSwapchain(swapchain_, chosen_gpu_, device_, surface_, graphics_queue_family_,
                                dims.x, dims.y, swapchain_.image_format, Settings::vsync.Get());
  if (status == SwapchainStatus::Resized || !draw_image_.image) {
    MakeSwapchainImageViews();
    LoadDrawImages();
  }
  return status != SwapchainStatus::NotReady;
}

void Renderer::FlushFrameData() {
  ZoneScoped;
  // Delete old objects
  GetCurrentFrame().deletion_queue.Flush();
  // clear previous frame descriptors
  GetCurrentFrame().frame_descriptors.ClearPools(device_);
  time_del_queue_.UpdateFrame(frame_num_);
  time_del_queue_.Flush();
}

bool Renderer::AcquireNextImage() {
  ZoneScoped;
  auto timeout = 1000000000;  // one second
  VkResult acquire_next_img_result =
      vkAcquireNextImageKHR(device_, swapchain_.swapchain, timeout,
                            GetCurrentFrame().swapchain_semaphore, nullptr, &swapchain_img_idx_);
  return acquire_next_img_result != VK_ERROR_OUT_OF_DATE_KHR;
}
void Renderer::Screenshot(const std::string&) { assert(0 && "unimplemented"); }

void Renderer::RegisterComputePipelines(
    std::span<std::pair<tvk::Pipeline*, std::string>> pipelines) {
  RegisterComputePipelinesInternal(pipelines);
}
void Renderer::RegisterComputePipelinesInternal(auto pipelines) {
  auto compute_create = [this](std::span<tvk::Shader> shaders, tvk::PipelineAndLayout& p) {
    LoadComputePipeline(device_, shaders, p);
  };
  for (const auto& [ptr, name] : pipelines) {
    ptr->shaders = {tvk::Shader{name}};
    ptr->create_fn = compute_create;
    pipelines_.push_back(ptr);
  }
}
void Renderer::RegisterComputePipelines(
    std::initializer_list<std::pair<tvk::Pipeline*, std::string>> pipelines) {
  RegisterComputePipelinesInternal(pipelines);
}
