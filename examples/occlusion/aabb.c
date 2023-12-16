#include "aabb.h"

void aabb_get_size(aabb_t* box, float* size) {
    size[0] = box->hi[0] - box->lo[0];
    size[1] = box->hi[1] - box->lo[1];
    size[2] = box->hi[2] - box->lo[2];
}

void aabb_get_center(aabb_t* box, float* center) {
    center[0] = 0.5f * (box->lo[0] + box->hi[0]);
    center[1] = 0.5f * (box->lo[1] + box->hi[1]);
    center[2] = 0.5f * (box->lo[2] + box->hi[2]);
}
