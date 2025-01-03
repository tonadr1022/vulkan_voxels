#pragma once

#include "voxels/Common.hpp"

struct PaddedChunkMask {
  std::array<uint64_t, PCS2> mask;
  void SetXZY(int x, int y, int z) { mask[(PCS * y) + z] |= 1ull << (x); }
  void SetXZY(int x, int y, int z, bool v) {
    uint64_t bitmask = 1ull << x;
    mask[(PCS * y) + z] ^= (-static_cast<int64_t>(v) ^ mask[(PCS * y) + z]) & bitmask;
  }
  [[nodiscard]] bool AnySolid() const {
    for (const uint64_t& e : mask) {
      if (e != 0) return true;
    }
    return false;
  }
  void ClearXZY(int x, int y, int z) { mask[(PCS * y) + z] &= ~(1ull << (x)); }
  [[nodiscard]] bool Test(int x, int y, int z) const {
    return (mask[(PCS * y) + z] & (1ull << x)) != 0;
  }
};
