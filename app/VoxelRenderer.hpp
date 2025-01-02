#pragma once

#include "StagingBufferPool.hpp"
#include "application/Renderer.hpp"
#include "tvk/Pipeline.hpp"

struct SceneData {
  vec3 cam_dir;
  vec3 cam_pos;
  vec3 sun_dir;
  vec3 sun_color;
  vec3 ambient_color;
  float time;
};

struct AABB {
  vec3 min;
  vec3 max;
};

struct VSettings {
  AABB aabb;
};

struct SceneDataUBO {
  mat4 view;
  mat4 proj;
  mat4 viewproj;
  mat4 world_center_view;
  mat4 world_center_viewproj;
  ivec4 view_pos_int;
  vec4 sun_dir;
  vec4 sun_color;
  vec4 cam_dir;
  vec3 ambient_color;
};

struct VoxelRenderer : public Renderer {
  VoxelRenderer();
  void Init(Window* window) override;
  void Draw(const SceneData* scene_data, bool draw_imgui);

  void InitPipelines() override;

  VSettings vsettings;
  void DrawImGui() override;

 private:
  struct ExtendedFrameData {
    tvk::AllocatedBuffer scene_data_ubo_buffer;
  };
  ExtendedFrameData extented_frame_data_[FrameOverlap];
  ExtendedFrameData& GetExtendedFrameData() {
    return extented_frame_data_[frame_num_ % FrameOverlap];
  }
  void UpdateSceneDataUBO();
  void PrepareAndCullChunks(VkCommandBuffer cmd);
  void DrawChunks(VkDescriptorSet scene_data_set, VkCommandBuffer cmd);
  friend class ChunkMeshManager;
  void DrawChunks(VkCommandBuffer cmd, tvk::AllocatedImage& img);
  void DrawRayMarchCompute(VkCommandBuffer cmd, tvk::AllocatedImage& img);
  void Draw(bool draw_imgui) override;
  const SceneData* scene_data_;
  StagingBufferPool staging_buffer_pool_;
  tvk::Pipeline chunk_cull_pipeline_;
  tvk::Pipeline raymarch_pipeline_;
  tvk::Pipeline chunk_mesh_pipeline_;
  uvec2 draw_dims_;
};
