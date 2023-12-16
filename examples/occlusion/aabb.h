#ifndef AABB_H_
#define AABB_H_

typedef struct aabb_s {
    float lo[3]; // minimum
    float hi[3]; // maximum
} aabb_t;

void aabb_get_size(aabb_t* box, float* size);
void aabb_get_center(aabb_t* box, float* center);
void aabb_extend_(aabb_t* box, const float* p);
void aabb_union_(aabb_t* box, const aabb_t* other);

#endif