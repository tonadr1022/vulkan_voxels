#include "Terrain.hpp"

#include "FastNoise/FastNoise.h"

namespace gen {

void FBMNoise::Init(int seed, float frequency, int octaves) {
  frequency_ = frequency;
  white_freq_ = frequency;
  seed_ = seed;
  octaves_ = octaves;
  InitNoise();
}

void FBMNoise::InitNoise() {
  auto fn_simplex = FastNoise::New<FastNoise::Simplex>();
  fbm = FastNoise::New<FastNoise::FractalFBm>();
  fbm->SetSource(fn_simplex);
  fbm->SetOctaveCount(octaves_);
  white_noise = FastNoise::New<FastNoise::White>();
}

void FBMNoise::GetNoise(std::span<float> out, uvec2 start, uvec2 size) const {
  fbm->GenUniformGrid2D(out.data(), start.x, start.y, size.x, size.y, frequency_, seed_);
}

void NoiseToHeights(std::span<float> noise, HeightMapData& data, uvec2 range) {
  uint32_t span = range.y - range.x;
  data.range.x = std::numeric_limits<int>::max();
  data.range.y = std::numeric_limits<int>::lowest();
  for (size_t i = 0; i < noise.size(); i++) {
    int h = std::floor(((noise[i] + 1.0) * 0.5 * span) + range.x);
    data.heights[i] = h;
    data.range.x = std::min(data.range.x, h);
    data.range.y = std::max(data.range.y, h);
  }
}

void NoiseToHeights(std::span<float> noise, std::span<int> heights, uvec2 range) {
  uint32_t span = range.y - range.x;
  for (size_t i = 0; i < noise.size(); i++) {
    heights[i] = std::floor(((noise[i] + 1.0) * 0.5 * span) + range.x);
  }
}

void FillChunk(PaddedChunkGrid3D& grid, std::span<int> heights, int value) {
  for (int y = 0; y < PaddedChunkGrid3D::Dims.x; y++) {
    for (int x = 0; x < PaddedChunkGrid3D::Dims.y; x++) {
      for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
        int fill = 0;
        if (y < heights[(PaddedChunkGrid3D::Dims.x * z) + x]) {
          fill = value;
        }
        grid.Set(x, y, z, fill);
      }
    }
  }
}

void FillSolid(PaddedChunkGrid3D& grid, int value) {
  // TODO: optimize, this is awful
  for (int y = 0; y < PaddedChunkGrid3D::Dims.x; y++) {
    for (int x = 0; x < PaddedChunkGrid3D::Dims.y; x++) {
      for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
        grid.Set(x, y, z, value);
      }
    }
  }
}

void FBMNoise::GetWhiteNoise(std::span<float> out, uvec2 start, uvec2 size) const {
  white_noise->GenUniformGrid2D(out.data(), start.x, start.y, size.x, size.y, white_freq_, seed_);
}

int GetHeight(std::span<const float> noise, int i, uvec2 range) {
  return std::floor(((noise[i] + 1.0) * 0.5 * (range.y - range.x)) + range.x);
}
int GetHeight(std::span<const float> noise, int x, int z, uvec2 range) {
  return std::floor(((noise[x * PCS + z] + 1.0) * 0.5 * (range.y - range.x)) + range.x);
}
}  // namespace gen
