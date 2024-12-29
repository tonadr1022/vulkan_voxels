#pragma once

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
  void Draw(const SceneData* scene_data, bool draw_imgui);

  void InitPipelines() override;

  VSettings vsettings;
  void DrawImGui() override;

 private:
  void DrawRayMarchCompute();
  void Draw(bool draw_imgui) override;
  const SceneData* scene_data_;
  tvk::Pipeline voxel_raster_pipeline_;
  uvec2 draw_dims_;
};
