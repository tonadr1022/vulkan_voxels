#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require
#include "../scene_data.h.glsl"
#include "../common.h.glsl"

struct QuadData {
    uint data1;
    uint data2;
};

layout(set = 1, binding = 0) readonly buffer ssbo1 {
    #ifdef PACK_QUAD
    uint data[];
    #else
    QuadData data[];
    #endif
} quads;

#ifdef PACK_QUAD
uint SafeShiftLeft(uint value, uint shiftAmount) {
    uint mask = (shiftAmount - 32u) >> 31u;
    return (value << (shiftAmount & 31u)) & -mask;
}
uvec2 UnpackQuad(uint quadIndex) {
    uint bitOffset = quadIndex * 40;
    uint wordOffset = bitOffset >> 5;
    uint bitStart = (bitOffset & 31u);
    uint lower = quads.data[wordOffset];
    uint upper = quads.data[wordOffset + 1];
    // need to safe shift left so shifting by 32 sets to 0 instead of doing nothing
    return uvec2((lower >> bitStart) | SafeShiftLeft(upper, 32 - bitStart), (upper >> (bitStart)) & 0xFF);
}
#else
uvec2 UnpackQuad(uint quadIndex) {
    QuadData d = quads.data[quadIndex];
    return uvec2(d.data1, d.data2);
}
#endif

struct UniformData {
    ivec4 pos;
};

layout(set = 1, binding = 1) readonly buffer ssbo2 {
    UniformData uniforms[];
};

layout(location = 0) out vec3 out_pos;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out uint out_material;

#ifdef SINGLE_TRIANGLE_QUAD
layout(location = 3) out vec2 out_uv;
const vec2 uv_lookup[3] = vec2[3](vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 2.0));
#endif

const vec3 normal_lookup[6] = vec3[6](
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, -1.0, 0.0),
        vec3(1.0, 0.0, 0.0),
        vec3(-1.0, 0.0, 0.0),
        vec3(0.0, 0.0, 1.0),
        vec3(0.0, 0.0, -1.0)
    );

const int flip_lookup[6] = int[6](1, -1, -1, 1, -1, 1);

vec4 GetVertexPos() {
    UniformData u = uniforms[gl_BaseInstance];
    uint face = u.pos.w & 7;
    int chunk_mult = int(u.pos.w >> 3);
    // chunk_mult = 2;
    ivec3 chunk_offset_pos = u.pos.xyz;
    // get index within the quad
    int vertex_id = int(gl_VertexIndex & 3u);
    // get quad idx
    uint quad_idx = gl_VertexIndex >> 2u;

    uvec2 data = UnpackQuad(quad_idx);
    uint data1 = data.x;
    uint data2 = data.y;

    // unpack quad position in the chunk
    ivec3 i_vertex_pos = ivec3(data1 & 63u, (data1 >> 6u) & 63u, (data1 >> 12u) & 63u) * chunk_mult;

    i_vertex_pos += chunk_offset_pos;
    // unpack width, height
    int w = int((data1 >> 18u) & 63u);
    int h = int((data1 >> 24u) & 63u);
    // get direction for the width axis and height axis based on face
    uint w_dir = (face & 2) >> 1, h_dir = 2 - (face >> 2);
    int w_mod = vertex_id >> 1, h_mod = vertex_id & 1;

    // offset the vertex in correct direction length
    #ifdef SINGLE_TRIANGLE_QUAD
    out_uv = uv_lookup[vertex_id];
    i_vertex_pos[w_dir] += (w * w_mod * flip_lookup[face]) * 2 * chunk_mult;
    i_vertex_pos[h_dir] += (h * h_mod) * 2 * chunk_mult;
    #else
    i_vertex_pos[w_dir] += (w * w_mod * flip_lookup[face]) * chunk_mult;
    i_vertex_pos[h_dir] += (h * h_mod) * chunk_mult;
    #endif

    out_pos = i_vertex_pos;
    out_material = (data2 & 255u);
    out_normal = normal_lookup[face];

    vec3 vertex_pos = i_vertex_pos - scene_data.view_pos_int.xyz;
    // offset by a little for t-junctions
    // vertex_pos[w_dir] += 0.0007 * flip_lookup[face] * (w_mod * 2 - 1);
    // vertex_pos[h_dir] += 0.0007 * (h_mod * 2 - 1);

    return scene_data.world_center_viewproj * vec4(vertex_pos, 1.0);
}

void main() {
    gl_Position = GetVertexPos();
}
