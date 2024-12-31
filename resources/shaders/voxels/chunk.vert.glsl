#version 460

#extension GL_GOOGLE_include_directive : require
#include "../scene_data.h.glsl"

struct QuadData {
    uint data1;
    uint data2;
};

layout(set = 1, binding = 0) readonly buffer ssbo1 {
    QuadData data[];
} quads;

layout(location = 0) out vec3 out_pos;
layout(location = 1) out uint out_material;

const int flip_lookup[6] = int[6](1, -1, -1, 1, -1, 1);

vec4 GetVertexPos() {
    // unpack chunk offset and face
    // gl_BaseInstance is unused, so use it for chunk position
    ivec3 chunk_offset_pos = ivec3(gl_BaseInstance & 255u, (gl_BaseInstance >> 8) & 255u, (gl_BaseInstance >> 16) & 255u) * 62;
    chunk_offset_pos = ivec3(0, -60, 0);
    uint face = gl_BaseInstance >> 24;
    // get index within the quad
    int vertex_id = int(gl_VertexIndex & 3u);
    // get quad idx
    uint quad_idx = gl_VertexIndex >> 2u;

    uint data1 = quads.data[quad_idx].data1;
    uint data2 = quads.data[quad_idx].data2;

    // unpack quad position in the chunk
    ivec3 i_vertex_pos = ivec3(data1 & 63u, (data1 >> 6u) & 63u, (data1 >> 12u) & 63u);

    i_vertex_pos += chunk_offset_pos;
    // unpack width, height
    int w = int((data1 >> 18u) & 63u);
    int h = int((data1 >> 24u) & 63u);
    // get direction for the width axis and height axis based on face
    uint w_dir = (face & 2) >> 1, h_dir = 2 - (face >> 2);
    int w_mod = vertex_id >> 1, h_mod = vertex_id & 1;

    // offset the vertex in correct direction length
    i_vertex_pos[w_dir] += (w * w_mod * flip_lookup[face]);
    i_vertex_pos[h_dir] += (h * h_mod);

    out_pos = i_vertex_pos;
    out_material = (data2 & 255u);

    vec3 vertex_pos = i_vertex_pos - scene_data.view_pos_int;
    // offset by a little for t-junctions
    vertex_pos[w_dir] += 0.0007 * flip_lookup[face] * (w_mod * 2 - 1);
    vertex_pos[h_dir] += 0.0007 * (h_mod * 2 - 1);

    return scene_data.world_center_viewproj * vec4(vertex_pos, 1.0);
}

void main() {
    gl_Position = GetVertexPos();
}
