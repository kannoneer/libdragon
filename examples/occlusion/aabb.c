#include "aabb.h"

#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })

#define min(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })


void aabb_get_size(const aabb_t* box, float* size) {
    size[0] = box->hi[0] - box->lo[0];
    size[1] = box->hi[1] - box->lo[1];
    size[2] = box->hi[2] - box->lo[2];
}

void aabb_get_center(const aabb_t* box, float* center) {
    center[0] = 0.5f * (box->lo[0] + box->hi[0]);
    center[1] = 0.5f * (box->lo[1] + box->hi[1]);
    center[2] = 0.5f * (box->lo[2] + box->hi[2]);
}

void aabb_extend_(aabb_t* box, const float* p) {
    for (int i = 0; i < 3; i++) {
        box->lo[i] = min(box->lo[i], p[i]);
        box->hi[i] = max(box->hi[i], p[i]);
    }
}

void aabb_union_(aabb_t* box, const aabb_t* other) {
    for (int i = 0; i < 3; i++) {
        box->lo[i] = min(box->lo[i], other->lo[i]);
        box->hi[i] = max(box->hi[i], other->hi[i]);
    }
}
