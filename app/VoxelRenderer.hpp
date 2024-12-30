#pragma once

#include "StagingBufferPool.hpp"
#include "application/Renderer.hpp"
#include "tvk/Pipeline.hpp"

struct SceneData {
  vec3 cam_dir;
  vec3 cam_pos;
  float time;
};

struct AABB {
  vec3 min;
  vec3 max;
};

struct VSettings {
  AABB aabb;
};

struct VoxelRenderer : public Renderer {
  VoxelRenderer();
  void Init(Window* window) override;
  void Draw(const SceneData* scene_data, bool draw_imgui);

  void InitPipelines() override;

  VSettings vsettings;
  void DrawImGui() override;

 private:
  friend class ChunkMeshManager;
  void DrawChunks(VkCommandBuffer cmd, tvk::AllocatedImage& img);
  void DrawRayMarchCompute(VkCommandBuffer cmd, tvk::AllocatedImage& img);
  void Draw(bool draw_imgui) override;
  const SceneData* scene_data_;
  StagingBufferPool staging_buffer_pool_;
  tvk::Pipeline raymarch_pipeline_;
  tvk::Pipeline chunk_mesh_pipeline_;
  uvec2 draw_dims_;
};
