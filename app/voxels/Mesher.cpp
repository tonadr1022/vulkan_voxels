#include "Mesher.hpp"

#include "application/Timer.hpp"

// https://github.com/cgerikj/binary-greedy-meshing/tree/master

namespace {

inline int AxisIndex(const int axis, const int a, const int b, const int c) {
  if (axis == 0) return b + (a * PCS) + (c * PCS2);
  if (axis == 1) return b + (c * PCS) + (a * PCS2);
  return c + (a * PCS) + (b * PCS2);
}

#ifdef PACK_QUAD
struct Quad {
  uint8_t data[5];
};
inline void EncodeQuad(Quad& q, const uint64_t x, const uint64_t y, const uint64_t z,
                       const uint64_t w, const uint64_t h, const uint64_t type) {
  q.data[0] = static_cast<uint8_t>((x) | ((y) << 6));
  q.data[1] = static_cast<uint8_t>((y >> 2) | ((z) << 4));
  q.data[2] = static_cast<uint8_t>((z >> 4) | ((w) << 2));
  q.data[3] = static_cast<uint8_t>(w >> 6 | (h));
  q.data[4] = static_cast<uint8_t>(type);
}
inline void InsertQuad(std::vector<uint8_t>& vertices, Quad& quad, int& i_vertex) {
  if (vertices.size() >= vertices.capacity() - 5) {
    vertices.reserve(vertices.size() * 2);
  }
  vertices.emplace_back(quad.data[0]);
  vertices.emplace_back(quad.data[1]);
  vertices.emplace_back(quad.data[2]);
  vertices.emplace_back(quad.data[3]);
  vertices.emplace_back(quad.data[4]);
  i_vertex++;
}
#else
inline uint64_t EncodeQuad(const uint64_t x, const uint64_t y, const uint64_t z, const uint64_t w,
                           const uint64_t h, const uint64_t type) {
  return (type << 32) | (h << 24) | (w << 18) | (z << 12) | (y << 6) | (x);
}
inline void InsertQuad(std::vector<uint64_t>& vertices, uint64_t quad, int& i_vertex) {
  vertices.emplace_back(quad);
  i_vertex++;
}
#endif

constexpr uint64_t PMask = ~(1ull << 63 | 1);
}  // namespace

