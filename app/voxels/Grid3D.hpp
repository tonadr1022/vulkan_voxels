#pragma once

#include "voxels/Common.hpp"

template <int Len>
struct Grid3D {
  Grid3Du8<Len> grid;
  void SetXZY(int x, int y, int z, uint8_t value) { ::SetXZY<Len>(grid, x, y, z, value); }
  [[nodiscard]] uint8_t GetXZY(int x, int y, int z) const { return ::GetXZY<Len>(grid, x, y, z); }
  void SetZXY(int x, int y, int z, uint8_t value) { ::SetZXY<Len>(grid, x, y, z, value); }
  [[nodiscard]] uint8_t GetZXY(int x, int y, int z) const { return ::GetZXY<Len>(grid, x, y, z); }
  static constexpr auto LenX = Len;
  static constexpr auto LenY = Len;
  static constexpr auto LenZ = Len;
};
