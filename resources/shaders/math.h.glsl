#ifndef VOXEL_MATH
#define VOXEL_MATH

struct Hit {
    float tmin;
    float tmax;
};

struct Ray {
    vec3 origin;
    vec3 dir;
    vec3 inv_dir;
};

// https://www.reddit.com/r/opengl/comments/8ntzz5/fast_glsl_ray_box_intersection/
bool BBoxIntersect(const vec3 box_min, const vec3 box_max, const Ray r, out Hit hit) {
    vec3 tbot = r.inv_dir * (box_min - r.origin);
    vec3 ttop = r.inv_dir * (box_max - r.origin);
    vec3 tmin = min(ttop, tbot);
    vec3 tmax = max(ttop, tbot);
    vec2 t = max(tmin.xx, tmin.yz);
    float t0 = max(t.x, t.y);
    t = min(tmax.xx, tmax.yz);
    float t1 = min(t.x, t.y);
    hit.tmin = t0;
    hit.tmax = t1;
    return t1 > max(t0, 0.0);
}
vec2 rotate2d(vec2 v, float a) {
    float sinA = sin(a);
    float cosA = cos(a);
    return vec2(v.x * cosA - v.y * sinA, v.y * cosA + v.x * sinA);
}
#endif
