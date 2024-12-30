#include "Terrain.hpp"

#include "FastNoise/FastNoise.h"

namespace gen {

FBMNoise::FBMNoise(float frequency, int octaves) : frequency_(frequency), octaves_(octaves) {
  InitNoise();
}

FBMNoise::FBMNoise(int seed) : frequency_(DefaultFrequency), octaves_(DefaultOctaves), seed_(seed) {
  InitNoise();
}

void FBMNoise::InitNoise() {
  auto fn_simplex = FastNoise::New<FastNoise::Simplex>();
  noise_ = FastNoise::New<FastNoise::FractalFBm>();
  noise_->SetSource(fn_simplex);
  noise_->SetOctaveCount(octaves_);
}

void FBMNoise::GetNoise(std::span<float> out, uvec2 start, uvec2 size) {
  noise_->GenUniformGrid2D(out.data(), start.x, start.y, size.x, size.y, frequency_, seed_);
}

void FBMNoise::GetNoise(std::span<float> out, uvec3 start, uvec3 size) {
  noise_->GenUniformGrid3D(out.data(), start.x, start.y, start.z, size.x, size.y, size.z,
                           frequency_, seed_);
}

void NoiseToHeights(std::span<float> noise, std::span<int> heights, uvec2 range) {
  uint32_t span = range.y - range.x;
  for (size_t i = 0; i < noise.size(); i++) {
    heights[i] = std::floor((noise[i] + 1.0) * 0.5 * span + range.x);
  }
}

void FillSphere(Grid3D<i8vec3{PCS}>& grid, PaddedChunkMask& mask) {
  int r = CS / 2;
  auto& data = grid.grid;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      for (int z = -r; z < r; z++) {
        int type = 0;
        if (glm::distance(glm::vec3(x, y, z), glm::vec3(0)) < r) {
          type = 1;
          mask.Set(x + r, y + r, z + r);
        }
        SetZXY<i8vec3{PCS}>(data, x + r, y + r, z + r, type);
      }
    }
  }
}

void FillChunk(PaddedChunkGrid3D& grid, std::span<int> heights, int value) {
  for (int y = 0; y < PaddedChunkGrid3D::Dims.x; y++) {
    for (int x = 0; x < PaddedChunkGrid3D::Dims.y; x++) {
      for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
        int fill = 0;
        if (y < heights[PaddedChunkGrid3D::Dims.z * x + z]) {
          fill = value;
        }
        grid.Set(x, y, z, fill);
      }
    }
  }
}

}  // namespace gen
