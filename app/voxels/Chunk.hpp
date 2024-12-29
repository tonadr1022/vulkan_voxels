#pragma once

#include "voxels/Common.hpp"

struct Grid3D {
  ChunkGrid grid;
  void Set(int x, int y, int z, uint8_t value) { ::Set(grid, x, y, z, value); }
  uint8_t Get(int x, int y, int z) { return ::Get(grid, x, y, z); }
};
