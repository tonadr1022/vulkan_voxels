#pragma once

#include "voxels/Common.hpp"

template <i8vec3 Len>
struct Grid3D {
  Grid3Du8<Len> grid;
  void SetZXY(int x, int y, int z, uint8_t value) { ::SetZXY<Len>(grid, x, y, z, value); }
  [[nodiscard]] uint8_t GetZXY(int x, int y, int z) const { return ::GetZXY<Len>(grid, x, y, z); }
  static constexpr auto LenX = Len.x;
  static constexpr auto LenY = Len.y;
  static constexpr auto LenZ = Len.z;
};
