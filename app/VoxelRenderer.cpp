#include "VoxelRenderer.hpp"

#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include "ChunkMeshManager.hpp"
#include "Descriptors.hpp"
#include "Image.hpp"
#include "Initializers.hpp"
#include "Pipeline.hpp"
#include "Resource.hpp"
#include "StagingBufferPool.hpp"
#include "Types.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "imgui.h"
#include "tvk/Barrier.hpp"
#include "tvk/Error.hpp"

using tvk::DescriptorBuilder;
using tvk::ImagePipelineState;
using tvk::PipelineBarrier;
using tvk::init::ImageBarrier;
using tvk::init::ImageBarrierUpdate;
using tvk::util::BlitImage;

void VoxelRenderer::Draw(bool draw_imgui) {
  ZoneScoped;
  if (!UpdateSwapchainAndCheckIfReady()) {
    return;
  }
  WaitForMainRenderFence();
  ChunkMeshManager::Get().Update();
  FlushFrameData();
  if (!AcquireNextImage()) {
    return;
  }
  VK_CHECK(vkResetFences(device_, 1, &GetCurrentFrame().render_fence));
  VK_CHECK(vkResetCommandBuffer(GetCurrentFrame().main_command_buffer, 0));

  VkCommandBuffer cmd = GetCurrentFrame().main_command_buffer;
  auto cmd_begin_info =
      tvk::init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

  ImagePipelineState draw_img_state{VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                    0};
  ImagePipelineState next_draw_img_state{VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};

  // ImagePipelineState next_draw_img_state{VK_IMAGE_LAYOUT_GENERAL,
  //                                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
  //                                        VK_ACCESS_2_SHADER_WRITE_BIT};
  PipelineBarrier(cmd, 0,
                  ImageBarrierUpdate(draw_image_.image, draw_img_state, next_draw_img_state));

  SceneDataUBO scene_data_ubo_cpu;
  scene_data_ubo_cpu.view_pos_int = glm::floor(scene_data_->cam_pos);
  vec3 intra_voxel_pos = scene_data_->cam_pos - glm::floor(scene_data_->cam_pos);
  float aspect = static_cast<float>(draw_dims_.x) / static_cast<float>(draw_dims_.y);
  if (draw_dims_.x == 0 && draw_dims_.y == 0) {
    aspect = 1.f;
  }

  float near = 0.1f;
  float far = 1000.f;
  std::swap(near, far);
  glm::mat4 proj = glm::perspective(70.f, aspect, near, far);
  proj[1][1] *= -1;
  scene_data_ubo_cpu.proj = proj;
  scene_data_ubo_cpu.world_center_view =
      glm::lookAt(intra_voxel_pos, intra_voxel_pos + scene_data_->cam_dir, vec3(0, 1, 0));
  scene_data_ubo_cpu.world_center_viewproj = proj * scene_data_ubo_cpu.world_center_view;

  auto* scene_uniform_data =
      static_cast<SceneDataUBO*>(GetExtendedFrameData().scene_data_ubo_buffer.data);
  *scene_uniform_data = scene_data_ubo_cpu;
  tvk::DescriptorBuilder b;
  auto scene_ubo_info = GetExtendedFrameData().scene_data_ubo_buffer.GetInfo();
  VkDescriptorSet scene_set =
      b.Begin(VK_SHADER_STAGE_VERTEX_BIT)
          .WriteBuffer(0, &scene_ubo_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);

  draw_dims_ = {draw_image_.extent.width, draw_image_.extent.height};
  // DrawRayMarchCompute(cmd, draw_image_);
  VkClearValue clear{.color = VkClearColorValue{{0.0, 0.0, 1.0, 1.0}}};
  auto color_attachment = tvk::init::AttachmentInfo(draw_image_.view, &clear);
  auto render_info = tvk::init::RenderingInfo(draw_image_.Extent2D(), &color_attachment);
  vkCmdBeginRendering(cmd, &render_info);
  SetViewportAndScissor(cmd, {draw_dims_.x, draw_dims_.y});
  DrawChunks(scene_set, cmd);
  vkCmdEndRendering(cmd);

  next_draw_img_state = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                         VK_ACCESS_2_TRANSFER_READ_BIT};
  {
    VkImageMemoryBarrier2 barriers[] = {
        // resolve image from color attatchment resolve to transfer src
        ImageBarrierUpdate(draw_image_.image, draw_img_state, next_draw_img_state),
        // allow UI image to be transferred to
        ImageBarrier(ui_draw_img_.image, VK_IMAGE_LAYOUT_UNDEFINED, 0, 0,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT),
    };
    PipelineBarrier(cmd, 0, {}, barriers);
  }

  BlitImage(cmd, draw_image_, ui_draw_img_);

  if (draw_imgui) {
    Renderer::DrawImGui(cmd, ui_draw_img_.view,
                        VkExtent2D(ui_draw_img_.extent.width, ui_draw_img_.extent.height));
  }
  {
    VkImageMemoryBarrier2 barriers[] = {
        // draw image to be read from
        ImageBarrier(ui_draw_img_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT),
        // swapchain to be written to
        ImageBarrier(swapchain_.images[swapchain_img_idx_], VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT),
    };
    PipelineBarrier(cmd, 0, {}, barriers);
    BlitImage(cmd, ui_draw_img_.image, swapchain_.images[swapchain_img_idx_],
              ui_draw_img_.Extent2D(), swapchain_.extent);
  }

  VkImageMemoryBarrier2 barriers[] = {
      ImageBarrier(swapchain_.images[swapchain_img_idx_], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_NONE, VK_ACCESS_NONE),
  };
  PipelineBarrier(cmd, 0, {}, barriers);

  EndMainCommandBufferSubmitAndPresent();
}

