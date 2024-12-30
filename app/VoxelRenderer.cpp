#include "VoxelRenderer.hpp"

#include <vulkan/vulkan_core.h>

#include "Descriptors.hpp"
#include "Image.hpp"
#include "Initializers.hpp"
#include "Pipeline.hpp"
#include "Resource.hpp"
#include "Types.hpp"
#include "imgui.h"
#include "tvk/Barrier.hpp"
#include "tvk/Error.hpp"

using tvk::DescriptorBuilder;
using tvk::ImagePipelineState;
using tvk::PipelineAndLayout;
using tvk::PipelineBarrier;
using tvk::Shader;
using tvk::init::ImageBarrier;
using tvk::init::ImageBarrierUpdate;
using tvk::util::BlitImage;

void VoxelRenderer::Draw(bool draw_imgui) {
  if (!UpdateSwapchainAndCheckIfReady()) {
    return;
  }
  WaitForMainRenderFence();
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

  draw_dims_ = {draw_image_.extent.width, draw_image_.extent.height};
  // DrawRayMarchCompute(cmd, draw_image_);
  VkClearValue clear{.color = VkClearColorValue{{0.0, 0.0, 1.0, 1.0}}};
  auto color_attachment = tvk::init::AttachmentInfo(draw_image_.view, &clear);
  auto render_info = tvk::init::RenderingInfo(draw_image_.Extent2D(), &color_attachment);
  vkCmdBeginRendering(cmd, &render_info);
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
  // chunk_mesh_pipeline_.create_fn = [this](std::span<Shader> shaders, PipelineAndLayout& p) {
  //   tvk::GraphicsPipelineBuilder p_builder;
  //   p_builder.DefaultGraphicsPipeline();
  // };
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
