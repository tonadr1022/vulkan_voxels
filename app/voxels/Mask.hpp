#pragma once

#include "voxels/Common.hpp"

struct PaddedChunkMask {
  std::array<uint64_t, PCS2> mask;

  void Set(int x, int y, int z) { mask[PCS * y + x] |= 1ull << (z); }

  void Set(int x, int y, int z, bool v) {
    uint64_t bitmask = 1ull << z;
    mask[PCS * y + x] ^= (-static_cast<int64_t>(v) ^ mask[PCS * y + x]) & bitmask;
  }

  void Clear(int x, int y, int z) { mask[PCS * y + x] &= ~(1ull << (z)); }
  [[nodiscard]] bool Test(int x, int y, int z) const {
    return (mask[PCS * y + x] & (1ull << z)) != 0;
  }
};
