#include <stdint.h>
#include <memory.h>
#include "defer.h"

enum BvhFlags {
    BVH_FLAG_LEFT_CHILD = 1,
    BVH_FLAG_RIGHT_CHILD = 2,
};

typedef struct bvh_node_s {
    float pos[3];
    float radius_sqr;
    uint8_t flags;
    uint16_t idx; // inner node: right child, leaf node: Node* index in array
} bvh_node_t;

typedef struct sphere_bvh_s {
    bvh_node_t* nodes; // owned by this struct
    uint32_t num_nodes;
} sphere_bvh_t;

void bvh_free(sphere_bvh_t* bvh);
void bvh_print_node(bvh_node_t* n);
bool bvh_validate(const sphere_bvh_t* bvh);
bool bvh_build(float* origins, float* radiuses, uint32_t num, sphere_bvh_t* out_bvh);
