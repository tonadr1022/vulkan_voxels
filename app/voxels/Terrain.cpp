#include "Terrain.hpp"

#include "FastNoise/FastNoise.h"
namespace gen {

void FillSphere(ChunkGrid& grid) {
  int r = CS / 2;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      for (int z = -r; z < r; z++) {
        int type = 0;
        if (glm::distance(glm::vec3(x, y, z), glm::vec3(0)) < r) {
          type = 1;
        }
        Set(grid, x + r, y + r, z + r, type);
      }
    }
  }
}

// void FBMNoise::Generate(ChunkGrid& voxels, OpaqueMask& opaque_mask, int seed) {
//   // Generate(voxels, opaque_mask, 0, 0, seed);
// }

// void FBMNoise::Generate(ChunkGrid& voxels, OpaqueMask& opaque_mask, uint64_t offset_x,
//                         uint64_t offset_z, int seed) {
//
// }

FBMNoise::FBMNoise(float frequency, int octaves) : frequency_(frequency), octaves_(octaves) {
  InitNoise();
}

FBMNoise::FBMNoise() : frequency_(DefaultFrequency), octaves_(DefaultOctaves) {}

void FBMNoise::InitNoise() {
  noise_ = FastNoise::New<FastNoise::FractalFBm>();
  noise_->SetOctaveCount(octaves_);
}

}  // namespace gen
