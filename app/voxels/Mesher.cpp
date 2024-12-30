#include "Mesher.hpp"

#include <cstddef>

// https://github.com/cgerikj/binary-greedy-meshing/tree/master

namespace {

inline uint64_t EncodeQuad(const uint64_t x, const uint64_t y, const uint64_t z, const uint64_t w,
                           const uint64_t h, const uint64_t type) {
  return (type << 32) | (h << 24) | (w << 18) | (z << 12) | (y << 6) | (x);
}

inline int AxisIndex(const int axis, const int a, const int b, const int c) {
  if (axis == 0) return b + (a * PCS) + (c * PCS2);
  if (axis == 1) return b + (c * PCS) + (a * PCS2);
  return c + (a * PCS) + (b * PCS2);
}

inline void InsertQuad(std::vector<uint64_t>& vertices, uint64_t quad, int& i_vertex,
                       int& vertex_capacity) {
  if (i_vertex >= vertex_capacity - 6) {
    vertex_capacity *= 2;
    vertices.resize(static_cast<size_t>(vertex_capacity), 0);
  }
  vertices[i_vertex] = quad;
  i_vertex++;
}

}  // namespace

void GenerateMesh(std::span<uint8_t> voxels, MeshAlgData& alg_data, MeshData& mesh_data) {
  mesh_data.vertex_cnt = 0;
  alg_data.max_vertices = mesh_data.vertices->size();
  const auto& opaque_mask = mesh_data.mask->mask;
  auto& forward_merged = alg_data.forward_merged;
  auto& right_merged = alg_data.right_merged;
  int i_vertex{0};
  auto& face_masks = alg_data.face_masks;
  // hidden face culling via bitmasks
  for (int a = 1; a < PCS - 1; a++) {
    const int a_pcs = a * PCS;
    for (int b = 1; b < PCS - 1; b++) {
      // extract the column mask from the opaque mask, (sets the pad bits of column to 0)
      const uint64_t column_bits = opaque_mask[(a_pcs) + b] & (~(1ull << 63 | 1));
      // convert indices back to unpadded dimensions
      const int face_mask_ba_idx = (b - 1) + (a - 1) * CS;
      const int face_mask_ab_idx = (a - 1) + (b - 1) * CS;

      // shift mask in each direction flip the bits, and with the column to get whether
      // faces should be hidden
      face_masks[face_mask_ba_idx + 0 * CS2] = (column_bits & ~opaque_mask[a_pcs + PCS + b]) >> 1;
      face_masks[face_mask_ba_idx + 1 * CS2] = (column_bits & ~opaque_mask[a_pcs - PCS + b]) >> 1;

      face_masks[face_mask_ab_idx + 2 * CS2] = (column_bits & ~opaque_mask[a_pcs + (b + 1)]) >> 1;
      face_masks[face_mask_ab_idx + 3 * CS2] = (column_bits & ~opaque_mask[a_pcs + (b - 1)]) >> 1;

      face_masks[face_mask_ab_idx + 4 * CS2] = (column_bits & ~(opaque_mask[a_pcs + b]) >> 1);
      face_masks[face_mask_ab_idx + 5 * CS2] = (column_bits & ~(opaque_mask[a_pcs + b]) << 1);
    }
  }

  // greedy face merging. split the first four faces into a separate loop since the planes are
  // different
  for (int face = 0; face < 4; face++) {
    const int axis = face / 2;
    const int face_vertex_begin = i_vertex;
    // iterate over each layer in the chunk
    for (int layer = 0; layer < CS; layer++) {
      const int bits_loc = layer * CS + face * CS2;
      // iterate over each row/col
      for (int forward = 0; forward < CS; forward++) {
        // get the current row/col bits
        uint64_t bits_here = face_masks[forward + bits_loc];
        // if nothing is here, continue
        if (bits_here == 0) continue;

        // get the next row/col bits, only exists if theres an adjacent row/col to check
        const uint64_t bits_next = forward + 1 < CS ? face_masks[(forward + 1) + bits_loc] : 0;

        // track the number of faces merged to the right, always at least 1
        uint8_t right_merged_cnt = 1;
        // iterate until nothing left
        while (bits_here) {
          uint64_t bit_pos;
          // use trailing 0s for first visible voxel in row (first non zero bit idx)
#ifdef _MSC_VER
          _BitScanForward64(&bit_pos, bits_here);
#else
          bit_pos = __builtin_ctzll(bits_here);
#endif
          const uint8_t type = voxels[AxisIndex(axis, forward + 1, bit_pos + 1, layer + 1)];
          uint8_t& forward_merged_ref = forward_merged[bit_pos];
          // if the forward voxel is the same and opaque, merge and continue
          if ((bits_next >> bit_pos & 1) &&
              type == voxels[AxisIndex(axis, forward + 2, bit_pos + 1, layer + 1)]) {
            // mark as merged
            forward_merged_ref++;
            // clear the bit to not mesh it again
            bits_here &= ~(1ull << bit_pos);
            continue;
          }
          // expand to the right as far as possible. must be the same voxel type and
          // length
          for (int right = bit_pos + 1; right < CS; right++) {
            if (!(bits_here >> right & 1) || forward_merged_ref != forward_merged[right] ||
                type != voxels[AxisIndex(axis, forward + 1, right + 1, layer + 1)]) {
              break;
            }
            // mark as merged and inc right merged count
            forward_merged[right] = 0;
            right_merged_cnt++;
          }
          bits_here &= ~((1ull << (bit_pos + right_merged_cnt)) - 1);

          // calculate dimensions
          const uint8_t mesh_front = forward - forward_merged_ref;
          const uint8_t mesh_left = bit_pos;
          const uint8_t mesh_up = layer + (~face & 1);

          const uint8_t mesh_width = right_merged_cnt;
          const uint8_t mesh_length = forward_merged_ref + 1;

          // reset
          forward_merged_ref = 0;
          right_merged_cnt = 1;

          // encode and insert quad
          uint64_t quad;
          switch (face) {
            case 0:
            case 1:
              quad = EncodeQuad(mesh_front + (face == 1 ? mesh_length : 0), mesh_up, mesh_left,
                                mesh_length, mesh_width, type);
              break;
            default:  // 2,3
              quad = EncodeQuad(mesh_up, mesh_front + (face == 2 ? mesh_length : 0), mesh_left,
                                mesh_length, mesh_width, type);
          }
          InsertQuad(*mesh_data.vertices, quad, i_vertex, alg_data.max_vertices);
        }
      }
    }
    alg_data.face_vertices_start_indices[face] = face_vertex_begin;
    alg_data.face_vertex_lengths[face] = i_vertex - face_vertex_begin;
  }
  for (int face = 4; face < 6; face++) {
    const int axis = face / 2;
    const int face_vertex_begin = i_vertex;
    for (int forward = 0; forward < CS; forward++) {
      const int bits_loc = forward * CS + face * CS2;
      const int bits_forward_loc = (forward + 1) * CS + face * CS2;

      for (int right = 0; right < CS; right++) {
        // get the current row/col bits
        uint64_t bits_here = face_masks[right + bits_loc];
        // if nothing is here, continue
        if (bits_here == 0) continue;

        const uint64_t bits_forward = forward < CS - 1 ? face_masks[right + bits_forward_loc] : 0;
        const uint64_t bits_right = right < CS - 1 ? face_masks[right + 1 + bits_loc] : 0;
        const int right_cs = right * CS;

        while (bits_here) {
          uint64_t bit_pos;
          // use trailing 0s for first visible voxel in row (first non zero bit idx)
#ifdef _MSC_VER
          _BitScanForward64(&bit_pos, bits_here);
#else
          bit_pos = __builtin_ctzll(bits_here);
#endif

          bits_here &= ~(1ull << bit_pos);
          const uint8_t type = voxels[AxisIndex(axis, right + 1, forward + 1, bit_pos)];
          uint8_t& forward_merged_ref = forward_merged[right_cs + (bit_pos - 1)];
          uint8_t& right_merged_ref = right_merged[bit_pos - 1];
          if (right_merged_ref == 0 && (bits_forward >> bit_pos & 1) &&
              type == voxels[AxisIndex(axis, right + 1, forward + 2, bit_pos)]) {
            forward_merged_ref++;
            continue;
          }
          if ((bits_right >> bit_pos & 1) &&
              forward_merged_ref == forward_merged[(right_cs + CS) + (bit_pos - 1)] &&
              type == voxels[AxisIndex(axis, right + 2, forward + 1, bit_pos)]) {
            forward_merged_ref = 0;
            right_merged_ref++;
            continue;
          }

          // calculate dimensions
          const uint8_t mesh_left = right - forward_merged_ref;
          const uint8_t mesh_front = forward - forward_merged_ref;
          const uint8_t mesh_up = bit_pos - 1 + (~face & 1);

          const uint8_t mesh_width = 1 + right_merged_ref;
          const uint8_t mesh_length = 1 + forward_merged_ref;
          // reset
          forward_merged_ref = 0;
          right_merged_ref = 0;

          const uint64_t quad = EncodeQuad(mesh_left + (face == 4 ? mesh_width : 0), mesh_front,
                                           mesh_up, mesh_width, mesh_length, type);
          InsertQuad(*mesh_data.vertices, quad, i_vertex, alg_data.max_vertices);
        }
      }
    }
    alg_data.face_vertices_start_indices[face] = face_vertex_begin;
    alg_data.face_vertex_lengths[face] = i_vertex - face_vertex_begin;
  }
  mesh_data.vertex_cnt = i_vertex + 1;
}
