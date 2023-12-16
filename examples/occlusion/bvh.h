#ifndef BVH_H_
#define BVH_H_
#include <stdint.h>
#include <memory.h>
#include "defer.h"
#include "frustum.h"
#include "aabb.h"

enum BvhFlags {
    BVH_FLAG_LEFT_CHILD = 1,
    BVH_FLAG_RIGHT_CHILD = 2,
};

#define BVH_FLAGS_CHILD_MASK (0b0011)
#define BVH_FLAGS_AXIS_MASK (0b1100)

typedef struct bvh_node_s {
    float pos[3];
    float radius_sqr;
    uint8_t flags;
    uint16_t idx; // inner node: right child, leaf node: Node* index in array
} bvh_node_t;

typedef struct sphere_bvh_s {
    bvh_node_t* nodes; // owned by this struct
    uint32_t num_nodes;
    uint32_t num_leaves;
} sphere_bvh_t;

inline bool bvh_node_is_leaf(const bvh_node_t* n) { return (n->flags & BVH_FLAGS_CHILD_MASK) == 0; }
inline uint32_t bvh_node_get_axis(const bvh_node_t* n) { return (n->flags & BVH_FLAGS_AXIS_MASK) >> 2; }

void bvh_free(sphere_bvh_t* bvh);
void bvh_print_node(const bvh_node_t* n);
bool bvh_validate(const sphere_bvh_t* bvh);
bool bvh_build(const float* origins, const float* radiuses, const aabb_t* aabbs, uint32_t num, sphere_bvh_t* out_bvh);

uint32_t bvh_find_visible(const sphere_bvh_t* bvh, const float* camera_pos, const plane_t* planes, uint16_t* out_data_inds, uint32_t max_data_inds);

#endif