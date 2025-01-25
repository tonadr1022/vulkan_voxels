#pragma once

struct ChunkMeshUpload {
  ivec3 pos;
  int mult{1};
  uint32_t vert_counts[6];
  uint32_t staging_copy_idx;
  bool stale{false};
};
