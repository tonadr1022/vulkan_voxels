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
    uvec2 handle;
    uint vertex_offset;
    uint size_bytes;
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
    mat4 cam_view;
    vec4 cam_dir;
    vec4 cam_pos;
    uint cull_cam;
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

#define VERTEX_SIZE 8
#define SINGLE_TRIANGLE_QUAD
void main() {
    uint g_id = gl_GlobalInvocationID.x;
    if (g_id >= in_draw_info.length()) return;
    DrawInfo info = in_draw_info[g_id];
    if (info.handle == uvec2(0)) return;

    vec3 chunk_center = vec3(info.pos.xyz) * 62.0 + vec3(31.0);

    // TODO: visibility test
    uint offset = info.vertex_offset;
    int cam_chunk_y = int(floor(cam_pos.y / 62.0));
    for (int i = 0; i < 6; i++) {
        vec3 face_normal = normal_lookup[i];
        uint size_bytes = info.vertex_counts[i] * VERTEX_SIZE;
        if (size_bytes == 0) continue;

        vec3 face_center = CalculateFaceCenter(chunk_center, face_normal, 62.0);
        vec3 cam_to_face = face_center - cam_pos.xyz;
        bool facing = dot(cam_to_face, face_normal) < 0.0;
        if (!facing && cam_chunk_y == info.pos.y) {
            facing = true;
        }

        if (cull_cam == 0 || facing) {
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
            uniform_data.pos = ivec4(info.pos.xyz, i);
            out_uniforms[insert_idx] = uniform_data;
            out_draw_cmds[insert_idx] = cmd;
        }
        offset += size_bytes;
    }
}
