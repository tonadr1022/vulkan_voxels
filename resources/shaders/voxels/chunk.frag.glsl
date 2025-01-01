#version 460

#extension GL_GOOGLE_include_directive : require
#include "../scene_data.h.glsl"

layout(location = 0) out vec4 out_frag_color;

layout(location = 0) in vec3 in_frag_pos;
layout(location = 1) flat in vec3 in_normal;
layout(location = 2) flat in uint material;
// layout(location = 3) in vec3 in_bary_coords;

const vec3 diffuse_color = vec3(0.15, 0.15, 0.15);

void main() {
    // if (in_bary_coords.x > 0.5 || in_bary_coords.y > 0.5 || in_bary_coords.z > 0.5) {
    //     discard;
    // }
    #ifdef VISUALIZE_NORMALS
    out_frag_color = vec4(vec3(in_normal * 0.5 + 0.5), 1.0);
    return;
    #endif
    vec3 L = normalize(-scene_data.sun_dir.xyz);
    float NdotL = max(dot(in_normal, L), 0.0);
    vec3 diffuse = NdotL * diffuse_color;
    vec3 mat_color = vec3(float(material) / 255.0);
    out_frag_color = vec4(diffuse + mat_color, 1.0);
    return;
}
