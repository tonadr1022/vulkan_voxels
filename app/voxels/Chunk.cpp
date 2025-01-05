#include "Chunk.hpp"

bool PaddedChunkGrid3D::ValidateBitmask() const {
  for (int y = 0; y < PaddedChunkGrid3D::Dims.y; y++) {
    for (int x = 0; x < PaddedChunkGrid3D::Dims.x; x++) {
      for (int z = 0; z < PaddedChunkGrid3D::Dims.z; z++) {
        if ((grid.GetZXY(x, y, z) != 0) != mask.TestZXY(x, y, z)) {
          return false;
        }
      }
    }
  }
  return true;
}
