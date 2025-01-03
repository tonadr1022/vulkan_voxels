#pragma once

struct ChunkMeshUpload {
  ivec3 pos;
  uint32_t counts[6];
  uint32_t staging_copy_idx;
};
