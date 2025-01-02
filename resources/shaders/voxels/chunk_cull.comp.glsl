#version 460

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

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#define VERTEX_SIZE 8
#define SINGLE_TRIANGLE_QUAD
void main() {
    uint g_id = gl_GlobalInvocationID.x;
    if (g_id >= in_draw_info.length()) return;
    DrawInfo info = in_draw_info[g_id];
    if (info.handle == uvec2(0)) return;
    // TODO: visibility test

    uint insert_idx = atomicAdd(next_idx, 1);
    DrawIndexedIndirectCmd cmd;
    cmd.instance_count = 1;
    cmd.first_instance = insert_idx;
    cmd.vertex_offset = (info.vertex_offset / VERTEX_SIZE) << 2;
    #ifdef SINGLE_TRIANGLE_QUAD
    cmd.index_count = (info.size_bytes / VERTEX_SIZE) * 3;
    #else
    cmd.index_count = (info.size_bytes / VERTEX_SIZE) * 6;
    #endif
    cmd.first_index = 0;
    UniformData uniform_data;
    uniform_data.pos = info.pos;
    out_uniforms[insert_idx] = uniform_data;
    out_draw_cmds[insert_idx] = cmd;
}