void VoxelRenderer::InitPipelines() {
  // TODO: make this more streamlined
  chunk_mesh_pipeline_.create_fn = [this](std::span<tvk::Shader> shaders,
                                          tvk::PipelineAndLayout& p) {
    tvk::GraphicsPipelineBuilder p_builder;

    p_builder.SetColorAttachmentFormat(draw_image_.format);
    p_builder.SetDepthFormat(depth_image_.format);
    p_builder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    p_builder.DefaultGraphicsPipeline();
    p_builder.pipeline_layout = p.layout;
    p_builder.SetShaders(shaders);
    p.pipeline = p_builder.BuildPipeline(device_);
  };

  chunk_mesh_pipeline_.shaders = {tvk::Shader{"voxels/chunk.vert"},
                                  tvk::Shader{"voxels/chunk.frag"}};
  pipelines_.push_back(&chunk_mesh_pipeline_);
  RegisterComputePipelines({{&raymarch_pipeline_, "voxels/raymarch_voxel.comp"}});
}

void VoxelRenderer::Draw(const SceneData* scene_data, bool draw_imgui) {
  scene_data_ = scene_data;
  Draw(draw_imgui);
}
void VoxelRenderer::DrawRayMarchCompute(VkCommandBuffer cmd, tvk::AllocatedImage& img) {
  raymarch_pipeline_.BindCompute(cmd);
  struct PC {
    vec4 aabb_min;
    vec4 aabb_max;
    vec3 cam_dir;
    float time;
    vec3 cam_pos;
  } pc;
  pc.aabb_min = vec4(vsettings.aabb.min, 0);
  pc.aabb_max = vec4(vsettings.aabb.max, 0);
  pc.time = scene_data_->time;
  pc.cam_dir = scene_data_->cam_dir;
  pc.cam_pos = scene_data_->cam_pos;
  DescriptorBuilder b;
  VkDescriptorSet set = b.Begin(VK_SHADER_STAGE_COMPUTE_BIT)
                            .WriteGeneralStorageImage(0, img)
                            .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);
  VkDescriptorSet sets[] = {set};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, raymarch_pipeline_.pipeline->layout,
                          0, COUNTOF(sets), sets, 0, nullptr);
  vkCmdPushConstants(cmd, raymarch_pipeline_.pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(PC), &pc);
  vkCmdDispatch(cmd, (img.extent.width + 7) / 8, (img.extent.height + 7) / 8, 1);
}

void VoxelRenderer::DrawImGui() {
  ImGui::DragFloat3("aabb min", &vsettings.aabb.min.x);
  ImGui::DragFloat3("aabb max", &vsettings.aabb.max.x);
}

void VoxelRenderer::DrawChunks(VkCommandBuffer, tvk::AllocatedImage&) {}

void VoxelRenderer::Init(Window* window) {
  Renderer::Init(window);
  fence_pool_.Init(device_);
  ChunkMeshManager::Get().Init(this);
  staging_buffer_pool_.Init({sizeof(uint64_t) * 20000});

  main_deletion_queue_.PushFunc([this]() {
    ChunkMeshManager::Get().Cleanup();
    fence_pool_.Cleanup();
    staging_buffer_pool_.Cleanup();
    for (auto& frame : extented_frame_data_) {
      allocator_.DestroyBuffer(frame.scene_data_ubo_buffer);
    }
  });
  for (auto& frame : extented_frame_data_) {
    frame.scene_data_ubo_buffer =
        allocator_.CreateBuffer(sizeof(SceneDataUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);
  }
}

VoxelRenderer::VoxelRenderer() : staging_buffer_pool_(allocator_) {}
void VoxelRenderer::DrawChunks(VkDescriptorSet scene_data_set, VkCommandBuffer cmd) {
  chunk_mesh_pipeline_.BindGraphics(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          chunk_mesh_pipeline_.pipeline->layout, 0, 1, &scene_data_set, 0, nullptr);

  auto info = ChunkMeshManager::Get().chunk_quad_buffer_.device_buffer.GetInfo();
  DescriptorBuilder b;
  VkDescriptorSet quad_set = b.Begin(VK_SHADER_STAGE_VERTEX_BIT)
                                 .WriteBuffer(0, &info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                                 .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          chunk_mesh_pipeline_.pipeline->layout, 1, 1, &quad_set, 0, nullptr);
  vkCmdBindIndexBuffer(cmd, ChunkMeshManager::Get().quad_index_buf_.buffer, 0,
                       VK_INDEX_TYPE_UINT32);
  auto& buf = ChunkMeshManager::Get().draw_indir_gpu_buf_;
  vkCmdDrawIndexedIndirect(cmd, buf.buffer, 0, buf.size / sizeof(VkDrawIndexedIndirectCommand),
                           sizeof(VkDrawIndexedIndirectCommand));
}

void VoxelRenderer::UpdateSceneDataUBO() {}
