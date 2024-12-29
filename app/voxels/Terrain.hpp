#pragma once

#include "FastNoise/Generators/Fractal.h"
#include "voxels/Common.hpp"

namespace gen {

void FillSphere(ChunkGrid& grid);
struct FBMNoise {
  static constexpr float DefaultFrequency{0.06};
  static constexpr int DefaultOctaves{5};

  FBMNoise();
  FBMNoise(float frequency, int octaves);
  void Generate(ChunkGrid& voxels, OpaqueMask& opaque_mask, uint64_t offset_x, uint64_t offset_z,
                int seed);
  void Generate(ChunkGrid& voxels, OpaqueMask& opaque_mask, int seed);

 private:
  void InitNoise();
  float frequency_;
  int octaves_;
  FastNoise::SmartNode<FastNoise::FractalFBm> noise_;
};

}  // namespace gen
