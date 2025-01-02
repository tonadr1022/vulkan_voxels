#pragma once

struct ChunkMeshUpload {
  ivec3 pos;
  uint32_t counts[6];
  void* data;
  uint32_t tot_cnt;
};
