#pragma once

#include <span>

#include "FastNoise/Generators/BasicGenerators.h"
#include "FastNoise/Generators/Fractal.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Grid3D.hpp"

namespace gen {

template <i8vec3 Len, typename Func>
void FillSphereArgs(PaddedChunkGrid3D& grid, Func&& func) {
  static_assert(std::is_invocable_v<Func, int, int, int> && "Func not invocable");
  int r = CS / 2;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      for (int z = -r; z < r; z++) {
        uint8_t type = 0;
        if (glm::distance(glm::vec3(x, y, z), glm::vec3(0)) < r) {
          type = func(x, y, z);
        }
        grid.Set(x + r, y + r, z + r, type);
      }
    }
  }
}
template <i8vec3 Len, typename Func>
void FillSphere(PaddedChunkGrid3D& grid, Func&& func) {
  static_assert(std::is_invocable_v<Func> && "Func not invocable");
  int r = CS / 2;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      for (int z = -r; z < r; z++) {
        uint8_t type = 0;
        if (glm::distance(glm::vec3(x, y, z), glm::vec3(0)) < r) {
          type = func();
        }
        grid.Set(x + r, y + r, z + r, type);
      }
    }
  }
}

void NoiseToHeights(std::span<float> noise, std::span<int> heights, uvec2 range);

template <auto Len>
void FillChunk(Grid3D<Len>& grid, std::span<int> heights, int value) {
  auto& data = grid.grid;
  for (int y = 0; y < Len.y; y++) {
    for (int x = 0; x < Len.x; x++) {
      for (int z = 0; z < Len.z; z++) {
        int fill = glm::mix(0, value, y < heights[(Len.z * x) + z]);
        data[ZXY<Len>(x, y, z)] = fill;
      }
    }
  }
}

void FillSolid(PaddedChunkGrid3D& grid, int value);

// void FillChunk(PaddedChunkGrid3D& grid, std::span<int> heights, int value);

template <typename Func>
void FillChunk(PaddedChunkGrid3D& grid, [[maybe_unused]] ivec3 chunk_start, std::span<int> heights,
               Func&& func) {
  ZoneScoped;
  static_assert(std::is_invocable_v<Func, int, int, int> && "Func not invocable");
  for (int y = 0; y < PaddedChunkGrid3D::Dims.x; y++) {
    for (int x = 0; x < PaddedChunkGrid3D::Dims.y; x++) {
      for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
        // int fill = 0;
        if (y < heights[(PaddedChunkGrid3D::Dims.x * x) + z] - chunk_start.y) {
          // fill = func(x, y, z);
          grid.Set(x, y, z, func(x, y, z));
        }
      }
    }
  }
}

struct FBMNoise {
  static constexpr float DefaultFrequency{0.06};
  static constexpr int DefaultOctaves{5};
  void Init(int seed, float frequency, int octaves);
  void GetNoise(std::span<float> out, uvec2 start, uvec2 size);
  void GetWhiteNoise(std::span<float> out, uvec2 start, uvec2 size);

  template <i8vec3 Dims>
  void FillWhiteNoise(HeightMapFloats<Dims>& out, uvec2 start) {
    white_noise->GenUniformGrid2D(out.data(), start.x, start.y, Dims.x, Dims.z, frequency_, seed_);
  }
  template <i8vec3 Dims>
  void FillWhiteNoise(FloatArray3D<Dims>& out, uvec3 start) {
    white_noise->GenUniformGrid3D(out.data(), start.x, start.y, start.z, Dims.x, Dims.y, Dims.z,
                                  frequency_, seed_);
  }
  template <i8vec3 Dims>
  void FillNoise2D(HeightMapFloats<Dims>& out, uvec2 start) {
    fbm->GenUniformGrid2D(out.data(), start.x, start.y, Dims.x, Dims.z, frequency_, seed_);
  }

  FastNoise::SmartNode<FastNoise::FractalFBm> fbm;
  FastNoise::SmartNode<FastNoise::White> white_noise;

 private:
  void InitNoise();
  float white_freq_;
  float frequency_;
  int octaves_;
  int seed_;
};

}  // namespace gen
