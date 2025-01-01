#pragma once

struct ChunkMeshUpload {
  ivec3 pos;
  uint32_t count;
  uint32_t first_instance;
  void* data;
};
