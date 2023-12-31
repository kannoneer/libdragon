#ifndef FRUSTUM_H_
#define FRUSTUM_H_

#include <stdint.h>

#define FRUSTUM_RIGHT (0)
#define FRUSTUM_TOP (1)
#define FRUSTUM_NEAR (2)
#define FRUSTUM_LEFT (3)
#define FRUSTUM_BOTTOM (4)
#define FRUSTUM_FAR (5)

typedef float plane_t[4];

// Plane normals will point inside the frustum.
void extract_planes_from_projmat(
    const float mat[4][4],
    plane_t right,
    plane_t top,
    plane_t near,
    plane_t left,
    plane_t bottom,
    plane_t far);

void normalize_plane(float* plane);
void print_clip_plane(const float* p);

enum plane_side_e {
    SIDE_OUT = -1,
    SIDE_INTERSECTS = 0,
    SIDE_IN = 1,
};

typedef int plane_side_t;
plane_side_t test_plane_sphere(const float* plane, const float* p, const float radius_sqr);

// inflags: clipping plane bitmask that's both read and written. *inflags = 0x00 tests all, 0x3f tests nothing
plane_side_t is_sphere_inside_frustum(const plane_t* planes, const float* pos, const float radius_sqr, uint8_t* inflags);

#endif