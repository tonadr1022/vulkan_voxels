#include "Renderer.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include <glm/vec2.hpp>
#include <unordered_set>

#include "Config.hpp"
#include "Descriptors.hpp"
#include "Resource.hpp"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "Swapchain.hpp"
#include "VkBootstrap.h"
#include "application/CVar.hpp"
#include "application/Util.hpp"
#include "application/Window.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/packing.hpp"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "tvk/Barrier.hpp"
#include "tvk/DeletionQueue.hpp"
#include "tvk/Error.hpp"
#include "tvk/Image.hpp"
#include "tvk/Initializers.hpp"
#include "tvk/Pipeline.hpp"
#include "tvk/SamplerCache.hpp"
#include "tvk/ShaderWatcher.hpp"
#include "tvk/Util.hpp"

namespace tvk {

namespace {

void InitImGui(SDL_Window* window, DeletionQueue& deletion_queue, VkDevice device,
               VkInstance instance, VkPhysicalDevice chosen_gpu, VkQueue queue,
               VkFormat swapchain_image_format) {
  // create descriptor pool
  VkDescriptorPoolSize pool_sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                                       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                                       {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                                       {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                                       {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                                       {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                                       {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                                       {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                                       {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                                       {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                                       {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;

  VkDescriptorPool imgui_pool;
  VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imgui_pool));

  // init imgui
  ImGui::CreateContext();
  if (!ImGui_ImplSDL3_InitForVulkan(window)) {
    fmt::println("Failed to initialize ImGui");
    exit(1);
  }
  ImGui_ImplVulkan_InitInfo init_info{};
  init_info.Instance = instance;
  init_info.PhysicalDevice = chosen_gpu;
  init_info.Device = device;
  init_info.Queue = queue;
  init_info.DescriptorPool = imgui_pool;
  init_info.MinImageCount = 3;
  init_info.ImageCount = 3;
  init_info.UseDynamicRendering = true;

  init_info.PipelineRenderingCreateInfo = {};
  init_info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
  init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchain_image_format;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  if (!ImGui_ImplVulkan_Init(&init_info)) {
    fmt::println("failed to initialize imgui vulkan");
  }
  ImGui_ImplVulkan_CreateFontsTexture();

  deletion_queue.PushFunc([imgui_pool, device]() {
    ImGui_ImplVulkan_Shutdown();
    vkDestroyDescriptorPool(device, imgui_pool, nullptr);
  });
}

struct Settings {
  inline static AutoCVarInt vsync{"renderer.vsync", "display vsync", 1, CVarFlags::EditCheckbox};
};

struct FrameData {
  VkCommandPool command_pool;
  VkCommandBuffer main_command_buffer;
  VkSemaphore swapchain_semaphore, render_semaphore;
  VkFence render_fence;
  DeletionQueue deletion_queue;
  DescriptorAllocatorGrowable frame_descriptors;
};

}  // namespace

struct RendererImpl {
  void Screenshot(const std::string&) {}
  ~RendererImpl() = default;
  DescriptorSetLayoutCache descriptor_set_layout_cache;
  FrameData frames[2];
  uint64_t frame_num{0};
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
  VkPhysicalDevice chosen_gpu;
  VkSurfaceKHR surface;
  DeletionQueue main_deletion_queue;
  Allocator allocator;
  VkDevice device;
  VkQueue transfer_queue;
  VkQueue graphics_queue;
  uint32_t graphics_queue_family;
  uint32_t transfer_queue_family;
  Window* window;
  ShaderWatcher shader_watcher;
  std::unordered_set<std::string> dirty_shaders;
  std::vector<Pipeline*> pipelines;
  float shader_hot_reload_wait_time = 200;
  Swapchain swapchain;
  VkCommandPool imm_command_pool;
  VkCommandBuffer imm_command_buffer;
  VkCommandPool transfer_command_pool;
  VkCommandBuffer transfer_command_buffer;
  // TODO: remove
  VkFence transfer_fence;
  VkFence imm_fence;
  DescriptorSetLayoutCache set_cache;
  DescriptorAllocatorGrowable global_descriptor_allocator;

  AllocatedImage draw_image;
  AllocatedImage depth_image;
  AllocatedImage ui_draw_img;

  void ReloadShaders() { LoadShaders(false, true); }
  void DrawImGui() {}

  FrameData& GetCurrentFrame() { return frames[frame_num % 2]; }

  void LoadShaders(bool force, bool async) {
    for (auto& pipeline : pipelines) {
      if (async) {
        GetCurrentFrame().deletion_queue.PushFunc(
            [pipeline, force, this]() { CreatePipeline(pipeline, force); });
      } else {
        CreatePipeline(pipeline, force);
      }
    }
  }

  void Init(Window* window) {
    this->window = window;
    fmt::println("VK_ICD_FILENAMES: {}", ::util::GetEnvVar("VK_ICD_FILENAMES"));
    InitShaderWatcher(shader_hot_reload_wait_time);
    InitVulkan();
    InitSwapchain();
    InitCommands();
    InitSyncStructures();
    InitDescriptors();
    InitPipelines();
    resource::SamplerCache::Init(device);
    InitDefaultData();
    set_cache.Init(device);

    InitImGui(window->GetContext(), main_deletion_queue, device, instance, chosen_gpu,
              graphics_queue, swapchain.image_format);
  }

  struct DefaultData {
    AllocatedImage white_image;
    AllocatedImage black_image;
    AllocatedImage gray_image;
    AllocatedImage error_checkerboard_image;
    VkSampler default_sampler_linear;
    VkSampler default_sampler_nearest;
    VkSampler depth_sampler;
  } default_data;

  void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function) {
    VK_CHECK(vkResetFences(device, 1, &imm_fence));
    VK_CHECK(vkResetCommandBuffer(imm_command_buffer, 0));

    VkCommandBuffer cmd = imm_command_buffer;
    VkCommandBufferBeginInfo cmd_begin_info =
        init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmd_info = init::CommandBufferSubmitInfo(cmd);
    VkSubmitInfo2 submit = init::SubmitInfo(&cmd_info, nullptr, nullptr);

    // submit command buffer to the queue and execute it.
    VK_CHECK(vkQueueSubmit2(graphics_queue, 1, &submit, imm_fence));
    VK_CHECK(vkWaitForFences(device, 1, &imm_fence, true, 99999999999));
  }
  AllocatedImage CreateImage2D(void* data, VkExtent2D size, VkFormat format,
                               VkImageUsageFlags usage, bool mipmapped = false) {
    ZoneScoped;
    // need temporal staging buffer, copy pixels to it, then submit it from staging buffer to the
    // GPU
    size_t data_size = static_cast<size_t>(size.width) * size.height * 4;
    // make staging buffer
    AllocatedBuffer staging_buffer =
        allocator.CreateBuffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
    auto* staging_mapped_data = staging_buffer.data;
    // copy data to staging buffer
    memcpy(staging_mapped_data, data, data_size);

    AllocatedImage new_image = allocator.CreateImage2D(
        size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        mipmapped);

    // submit the copy command
    ImmediateSubmit([&, size](VkCommandBuffer cmd) {
      // make image writeable
      PipelineBarrier(
          cmd, 0,
          init::ImageBarrier(new_image.image, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_ACCESS_TRANSFER_WRITE_BIT));
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
        util::GenerateMipmaps(cmd, new_image.image,
                              VkExtent2D{new_image.extent.width, new_image.extent.height});
      } else {
        auto dst_stage =
            (usage & VK_IMAGE_USAGE_SAMPLED_BIT)   ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : (usage & VK_IMAGE_USAGE_STORAGE_BIT) ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                   : VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        PipelineBarrier(
            cmd, 0,
            init::ImageBarrier(new_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, dst_stage,
                               VK_ACCESS_SHADER_READ_BIT));
      }
    });

    allocator.DestroyBuffer(staging_buffer);

    return new_image;
  }
  void InitDefaultData() {
    // make default images
    /*
     *VK_IMAGE_USAGE_SAMPLED_BIT specifies that the image can be used to create a VkImageView
     *suitable for occupying a VkDescriptorSet slot either of type VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
     *or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, and be sampled by a shader.
     */
    uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
    default_data.white_image = CreateImage2D(static_cast<void*>(&white), {1, 1},
                                             VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t gray = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
    default_data.gray_image = CreateImage2D(static_cast<void*>(&gray), {1, 1},
                                            VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
    default_data.black_image = CreateImage2D(static_cast<void*>(&black), {1, 1},
                                             VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
    // make checkerboard
    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16ul * 16> pixels;
    for (int x = 0; x < 16; x++) {
      for (int y = 0; y < 16; y++) {
        pixels[(y * 16) + x] = ((x % 2) ^ (y % 2) ? magenta : black);
      }
    }
    default_data.error_checkerboard_image = CreateImage2D(
        pixels.data(), {16, 16}, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);

    // Create default samplers
    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    default_data.default_sampler_linear = resource::SamplerCache::Get().GetSampler(sampler_info);
    sampler_info.minFilter = VK_FILTER_NEAREST;
    sampler_info.magFilter = VK_FILTER_NEAREST;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    default_data.default_sampler_nearest = resource::SamplerCache::Get().GetSampler(sampler_info);

    auto depth_sampler_info =
        init::SamplerCreateInfo(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    depth_sampler_info.compareEnable = VK_TRUE;
    depth_sampler_info.compareOp = VK_COMPARE_OP_LESS;
    default_data.depth_sampler = resource::SamplerCache::Get().GetSampler(depth_sampler_info);

    // destroy images
    main_deletion_queue.PushFunc([this]() {
      allocator.DestroyImageAndView(default_data.white_image);
      allocator.DestroyImageAndView(default_data.gray_image);
      allocator.DestroyImageAndView(default_data.black_image);
      allocator.DestroyImageAndView(default_data.error_checkerboard_image);
      // vkDestroySampler(device_, default_shadow_sampler_, nullptr);
      vkDestroySampler(device, default_data.default_sampler_linear, nullptr);
      vkDestroySampler(device, default_data.default_sampler_nearest, nullptr);
    });
  }
  void InitPipelines() { LoadShaders(true, true); }

  void InitDescriptors() {
    // descriptor pool will hold 10 sets with 1 image each
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
        DescriptorAllocatorGrowable::PoolSizeRatio{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                   .ratio = 1}};

    global_descriptor_allocator.Init(device, 10, sizes);

    main_deletion_queue.PushFunc([this]() { global_descriptor_allocator.DestroyPools(device); });

    // create per-frame descriptor pools
    for (auto& i : frames) {
      auto& frame = i;
      std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
          {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .ratio = 3},
          {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .ratio = 3},
          {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .ratio = 3},
          {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .ratio = 4},
      };
      frame.frame_descriptors.Init(device, 1000, frame_sizes);

      main_deletion_queue.PushFunc([this, &i]() {
        // destroy uniform buffer for global scene descriptor
        i.frame_descriptors.DestroyPools(device);
      });
    }
  }
  void InitSyncStructures() {
    // create sync structures
    // one fence to control when gpu has finished rendering the frame
    // 2 semaphores to sync rendering with swapchain
    // fence starts signaled so we can wait on it during first frame
    auto fence_create_info = init::FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
    auto sem_create_info = init::SemaphoreCreateInfo();
    for (auto& frame : frames) {
      VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &frame.render_fence));
      VK_CHECK(vkCreateSemaphore(device, &sem_create_info, nullptr, &frame.swapchain_semaphore));
      VK_CHECK(vkCreateSemaphore(device, &sem_create_info, nullptr, &frame.render_semaphore));
    }

    VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &transfer_fence));
    VK_CHECK(vkCreateFence(device, &fence_create_info, nullptr, &imm_fence));
    main_deletion_queue.PushFunc([this]() {
      vkDestroyFence(device, transfer_fence, nullptr);
      vkDestroyFence(device, imm_fence, nullptr);
    });
  }
  void MakeSwapchainImageViews() {
    swapchain.img_views.resize(swapchain.imageCount);
    for (uint32_t i = 0; i < swapchain.imageCount; i++) {
      if (swapchain.img_views[i]) {
        vkDestroyImageView(device, swapchain.img_views[i], nullptr);
      }
      auto info = init::ImageViewCreateInfo(swapchain.image_format, swapchain.images[i],
                                            VK_IMAGE_ASPECT_COLOR_BIT);
      VK_CHECK(vkCreateImageView(device, &info, nullptr, &swapchain.img_views[i]));
    }
  }

  void InitCommands() {
    // flag: able to reset individual command buffers instead of just the entire pool
    VkCommandPoolCreateInfo command_pool_info = init::CommandPoolCreateInfo(
        graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    for (auto& frame_data : frames) {
      VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &frame_data.command_pool));
      VkCommandBufferAllocateInfo cmd_alloc_info =
          init::CommandBufferAllocateInfo(frame_data.command_pool);
      VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc_info, &frame_data.main_command_buffer));
    }

    // create command pool and command buffer for immediate submits
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &imm_command_pool));
    VkCommandBufferAllocateInfo cmd_alloc_info = init::CommandBufferAllocateInfo(imm_command_pool);
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc_info, &imm_command_buffer));

    command_pool_info = init::CommandPoolCreateInfo(
        transfer_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(device, &command_pool_info, nullptr, &transfer_command_pool));
    cmd_alloc_info = init::CommandBufferAllocateInfo(transfer_command_pool);
    VK_CHECK(vkAllocateCommandBuffers(device, &cmd_alloc_info, &transfer_command_buffer));
    main_deletion_queue.PushFunc([this]() {
      vkDestroyCommandPool(device, transfer_command_pool, nullptr);
      vkDestroyCommandPool(device, imm_command_pool, nullptr);
    });
  }

  void LoadDrawImages() {
    auto destroy_imgs = [this]() {
      allocator.DestroyImageAndView(draw_image);
      allocator.DestroyImageAndView(depth_image);
      allocator.DestroyImageAndView(ui_draw_img);
    };
    if (draw_image.image != VK_NULL_HANDLE) {
      assert(depth_image.image && ui_draw_img.image);
      destroy_imgs();
    } else {
      main_deletion_queue.PushFunc([&destroy_imgs]() { destroy_imgs(); });
    }
    VkExtent2D size{swapchain.extent};
    draw_image = allocator.CreateImage2D(size, VK_FORMAT_R16G16B16A16_SFLOAT,
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                             VK_IMAGE_USAGE_STORAGE_BIT);

    depth_image = allocator.CreateImage2D(
        size, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);

    ui_draw_img =
        allocator.CreateImage2D(size, swapchain.image_format,
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
  }

  void InitSwapchain() {
    swapchain.img_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    glm::uvec2 dims = window->UpdateWindowSize();
    swapchain.image_format = GetSwapchainFormat(chosen_gpu, surface);
    CreateSwapchain(swapchain, chosen_gpu, device, surface, graphics_queue_family, dims.x, dims.y,
                    swapchain.image_format, Settings::vsync.Get(), swapchain.swapchain);
    MakeSwapchainImageViews();
    LoadDrawImages();
  }
  void CreatePipeline(Pipeline* pipeline, bool force) {
#ifdef SHADER_HOT_RELOAD
    if (!force) {
      bool dirty = false;
      for (const auto& shader_name : pipeline->shaders) {
        if (dirty_shaders.contains(shader_name.path)) {
          dirty = true;
          break;
        }
      }
      if (!dirty) return;
    }
#endif
    pipeline->Create(device, GetCurrentFrame().deletion_queue, descriptor_set_layout_cache);
  }

  void InitShaderWatcher(float wait_time) {
#ifdef COMPILE_SHADERS
    std::string glslang_validator_path = util::FindGlslangValidator();
    util::CompileShadersToSPIRV(SHADER_DIR, glslang_validator_path, false);
    shader_watcher.Init(
        ::util::GetCacheFilePath("vulkan_renderer1"), SHADER_DIR,
        [this, glslang_validator_path](const std::string& glsl_path) {
          std::string spv_path = util::GlslToSpvPath(glsl_path);
          util::CompileToSPIRV(glslang_validator_path, glsl_path, spv_path);
          dirty_shaders.insert(spv_path);
#ifdef SHADER_HOT_RELOAD
          for (const auto& p : pipelines) {
            for (const Shader& shader : p->shaders) {
              if (shader.path == spv_path) {
                GetCurrentFrame().deletion_queue.PushFunc([p, this]() { CreatePipeline(p, true); });
              }
            }
          }
#endif
        },
        wait_time);
    shader_watcher.StartWatching();
#endif
  }

  void Draw() {}
  void Cleanup() {}

  void DrawImGui(VkCommandBuffer cmd, VkImageView target_img_view, VkExtent2D draw_extent) {
    VkRenderingAttachmentInfo color_attachment = init::AttachmentInfo(target_img_view, nullptr);
    VkRenderingInfo render_info =
        init::RenderingInfo(draw_extent, &color_attachment, nullptr, nullptr);
    vkCmdBeginRendering(cmd, &render_info);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);
  }

  void InitVulkan() {
    ZoneScoped;
#ifdef USE_VALIDATION_LAYERS
    bool use_validation_layers = true;
#else
    bool use_validation_layers = true;
#endif

    fmt::println("Using Validation Layers: {}", use_validation_layers);

    vkb::InstanceBuilder builder;
    auto& vkb_instance_builder = builder.set_app_name("Example Vulkan App")
                                     .request_validation_layers(use_validation_layers)
                                     .require_api_version(1, 3, 0);
#ifndef NDEBUG
    vkb_instance_builder.set_debug_callback(DebugCallback);
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
    instance = vkb_instance.instance;
    debug_messenger = vkb_instance.debug_messenger;

    // Ensure surface is created
    if (!SDL_Vulkan_CreateSurface(window->GetContext(), instance, nullptr, &surface)) {
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
                               .set_surface(surface)
                               .select();
    if (!phys_device_ret.has_value()) {
      fmt::println("Failed to create Vulkan Physical Device: ", phys_device_ret.error().message());
      exit(1);
    }

    const auto& phys_device = phys_device_ret.value();
    vkb::DeviceBuilder device_builder{phys_device};
    vkb::Device vkb_device = device_builder.build().value();
    device = vkb_device.device;
    chosen_gpu = phys_device.physical_device;

    fmt::println("Instance version: {}", vkb_device.instance_version);
    for (const auto& p : phys_device.get_queue_families()) {
      fmt::println("Queue: count: {}, flags: {}", p.queueCount, string_VkQueueFlags(p.queueFlags));
    }
    // TODO: handle not available
    transfer_queue = vkb_device.get_dedicated_queue(vkb::QueueType::transfer).value();
    transfer_queue_family = vkb_device.get_queue_index(vkb::QueueType::transfer).value();
    graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
    graphics_queue_family = vkb_device.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocator_info{};
    allocator_info.physicalDevice = chosen_gpu;
    allocator_info.device = device;
    allocator_info.instance = instance;
    allocator_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

    VmaAllocator vma_allocator;
    vmaCreateAllocator(&allocator_info, &vma_allocator);
    allocator.Init(device, vma_allocator);
    main_deletion_queue.PushFunc([vma_allocator]() { vmaDestroyAllocator(vma_allocator); });
  }
};

void Renderer::Cleanup() {}

void Renderer::Draw() {}

void Renderer::Init(Window* window) {
  impl_ = new RendererImpl;
  impl_->Init(window);
}

Renderer::~Renderer() { delete impl_; }

void Renderer::DrawImGui() { impl_->DrawImGui(); }

void Renderer::ReloadShaders() { impl_->ReloadShaders(); }

void Renderer::Screenshot(const std::string& path) { impl_->Screenshot(path); }

}  // namespace tvk
