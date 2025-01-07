#version 460

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct DrawIndexedIndirectCmd {
    uint index_count;
    uint instance_count;
    uint first_index;
    uint vertex_offset;
    uint first_instance;
};

struct DrawInfo {
    uint handle;
    uint vertex_offset;
    uint size_bytes;
    uint pad;
    ivec4 pos;
    uint vertex_counts[8];
};

struct UniformData {
    ivec4 pos;
};

layout(set = 0, binding = 0) readonly restrict buffer ssbo_0 {
    DrawInfo in_draw_info[];
};

layout(set = 0, binding = 1) writeonly restrict buffer ssbo_1 {
    DrawIndexedIndirectCmd out_draw_cmds[];
};

layout(set = 0, binding = 2) writeonly restrict buffer ssbo_3 {
    UniformData out_uniforms[];
};

layout(std430, binding = 3) restrict buffer ssbo_4 {
    uint next_idx;
};

layout(push_constant) uniform pc {
    vec4 cam_pos;
    vec4 plane0;
    vec4 plane1;
    vec4 plane2;
    vec4 plane3;
    vec4 plane4;
    vec4 plane5;
    uvec4 bits;
};

const vec3 normal_lookup[6] = vec3[6](
        vec3(0.0, 1.0, 0.0),
        vec3(0.0, -1.0, 0.0),
        vec3(1.0, 0.0, 0.0),
        vec3(-1.0, 0.0, 0.0),
        vec3(0.0, 0.0, 1.0),
        vec3(0.0, 0.0, -1.0)
    );

vec3 CalculateFaceCenter(vec3 chunk_center, vec3 face_normal, float chunk_size) {
    return chunk_center + 0.5 * chunk_size * face_normal;
}

bool CullFrustum(vec4 pos, float radius) {
    #define cull(x) dot(pos, x) + radius >= 0.0
    return cull(plane0) && cull(plane1) && cull(plane2) && cull(plane3) && cull(plane4) && cull(plane5);
}

bool ChunkBackFaceCull(vec3 chunk_center, uint face, float chunk_size) {
    vec3 face_normal = normal_lookup[face];
    vec3 face_pos = chunk_center - 0.5 * (chunk_size * face_normal);
    vec3 cam_to_face = face_pos - cam_pos.xyz;
    return dot(cam_to_face, face_normal) < 0.0;
}

#define VERTEX_SIZE 5
#define SINGLE_TRIANGLE_QUAD

void main() {
    uint g_id = gl_GlobalInvocationID.x;
    if (g_id >= in_draw_info.length()) return;
    DrawInfo info = in_draw_info[g_id];
    if (info.handle == 0) return;

    int chunk_mult = info.pos.w >> 3;
    float chunk_size = chunk_mult * 62.0;
    float half_chunk_size = chunk_size * 0.5;
    vec3 chunk_center = vec3(info.pos.xyz) * chunk_size + vec3(half_chunk_size);

    // TODO: visibility test
    uint offset = info.vertex_offset;
    const bool freeze_cull = bool(bits.x & 0x4);
    if (freeze_cull) return;
    const bool frustum_cull_enabled = !freeze_cull && bool(bits.x & 0x2);
    if (frustum_cull_enabled) {
        vec4 pos = vec4(chunk_center - cam_pos.xyz, 1.0);
        if (!CullFrustum(pos, length(vec3(half_chunk_size)))) return;
    }

    for (int i = 0; i < 6; i++) {
        vec3 face_normal = normal_lookup[i];
        uint size_bytes = info.vertex_counts[i] * VERTEX_SIZE;
        if (size_bytes == 0) continue;

        bool visible = ChunkBackFaceCull(chunk_center, i, chunk_size);

        if (!freeze_cull && visible) {
            uint insert_idx = atomicAdd(next_idx, 1);
            DrawIndexedIndirectCmd cmd;
            cmd.instance_count = 1;
            cmd.first_instance = insert_idx;
            cmd.vertex_offset = (offset / VERTEX_SIZE) << 2;
            #ifdef SINGLE_TRIANGLE_QUAD
            cmd.index_count = (size_bytes / VERTEX_SIZE) * 3;
            #else
            cmd.index_count = (size_bytes / VERTEX_SIZE) * 6;
            #endif
            cmd.first_index = 0;
            UniformData uniform_data;
            uniform_data.pos = ivec4(info.pos.xyz, (chunk_mult << 3) | i);
            out_uniforms[insert_idx] = uniform_data;
            out_draw_cmds[insert_idx] = cmd;
        }
        offset += size_bytes;
    }
}
