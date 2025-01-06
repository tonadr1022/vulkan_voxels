#pragma once

#include <cstddef>
#include <span>

#include "Mask.hpp"

struct MeshAlgData {
  std::array<uint64_t, static_cast<std::size_t>(CS2 * 6)> face_masks;
  std::array<uint8_t, CS2> forward_merged;
  std::array<uint8_t, CS> right_merged;
  std::array<int, 6> face_vertices_start_indices{};
  std::array<int, 6> face_vertex_lengths{};
  PaddedChunkMask* mask{};
  int max_vertices{};
};

struct MesherOutputData {
  std::vector<uint8_t> vertices;
  int vertex_cnt{};
  float mesh_time;
};

void GenerateMesh(std::span<uint8_t> voxels, MeshAlgData& alg_data, MesherOutputData& mesh_data);
