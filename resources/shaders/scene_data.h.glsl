layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    mat4 world_center_view;
    mat4 world_center_viewproj;
    ivec4 view_pos_int;
    vec4 sun_dir;
    vec4 sun_color;
    vec4 view_dir;
    vec3 ambient_color;
} scene_data;
