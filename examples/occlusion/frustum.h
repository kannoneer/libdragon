#ifndef FRUSTUM_H_
#define FRUSTUM_H_

void normalize_plane(float* plane);

typedef float plane_t[4];

// Plane normals will point inside the frustum.
void extract_planes_from_projmat(
    const float mat[4][4],
    plane_t left, plane_t right,
    plane_t bottom, plane_t top,
    plane_t near, plane_t far);

void print_clip_plane(float* p);

enum plane_test_result_e {
    RESULT_OUTSIDE = -1,
    RESULT_INTERSECTS = 0,
    RESULT_INSIDE = 1,
};

typedef int plane_test_result_t;
plane_test_result_t test_plane_sphere(float* plane, float* p, float radius_sqr);
plane_test_result_t is_sphere_inside_frustum(plane_t* planes, float* pos, float radius_sqr);

#endif