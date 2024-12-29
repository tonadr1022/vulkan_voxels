#pragma once

struct MeshData {
  uint64_t* opaque_mask;
  uint64_t* face_masks;
  int vertex_count;
  int max_vertices;
  std::vector<uint64_t>* vertices;
  std::array<int, 6> face_vertex_begin;
  std::array<int, 6> face_vertex_length;
};
