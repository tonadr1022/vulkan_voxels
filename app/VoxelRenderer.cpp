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
#include "application/CVar.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "tvk/Barrier.hpp"
#include "tvk/Error.hpp"

using tvk::DescriptorBuilder;
using tvk::ImagePipelineState;
using tvk::PipelineBarrier;
using tvk::init::BufferBarrier;
using tvk::init::ImageBarrier;
using tvk::init::ImageBarrierUpdate;
using tvk::util::BlitImage;

namespace {

inline AutoCVarInt wireframe{"renderer.wireframe", "Wireframe Mode", 0, CVarFlags::EditCheckbox};
inline AutoCVarFloat z_far{"renderer.z_far", "Z Far", 400000, CVarFlags::EditFloatDrag};
VkPolygonMode GetPolygonMode() {
  return wireframe.Get() ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
}

inline AutoCVarFloat fov{"renderer.fov", "Field of View", 70.f, CVarFlags::EditFloatDrag};
inline AutoCVarInt reverse_z{"renderer.reverse_z", "Reverse Depth Buffer", 1,
                             CVarFlags::EditCheckbox};
}  // namespace
void VoxelRenderer::Draw(bool draw_imgui) {
  ZoneScoped;
  scene_data_ubo_cpu_.sun_color = vec4(scene_data_->sun_color, 0.0);
  scene_data_ubo_cpu_.sun_dir = vec4(glm::normalize(scene_data_->sun_dir), 0.0);
  scene_data_ubo_cpu_.view_pos_int = ivec4(glm::floor(scene_data_->cam_pos), 0.0);
  scene_data_ubo_cpu_.cam_dir = vec4(scene_data_->cam_dir, 0.0);
  scene_data_ubo_cpu_.ambient_color = vec4(scene_data_->ambient_color, 0.0);
  vec3 intra_voxel_pos = scene_data_->cam_pos - glm::floor(scene_data_->cam_pos);
  float aspect = static_cast<float>(draw_dims_.x) / static_cast<float>(draw_dims_.y);
  if (draw_dims_.x == 0 && draw_dims_.y == 0) {
    aspect = 1.f;
  }

  float near = 0.1f;
  float far = z_far.Get();
  float fov_rad = glm::radians(fov.GetFloat());
  glm::mat4 proj = glm::perspective(fov_rad, aspect, near, std::numeric_limits<float>::infinity());
  if (reverse_z.Get()) {
    proj[2][2] = 0.0f;
    proj[3][2] = near;
  }
  proj[1][1] *= -1;
  scene_data_ubo_cpu_.proj = proj;
  glm::mat4 view = glm::lookAt(intra_voxel_pos, intra_voxel_pos + scene_data_->cam_dir,
                               glm::vec3(0.0f, 1.0f, 0.0f));
  scene_data_ubo_cpu_.world_center_view = view;
  scene_data_ubo_cpu_.world_center_viewproj = proj * view;

  glm::mat4 cull_proj = glm::perspective(fov_rad, aspect, near, far);
  cull_proj[1][1] *= -1;
  frustum_cpu_.SetData(cull_proj * view);

  if (!UpdateSwapchainAndCheckIfReady()) {
    return;
  }
  WaitForMainRenderFence();

  FlushFrameData();
  if (!AcquireNextImage()) {
    return;
  }
  static bool curr_wireframe_mode = wireframe.Get();
  if (wireframe.Get() != curr_wireframe_mode) {
    curr_wireframe_mode = wireframe.Get();
    CreatePipeline(&chunk_mesh_pipeline_, true);
    // LoadShaders(true, true);
  }

  static bool curr_rev_z = reverse_z.Get();
  if (reverse_z.Get() != curr_rev_z) {
    curr_rev_z = reverse_z.Get();
    CreatePipeline(&chunk_mesh_pipeline_, true);
  }
  VK_CHECK(vkResetFences(device_, 1, &GetCurrentFrame().render_fence));
  VK_CHECK(vkResetCommandBuffer(GetCurrentFrame().main_command_buffer, 0));

  auto* scene_uniform_data =
      static_cast<SceneDataUBO*>(GetExtendedFrameData().scene_data_ubo_buffer.data);
  *scene_uniform_data = scene_data_ubo_cpu_;
  ImagePipelineState init_img_state{VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                                    0};
  tvk::ImageAndState draw_img_state(
      init_img_state,
      {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT},
      VK_IMAGE_ASPECT_COLOR_BIT, draw_image_);

  tvk::ImageAndState depth_img_state{
      init_img_state,
      {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
      VK_IMAGE_ASPECT_DEPTH_BIT,
      depth_image_};

  VkCommandBuffer cmd = GetCurrentFrame().main_command_buffer;
  auto cmd_begin_info =
      tvk::init::CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
  VK_CHECK(vkBeginCommandBuffer(cmd, &cmd_begin_info));

  PrepareAndCullChunks(cmd);
  auto& chunk_vert_pool = ChunkMeshManager::Get().chunk_quad_buffer_;
  {
    TracyVkZone(graphics_queue_ctx_, cmd, "copy quads");
    {
      VkBufferMemoryBarrier2 buffer_barriers[] = {
          BufferBarrier(chunk_vert_pool.quad_gpu_buf.buffer, VK_QUEUE_FAMILY_IGNORED,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT),
          BufferBarrier(chunk_vert_pool.vertex_staging.Staging().buffer, VK_QUEUE_FAMILY_IGNORED,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT),
      };
      PipelineBarrier(cmd, 0, buffer_barriers, {});
      chunk_vert_pool.ExecuteCopy(cmd);
      {
        VkBufferMemoryBarrier2 buffer_barriers[] = {
            BufferBarrier(chunk_vert_pool.quad_gpu_buf.buffer, VK_QUEUE_FAMILY_IGNORED,
                          VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                          VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
        };
        PipelineBarrier(cmd, 0, buffer_barriers, {});
      }
    }
  }

  VkBufferMemoryBarrier2 buffer_barriers[] = {
      BufferBarrier(chunk_vert_pool.draw_infos_gpu_buf.buffer, VK_QUEUE_FAMILY_IGNORED,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
      BufferBarrier(chunk_vert_pool.draw_cmd_gpu_buf.buffer, VK_QUEUE_FAMILY_IGNORED,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT),
      BufferBarrier(chunk_vert_pool.draw_count_buffer.buffer, VK_QUEUE_FAMILY_IGNORED,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT),
  };
  PipelineBarrier(cmd, 0, buffer_barriers, {});

  tvk::ImageAndState* img_barriers[] = {&draw_img_state, &depth_img_state};
  PipelineBarrier(cmd, 0, img_barriers);

  static DescriptorBuilder b;
  auto scene_ubo_info = GetExtendedFrameData().scene_data_ubo_buffer.GetInfo();
  VkDescriptorSet scene_set =
      b.Begin(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          .WriteBuffer(0, &scene_ubo_info, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
          .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);

  draw_dims_ = {draw_image_.extent.width, draw_image_.extent.height};
  // DrawRayMarchCompute(cmd, draw_image_);
  VkClearValue clear{.color = VkClearColorValue{{0.0, 0.2, 0.3, 1.0}}};
  auto color_attachment = tvk::init::AttachmentInfo(draw_image_.view, &clear);
  auto depth_attachment = tvk::init::DepthAttachmentInfo(depth_image_.view);
  if (!reverse_z.Get()) {
    depth_attachment.clearValue.depthStencil.depth = 1.f;
  }
  auto render_info =
      tvk::init::RenderingInfo(draw_image_.Extent2D(), &color_attachment, &depth_attachment);
  vkCmdBeginRendering(cmd, &render_info);
  SetViewportAndScissor(cmd, {draw_dims_.x, draw_dims_.y});
  DrawChunks(scene_set, cmd);
  vkCmdEndRendering(cmd);

  draw_img_state.nxt_state = {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
  {
    VkImageMemoryBarrier2 barriers[] = {
        // resolve image from color attatchment resolve to transfer src
        ImageBarrierUpdate(draw_image_.image, draw_img_state.curr_state, draw_img_state.nxt_state),
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
    p_builder.DefaultGraphicsPipeline();

    p_builder.SetColorAttachmentFormat(draw_image_.format);
    p_builder.SetPolygonMode(GetPolygonMode());
    p_builder.SetDepthFormat(depth_image_.format);
    // TODO: reverse
    if (reverse_z.Get()) {
      p_builder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    } else {
      p_builder.EnableDepthTest(true, VK_COMPARE_OP_LESS);
    }
    p_builder.pipeline_layout = p.layout;
    p_builder.SetShaders(shaders);
    p_builder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    p.pipeline = p_builder.BuildPipeline(device_);
  };

  chunk_mesh_pipeline_.shaders = {tvk::Shader{"voxels/chunk.vert"},
                                  tvk::Shader{"voxels/chunk.frag"}};
  pipelines_.push_back(&chunk_mesh_pipeline_);
  RegisterComputePipelines({{&raymarch_pipeline_, "voxels/raymarch_voxel.comp"},
                            {&chunk_cull_pipeline_, "voxels/chunk_cull.comp"}});
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
  pc.aabb_min = vec4(vec3(-0.5), 0);
  pc.aabb_max = vec4(vec3(0.5), 0);
  pc.time = scene_data_->time;
  pc.cam_dir = scene_data_->cam_dir;
  pc.cam_pos = scene_data_->cam_pos;
  static DescriptorBuilder b;
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
  TracyVkZone(graphics_queue_ctx_, cmd, "DrawChunks");
  auto& mgr = ChunkMeshManager::Get();

  if (!mgr.chunk_quad_buffer_.draw_cmds_count) {
    return;
  }
  auto& quad_buf = mgr.chunk_quad_buffer_.quad_gpu_buf;
  if (!quad_buf.buffer) {
    return;
  }
  chunk_mesh_pipeline_.BindGraphics(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          chunk_mesh_pipeline_.pipeline->layout, 0, 1, &scene_data_set, 0, nullptr);

  auto info = ChunkMeshManager::Get().chunk_quad_buffer_.quad_gpu_buf.GetInfo();
  auto uniform_buf_info = mgr.chunk_uniform_gpu_buf_.GetInfo();
  static DescriptorBuilder b;
  VkDescriptorSet quad_set =
      b.Begin(VK_SHADER_STAGE_VERTEX_BIT)
          .WriteBuffer(0, &info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .WriteBuffer(1, &uniform_buf_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
          .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          chunk_mesh_pipeline_.pipeline->layout, 1, 1, &quad_set, 0, nullptr);
  vkCmdBindIndexBuffer(cmd, ChunkMeshManager::Get().quad_index_buf_.buffer, 0,
                       VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexedIndirectCount(cmd, mgr.chunk_quad_buffer_.draw_cmd_gpu_buf.buffer, 0,
                                mgr.chunk_quad_buffer_.draw_count_buffer.buffer, 0,
                                mgr.chunk_quad_buffer_.draw_cmds_count * 6,
                                sizeof(VkDrawIndexedIndirectCommand));
}

void VoxelRenderer::UpdateSceneDataUBO() {}

void VoxelRenderer::PrepareAndCullChunks(VkCommandBuffer cmd) {
  ZoneScoped;
  // TODO:
  // https://github.com/SaschaWillems/Vulkan/blob/master/examples/computecullandlod/computecullandlod.cpp
  static AutoCVarInt frustum_cull("chunks.frustum_cull", "GPU Frustum Cull Enabled", 1,
                                  CVarFlags::EditCheckbox);
  static AutoCVarInt freeze_cull("chunks.freeze_cull", "Pause Culling", 0, CVarFlags::EditCheckbox);

  auto& chunk_vert_pool = ChunkMeshManager::Get().chunk_quad_buffer_;
  {
    TracyVkZone(graphics_queue_ctx_, cmd, "CopyDrawsToGPU");
    {
      VkBufferMemoryBarrier2 buffer_barriers[] = {
          BufferBarrier(chunk_vert_pool.draw_infos_gpu_buf.buffer, graphics_queue_family_,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_WRITE_BIT),
          BufferBarrier(chunk_vert_pool.draw_infos_staging.buffer, graphics_queue_family_,
                        VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT),
      };
      PipelineBarrier(cmd, 0, buffer_barriers, {});
    }
    ChunkMeshManager::Get().CopyDrawBuffers();
    chunk_vert_pool.CopyDrawsStagingToGPU(cmd);

    // VkBufferCopy copy{};
    // copy.size = chunk_vert_pool.draw_infos_staging.size;
    // vkCmdCopyBuffer(cmd, chunk_vert_pool.draw_infos_staging.buffer,
    //                 chunk_vert_pool.draw_infos_gpu_buf.buffer, 1, &copy);
  }
  TracyVkZone(graphics_queue_ctx_, cmd, "ChunkCull");
  // write to the draws buf

  // clear draw count compute buffer
  {
    VkBufferMemoryBarrier2 buffer_barriers[] = {
        BufferBarrier(chunk_vert_pool.draw_count_buffer.buffer, graphics_queue_family_,
                      VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT),
    };
    PipelineBarrier(cmd, 0, buffer_barriers, {});
  }
  if (!freeze_cull.Get()) {
    vkCmdFillBuffer(cmd, chunk_vert_pool.draw_count_buffer.buffer, 0, VK_WHOLE_SIZE, 0);
  }

  auto draw_info_info = chunk_vert_pool.draw_infos_gpu_buf.GetInfo();
  auto draw_cmds_info = chunk_vert_pool.draw_cmd_gpu_buf.GetInfo();
  auto draw_count_info = chunk_vert_pool.draw_count_buffer.GetInfo();
  auto chunk_uniforms_info = ChunkMeshManager::Get().chunk_uniform_gpu_buf_.GetInfo();
  VkBufferMemoryBarrier2 buffer_barriers[] = {
      BufferBarrier(chunk_vert_pool.draw_infos_gpu_buf.buffer, graphics_queue_family_,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
      BufferBarrier(chunk_vert_pool.draw_cmd_gpu_buf.buffer, graphics_queue_family_,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT),
      BufferBarrier(ChunkMeshManager::Get().chunk_uniform_gpu_buf_.buffer, graphics_queue_family_,
                    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_WRITE_BIT),
      BufferBarrier(chunk_vert_pool.draw_count_buffer.buffer, graphics_queue_family_,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
  };
  PipelineBarrier(cmd, 0, buffer_barriers, {});

  struct PC {
    vec4 cam_pos;
    vec4 plane0;
    vec4 plane1;
    vec4 plane2;
    vec4 plane3;
    vec4 plane4;
    vec4 plane5;
    uvec4 bits;
  };
  auto& frustum = frustum_cpu_.data;
  PC pc{vec4(scene_data_->cam_pos, 1.0),
        frustum[0],
        frustum[1],
        frustum[2],
        frustum[3],
        frustum[4],
        frustum[5],
        uvec4{0}};
  pc.bits.x |= (frustum_cull.Get() != 0) << 1;
  pc.bits.x |= (freeze_cull.Get() != 0) << 2;
  pc.bits.w = chunk_vert_pool.draw_cmd_allocator.Allocs().size();

  vkCmdPushConstants(cmd, chunk_cull_pipeline_.pipeline->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(PC), &pc);

  static DescriptorBuilder b;
  VkDescriptorSet s = b.Begin(VK_SHADER_STAGE_COMPUTE_BIT)
                          .WriteBuffer(0, &draw_info_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                          .WriteBuffer(1, &draw_cmds_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                          .WriteBuffer(2, &chunk_uniforms_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                          .WriteBuffer(3, &draw_count_info, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                          .Build(device_, set_cache_, GetCurrentFrame().frame_descriptors);
  chunk_cull_pipeline_.BindCompute(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          chunk_cull_pipeline_.pipeline->layout, 0, 1, &s, 0, nullptr);

  vkCmdDispatch(cmd, (chunk_vert_pool.draw_cmd_allocator.Allocs().size() + 63) / 64, 1, 1);
  {
    VkBufferMemoryBarrier2 buffer_barriers[] = {
        BufferBarrier(chunk_vert_pool.draw_cmd_gpu_buf.buffer, graphics_queue_family_,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT),
        BufferBarrier(ChunkMeshManager::Get().chunk_uniform_gpu_buf_.buffer, graphics_queue_family_,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
        BufferBarrier(chunk_vert_pool.draw_count_buffer.buffer, graphics_queue_family_,
                      VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                      VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT),
    };
    PipelineBarrier(cmd, 0, buffer_barriers, {});
  }
}
