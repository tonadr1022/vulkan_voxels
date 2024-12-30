#pragma once

#include <span>

#include "FastNoise/Generators/Fractal.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Grid3D.hpp"
#include "voxels/Mask.hpp"

namespace gen {

template <i8vec3 Len>
void FillSphere(Grid3D<Len>& grid) {
  int r = CS / 2;
  auto& data = grid.grid;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      for (int z = -r; z < r; z++) {
        int type = 0;
        if (glm::distance(glm::vec3(x, y, z), glm::vec3(0)) < r) {
          type = 1;
        }
        SetZXY<Len>(data, x + r, y + r, z + r, type);
      }
    }
  }
}

void FillSphere(Grid3D<i8vec3{PCS}>& grid, PaddedChunkMask& mask);

void NoiseToHeights(std::span<float> noise, std::span<int> heights, uvec2 range);

template <auto Len>
void FillChunk(Grid3D<Len>& grid, std::span<int> heights, int value) {
  auto& data = grid.grid;
  for (int y = 0; y < Len.y; y++) {
    for (int x = 0; x < Len.x; x++) {
      for (int z = 0; z < Len.z; z++) {
        int fill = glm::mix(0, value, y < heights[Len.z * x + z]);
        data[ZXY<Len>(x, y, z)] = fill;
      }
    }
  }
}

void FillChunk(PaddedChunkGrid3D& grid, std::span<int> heights, int value);

struct FBMNoise {
  static constexpr float DefaultFrequency{0.06};
  static constexpr int DefaultOctaves{5};

  explicit FBMNoise(int seed);
  FBMNoise(float frequency, int octaves);
  void GetNoise(std::span<float> out, uvec2 start, uvec2 size);
  void GetNoise(std::span<float> out, uvec3 start, uvec3 size);

  template <i8vec3 Dims>
  void FillNoise(HeightMapFloats<Dims>& out, uvec2 start) {
    noise_->GenUniformGrid2D(out.data(), start.x, start.y, Dims.x, Dims.z, frequency_, seed_);
  }

 private:
  void InitNoise();
  float frequency_;
  int octaves_;
  int seed_;
  FastNoise::SmartNode<FastNoise::FractalFBm> noise_;
};

}  // namespace gen
