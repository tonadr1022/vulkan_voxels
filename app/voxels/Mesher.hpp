#pragma once

#include <cstddef>
#include <span>

#include "Mask.hpp"

struct MeshAlgData {
  std::array<uint64_t, static_cast<std::size_t>(CS2 * 6)> face_masks;
  std::array<uint8_t, CS2> forward_merged;
  std::array<uint8_t, CS> right_merged;
  std::array<int, 6> face_vertices_start_indices;
  std::array<int, 6> face_vertex_lengths;
  int max_vertices{};
};

struct MeshData {
  std::vector<uint64_t> vertices;
  PaddedChunkMask* mask{};
  int vertex_cnt{};
};

void GenerateMesh(std::span<uint8_t> voxels, MeshAlgData& alg_data, MeshData& mesh_data);
