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

void FBMNoise::GetNoise(std::span<float> out, uvec2 start, uvec2 size) {
  fbm->GenUniformGrid2D(out.data(), start.x, start.y, size.x, size.y, frequency_, seed_);
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

void FBMNoise::GetWhiteNoise(std::span<float> out, uvec2 start, uvec2 size) {
  white_noise->GenUniformGrid2D(out.data(), start.x, start.y, size.x, size.y, white_freq_, seed_);
}

}  // namespace gen
