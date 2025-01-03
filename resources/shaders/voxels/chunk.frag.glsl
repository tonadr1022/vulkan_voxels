#version 460

#extension GL_GOOGLE_include_directive : require
#include "../scene_data.h.glsl"

layout(location = 0) out vec4 out_frag_color;

layout(location = 0) in vec3 in_frag_pos;
layout(location = 1) flat in vec3 in_normal;
layout(location = 2) flat in uint material;
// TODO: move the define out
#define SINGLE_TRIANGLE_QUADS
#ifdef SINGLE_TRIANGLE_QUADS
layout(location = 3) in vec2 uv;
#endif

const vec3 diffuse_color = vec3(0.15, 0.15, 0.15);
uvec3 DecompressRGB(uint compressed) {
    uint red = (compressed >> 5) & 0x07;
    uint green = (compressed >> 2) & 0x07;
    uint blue = compressed & 0x03;
    red = red << 5 | red << 2 | red >> 1;
    green = green << 5 | green << 2 | green >> 1;
    blue = blue << 6 | blue << 4 | blue << 2 | blue;
    return uvec3(red, green, blue);
}

void main() {
    #ifdef SINGLE_TRIANGLE_QUADS
    if (uv.x > 1.0 || uv.y > 1.0) {
        discard;
    }
    #endif
    #ifdef VISUALIZE_NORMALS
    out_frag_color = vec4(vec3(in_normal * 0.5 + 0.5), 1.0);
    return;
    #endif
    vec3 L = normalize(-scene_data.sun_dir.xyz);
    float NdotL = max(dot(in_normal, L), 0.0);
    vec3 diffuse = NdotL * diffuse_color;
    // uvec3 col = DecompressRGB(material);
    uvec3 col = uvec3(material);
    vec3 mat_color = vec3(col) / 255.0;
    out_frag_color = vec4(diffuse + mat_color, 1.0);
    return;
}
