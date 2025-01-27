#pragma once

#include "voxels/Grid3D.hpp"
#include "voxels/Mask.hpp"

struct PaddedChunkGrid3D {
  Grid3D<PCS> grid;
  PaddedChunkMask mask;
  static constexpr i8vec3 Dims = i8vec3{PCS};
  void Set(int x, int y, int z, uint8_t val) {
    mask.SetZXY(x, y, z, val);
    grid.SetZXY(x, y, z, val);
  }
  void Clear() {
    memset(mask.mask.data(), 0, sizeof(mask));
    memset(grid.grid.data(), 0, sizeof(grid.grid));
  }
  bool ValidateBitmask() const;
};

struct Chunk {
  Chunk() = default;
  explicit Chunk(ivec3 pos) : pos(pos) {}
  PaddedChunkGrid3D grid;
  ivec3 pos;
};
