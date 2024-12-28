#pragma once

#include "application/Renderer.hpp"

struct VoxelRenderer : public Renderer {
  void Draw(bool draw_imgui) override;
};
