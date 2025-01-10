#pragma once

#include <span>

#include "FastNoise/Generators/BasicGenerators.h"
#include "FastNoise/Generators/Fractal.h"
#include "pch.hpp"
#include "voxels/Chunk.hpp"
#include "voxels/Common.hpp"
#include "voxels/Grid3D.hpp"

namespace gen {

template <int Len, typename Func>
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
void FillVisibleCube(PaddedChunkGrid3D& grid, int gap, int val);

template <typename Func>
void FillVisibleCube(PaddedChunkGrid3D& grid, int gap, Func&& func) {
  static_assert(std::is_invocable_v<Func> && "Func not invocable");
  for (int y = 1 + gap; y < PCS - 1 - gap; y++) {
    for (int z = 1 + gap; z < PCS - 1 - gap; z++) {
      for (int x = 1 + gap; x < PCS - 1 - gap; x++) {
        grid.Set(x, y, z, func());
      }
    }
  }
}

template <int Len, typename Func>
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
void NoiseToHeights(std::span<float> noise, HeightMapData& data, uvec2 range);

int GetHeight(std::span<const float> noise, int x, int z, uvec2 range);
int GetHeight(std::span<const float> noise, int i, uvec2 range);

template <auto Len>
void FillChunk(Grid3D<Len>& grid, std::span<float> heights, int value) {
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
template <int Len>
void FillChunk(Grid3D<Len>& grid, std::span<int> heights, int value) {
  FillChunk(grid, heights, [value](int, int, int) { return value; });
}

void FillSolid(PaddedChunkGrid3D& grid, int value);

template <typename Func>
void FillChunk(PaddedChunkGrid3D& grid, [[maybe_unused]] ivec3 chunk_start,
               HeightMapData& height_data, Func&& func) {
  ZoneScoped;
  if (chunk_start.y + PaddedChunkGrid3D::Dims.y <= height_data.range.x ||
      chunk_start.y >= height_data.range.y) {
    return;
  }
  static_assert(std::is_invocable_v<Func, int, int, int>, "Func not invocable");

  for (int y = 0; y < PaddedChunkGrid3D::Dims.y; y++) {
    int i = 0;
    for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
      for (int x = 0; x < PaddedChunkGrid3D::Dims.x; x++, i++) {
        if (y < (height_data.heights[i] - chunk_start.y)) {
          grid.Set(x, y, z, func(x, y, z));
        }
      }
    }
  }
}

template <typename Func>
void FillChunk(PaddedChunkGrid3D& grid, [[maybe_unused]] ivec3 chunk_start,
               std::span<const int> heights, Func&& func) {
  ZoneScoped;
  static_assert(std::is_invocable_v<Func, int, int, int>, "Func not invocable");
  for (int y = 0; y < PaddedChunkGrid3D::Dims.y; y++) {
    for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
      for (int x = 0; x < PaddedChunkGrid3D::Dims.x; x++) {
        const int col_start = x * PaddedChunkGrid3D::Dims.z;
        const int i = col_start + z;
        if (y < (heights[i] - chunk_start.y)) {
          grid.Set(x, y, z, func(x, y, z));
        }
      }
    }
  }
}

template <typename Func>
void FillChunk(PaddedChunkGrid3D& grid, [[maybe_unused]] ivec3 chunk_start,
               std::span<const float> heights, uvec2 height_map_range, Func&& func) {
  ZoneScoped;
  std::vector<int> heights_computed(heights.size());
  for (size_t i = 0; i < heights.size(); i++) {
    heights_computed[i] = GetHeight(heights, i, height_map_range);
  }
  FillChunk(grid, chunk_start, heights_computed, func);
}

struct FBMNoise {
  static constexpr float DefaultFrequency{0.06};
  static constexpr int DefaultOctaves{5};
  void Init(int seed, float frequency, int octaves);
  void GetNoise(std::span<float> out, uvec2 start, uvec2 size) const;
  void GetWhiteNoise(std::span<float> out, uvec2 start, uvec2 size) const;

  template <int Len>
  void FillWhiteNoise(HeightMapFloats& out, uvec2 start) {
    white_noise->GenUniformGrid2D(out.data(), start.x, start.y, Len, Len, frequency_, seed_);
  }
  template <int Len>
  void FillWhiteNoise(FloatArray3D<Len>& out, uvec3 start) {
    white_noise->GenUniformGrid3D(out.data(), start.x, start.y, start.z, Len, Len, Len, frequency_,
                                  seed_);
  }
  void FillNoise2D(HeightMapFloats& out, uvec2 start, uvec2 dims, float scale) const {
    ZoneScoped;
    fbm->GenUniformGrid2D(out.data(), start.x, start.y, dims.x, dims.y, frequency_ * scale, seed_);
  }
  template <int Len>
  void FillNoise2D(HeightMapFloats& out, uvec2 start) {
    fbm->GenUniformGrid2D(out.data(), start.x, start.y, Len, Len, frequency_, seed_);
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