void GenerateMesh(std::span<uint8_t> voxels, MeshAlgData& alg_data, MesherOutputData& mesh_data) {
  ZoneScoped;
  Timer t;
  mesh_data.vertex_cnt = 0;
  mesh_data.vertices.clear();
  const auto& opaque_mask = alg_data.mask->mask;
  auto& forward_merged = alg_data.forward_merged;
  auto& right_merged = alg_data.right_merged;
  int i_vertex{0};
  auto& face_masks = alg_data.face_masks;
  // Hidden face culling
  for (int a = 1; a < PCS - 1; a++) {
    const int a_pcs = a * PCS;

    for (int b = 1; b < PCS - 1; b++) {
      const uint64_t column_bits = opaque_mask[(a * PCS) + b] & PMask;
      const int ba_index = (b - 1) + ((a - 1) * CS);
      const int ab_index = (a - 1) + ((b - 1) * CS);

      face_masks[ba_index + (0 * CS2)] = (column_bits & ~opaque_mask[a_pcs + PCS + b]) >> 1;
      face_masks[ba_index + (1 * CS2)] = (column_bits & ~opaque_mask[a_pcs - PCS + b]) >> 1;

      face_masks[ab_index + (2 * CS2)] = (column_bits & ~opaque_mask[a_pcs + (b + 1)]) >> 1;
      face_masks[ab_index + (3 * CS2)] = (column_bits & ~opaque_mask[a_pcs + (b - 1)]) >> 1;

      face_masks[ba_index + (4 * CS2)] = column_bits & ~(opaque_mask[a_pcs + b] >> 1);
      face_masks[ba_index + (5 * CS2)] = column_bits & ~(opaque_mask[a_pcs + b] << 1);
    }
  }

  // Greedy meshing faces 0-3
  for (int face = 0; face < 4; face++) {
    const int axis = face / 2;

    const int face_vertex_begin = i_vertex;

    for (int layer = 0; layer < CS; layer++) {
      const int bits_location = (layer * CS) + (face * CS2);

      for (int forward = 0; forward < CS; forward++) {
        uint64_t bits_here = face_masks[forward + bits_location];
        if (bits_here == 0) continue;

        const uint64_t bits_next = forward + 1 < CS ? face_masks[(forward + 1) + bits_location] : 0;

        uint8_t right_merged = 1;
        while (bits_here) {
          uint64_t bit_pos;
#ifdef _MSC_VER
          _BitScanForward64(&bitPos, bitsHere);
#else
          bit_pos = __builtin_ctzll(bits_here);
#endif

          const uint8_t type = voxels[AxisIndex(axis, forward + 1, bit_pos + 1, layer + 1)];
          uint8_t& forward_merged_ref = forward_merged[bit_pos];

          if ((bits_next >> bit_pos & 1) &&
              type == voxels[AxisIndex(axis, forward + 2, bit_pos + 1, layer + 1)]) {
            forward_merged_ref++;
            bits_here &= ~(1ull << bit_pos);
            continue;
          }

          for (int right = bit_pos + 1; right < CS; right++) {
            if (!(bits_here >> right & 1) || forward_merged_ref != forward_merged[right] ||
                type != voxels[AxisIndex(axis, forward + 1, right + 1, layer + 1)]) {
              break;
            }
            forward_merged[right] = 0;
            right_merged++;
          }
          bits_here &= ~((1ull << (bit_pos + right_merged)) - 1);

          const uint8_t mesh_front = forward - forward_merged_ref;
          const uint8_t mesh_left = bit_pos;
          const uint8_t mesh_up = layer + (~face & 1);
          const uint8_t mesh_width = right_merged;
          const uint8_t mesh_length = forward_merged_ref + 1;
          forward_merged_ref = 0;
          right_merged = 1;

#ifdef PACK_QUAD
          Quad q;
          switch (face) {
            case 0:
            case 1:
              EncodeQuad(q, mesh_front + (face == 1 ? mesh_length : 0), mesh_up, mesh_left,
                         mesh_length, mesh_width, type);
              break;
            default:
              EncodeQuad(q, mesh_up, mesh_front + (face == 2 ? mesh_length : 0), mesh_left,
                         mesh_length, mesh_width, type);
          }
#else
          uint64_t q;
          switch (face) {
            case 0:
            case 1:
              q = EncodeQuad(mesh_front + (face == 1 ? mesh_length : 0), mesh_up, mesh_left,
                             mesh_length, mesh_width, type);
              break;
            default:
              q = EncodeQuad(mesh_up, mesh_front + (face == 2 ? mesh_length : 0), mesh_left,
                             mesh_length, mesh_width, type);
          }
#endif
          mesh_data.vertex_cnt++;
          InsertQuad(mesh_data.vertices, q, i_vertex);
        }
      }
    }

    const int face_vertex_length = i_vertex - face_vertex_begin;
    alg_data.face_vertices_start_indices[face] = face_vertex_begin;
    alg_data.face_vertex_lengths[face] = face_vertex_length;
  }

  // Greedy meshing faces 4-5
  for (int face = 4; face < 6; face++) {
    const int axis = face / 2;

    const int face_vertex_begin = i_vertex;

    for (int forward = 0; forward < CS; forward++) {
      const int bits_location = (forward * CS) + (face * CS2);
      const int bits_forward_location = ((forward + 1) * CS) + (face * CS2);

      for (int right = 0; right < CS; right++) {
        uint64_t bits_here = face_masks[right + bits_location];
        if (bits_here == 0) continue;

        const uint64_t bits_forward =
            forward < CS - 1 ? face_masks[right + bits_forward_location] : 0;
        const uint64_t bits_right = right < CS - 1 ? face_masks[right + 1 + bits_location] : 0;
        const int right_cs = right * CS;

        while (bits_here) {
          uint64_t bit_pos;
#ifdef _MSC_VER
          _BitScanForward64(&bitPos, bitsHere);
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

          const uint8_t mesh_left = right - right_merged_ref;
          const uint8_t mesh_front = forward - forward_merged_ref;
          const uint8_t mesh_up = bit_pos - 1 + (~face & 1);
          const uint8_t mesh_width = 1 + right_merged_ref;
          const uint8_t mesh_length = 1 + forward_merged_ref;

          forward_merged_ref = 0;
          right_merged_ref = 0;

#ifdef PACK_QUAD
          Quad q;
          EncodeQuad(q, mesh_left + (face == 4 ? mesh_width : 0), mesh_front, mesh_up, mesh_width,
                     mesh_length, type);
#else
          uint64_t q = EncodeQuad(mesh_left + (face == 4 ? mesh_width : 0), mesh_front, mesh_up,
                                  mesh_width, mesh_length, type);
#endif

          mesh_data.vertex_cnt++;
          InsertQuad(mesh_data.vertices, q, i_vertex);
        }
      }
    }

    const int face_vertex_length = i_vertex - face_vertex_begin;
    alg_data.face_vertices_start_indices[face] = face_vertex_begin;
    alg_data.face_vertex_lengths[face] = face_vertex_length;
  }

  mesh_data.mesh_time = t.ElapsedMicro();
}
