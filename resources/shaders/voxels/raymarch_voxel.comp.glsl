#version 460

layout(local_size_x = 8, local_size_y = 8) in;

#extension GL_GOOGLE_include_directive : require
#include "../math.h.glsl"

layout(set = 0, binding = 0) uniform writeonly image2D img;

#define STORE(x) imageStore(img, pos, vec4(x, 1.0)); return

// layout(set = 1, binding = 0) readonly buffer VoxelGrid {
//     uint voxels[];
// };

struct AABB {
    vec3 min;
    vec3 max;
};

layout(push_constant) uniform block {
    AABB aabb;
    uvec2 img_dims;
    vec4 cam_dir;
    vec3 cam_pos;
};

#define MAXRAYSTEPS 64

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= img_dims.x || pos.y >= img_dims.y) return;
    float time = cam_dir.w / 1000.0;
    vec3 camera_dir = cam_dir.xyz;
    ivec2 size = imageSize(img);
    float aspect_ratio = float(size.x) / float(size.y);

    vec3 vx = normalize(cross(camera_dir, vec3(0.0, 1.0, 0.0)));
    vec3 vy = normalize(cross(camera_dir, vx));
    vec2 ndc = (vec2(pos) + 0.5) / vec2(size);
    // vec2 ssc = 2 * (ndc - 0.5);
    vec2 ssc = 2 * (ndc - 0.5);
    vec3 ray_dir = normalize(camera_dir + ssc.x * vx * aspect_ratio + ssc.y * vy);

    Ray r;
    r.origin = cam_pos;
    r.dir = ray_dir;
    r.inv_dir = 1.0 / r.dir;
    vec3 color = vec3(0.1, 0.1, 0.2);

    Hit hit;
    if (BBoxIntersect(aabb.min, aabb.max, r, hit)) {
        color = vec3(1.0, 0.0, 0.0);
    }
    STORE(color);
}
