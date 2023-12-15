#include "frustum.h"

#include <math.h>
#include <debug.h>

// frustum culling

void normalize_plane(float* plane) {
    float scaler = 1.0f/sqrtf(plane[0]*plane[0] + plane[1]*plane[1] + plane[2]*plane[2]);
    plane[0] *= scaler;
    plane[1] *= scaler;
    plane[2] *= scaler;
    plane[3] *= scaler;
}

// The Gribb-Hartmann method, see https://stackoverflow.com/a/34960913
// and https://www8.cs.umu.se/kurser/5DV051/HT12/lab/plane_extraction.pdf
// Plane normals will point inside the frustum.
void extract_planes_from_projmat(
    const float mat[4][4],
    plane_t left, plane_t right,
    plane_t bottom, plane_t top,
    plane_t near, plane_t far)
{
    for (int i = 4; i--; ) { left[i]   = mat[i][3] + mat[i][0]; }
    for (int i = 4; i--; ) { right[i]  = mat[i][3] - mat[i][0]; }
    for (int i = 4; i--; ) { bottom[i] = mat[i][3] + mat[i][1]; }
    for (int i = 4; i--; ) { top[i]    = mat[i][3] - mat[i][1]; }
    for (int i = 4; i--; ) { near[i]   = mat[i][3] + mat[i][2]; }
    for (int i = 4; i--; ) { far[i]    = mat[i][3] - mat[i][2]; }
    normalize_plane(&left[0]);
    normalize_plane(&right[0]);
    normalize_plane(&bottom[0]);
    normalize_plane(&top[0]);
    normalize_plane(&near[0]);
    normalize_plane(&far[0]);
}

void print_clip_plane(float* p) {
    debugf("(%f, %f, %f, %f)\n", p[0], p[1], p[2], p[3]);
}

plane_test_result_t test_plane_sphere(float* plane, float* p, float radius_sqr) {
    float dist = plane[0] * p[0] + plane[1] * p[1] + plane[2] * p[2] + plane[3];
    float dist_sqr = dist * dist;
    //debugf("dist: %f, dist_sqr: %f vs radius_sqr=%f\n", dist, dist_sqr, radius_sqr);
    if (dist_sqr < radius_sqr) {
        return RESULT_INTERSECTS;
    }
    if (dist > 0) {
        return RESULT_INSIDE;
    }

    return RESULT_OUTSIDE;
}

plane_test_result_t is_sphere_inside_frustum(plane_t* planes, float* pos, float radius_sqr)
{
    plane_test_result_t all = RESULT_INSIDE;

    for (int i=0;i<6;i++) {
        plane_test_result_t result = test_plane_sphere(planes[i], pos, radius_sqr);
        if (result == RESULT_OUTSIDE) return result;
        if (result < all) all = result;
    }

    return all;
}

