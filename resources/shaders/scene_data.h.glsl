layout(set = 0, binding = 0) uniform SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    mat4 world_center_view;
    mat4 world_center_viewproj;
    ivec3 view_pos_int;
} scene_data;
