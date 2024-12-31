#version 460

layout(location = 0) out vec4 out_frag_color;
layout(location = 0) in vec3 in_frag_pos;
layout(location = 1) flat in uint material;

void main() {
    float c = float(material) / 8;
    out_frag_color = vec4(vec3(c), 1.0);
}
