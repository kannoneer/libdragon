#ifndef AABB_H_
#define AABB_H_

typedef struct aabb_s {
    float lo[3]; // minimum
    float hi[3]; // maximum
} aabb_t;

void aabb_get_size(const aabb_t* box, float* size);
void aabb_get_center(const aabb_t* box, float* center);
void aabb_extend_(aabb_t* box, const float* p);
void aabb_union_(aabb_t* box, const aabb_t* other);

#include <debug.h>

inline void aabb_print(const aabb_t* box) {
    debugf("{.lo = (%f, %f, %f), .hi = (%f, %f, %f)}\n",
        box->lo[0], box->lo[1], box->lo[2],
        box->hi[0], box->hi[1], box->hi[2]);
        }

#endif