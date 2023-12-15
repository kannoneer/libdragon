#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <memory.h>
#include <assert.h>

#include "bvh.h"

#include <debug.h>

#ifndef max
#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })
#endif

#ifndef min
#define min(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })
#endif

// Rasky's defer macro
#ifndef DEFER
#define PPCAT2(n,x) n ## x
#define PPCAT(n,x) PPCAT2(n,x)

// DEFER(stmt): execute "stmt" statement when the current lexical block exits.
// This is useful in tests to execute cleanup functions even if the test fails
// through ASSERT macros.
#define DEFER2(stmt, counter) \
    void PPCAT(__cleanup, counter) (int* __u) { stmt; } \
    int PPCAT(__var, counter) __attribute__((unused, cleanup(PPCAT(__cleanup, counter ))));
#define DEFER(stmt) DEFER2(stmt, __COUNTER__)
#endif

void bvh_free(sphere_bvh_t* bvh) {
    if (bvh->nodes) free(bvh->nodes);
}

void bvh_print_node(const bvh_node_t* n) {
    if (n->flags == 0) {
        debugf("leaf (0x%x): ", n->flags);
    } else {
        debugf("inner (0x%x): ", n->flags);
    }
    debugf("(%f, %f, %f), rad^2=%f, idx=%u\n", n->pos[0], n->pos[1], n->pos[2], n->radius_sqr, n->idx);
}

bool bvh_validate(const sphere_bvh_t* bvh) {
    // TODO check parent bounds always contain all children
    if (!bvh->nodes) return false;
    if (bvh->num_nodes == 0) return false;
    return true;
}

bool bvh_build(float* origins, float* radiuses, uint32_t num, sphere_bvh_t* out_bvh)
{
    sphere_bvh_t bvh = {};

    // the number of nodes for a full binary tree with 'num' leaves
    const uint32_t max_nodes = 2 * num - 1;
    debugf("max_nodes: %lu\n", max_nodes);
    // temporary array where we add nodes as we go and truncate at the end
    bvh_node_t* nodes = malloc(max_nodes * sizeof(bvh_node_t));
    // references to origins[] and radiuses[]
    uint32_t* inds = malloc(max_nodes * sizeof(uint32_t));
    // start at identity mapping that gets permuted by sorting of recursive subranges
    for (uint32_t i=0;i<num;i++) {
        inds[i] = i;
    }

    DEFER(free(nodes));
    DEFER(free(inds));

    bvh.num_nodes = 0;

    uint32_t bvh_build_range(uint32_t start, uint32_t num) {
        debugf("[%lu,num=%lu] node=%lu\n", start, num, bvh.num_nodes);
        assert(num > 0);
        assert(num != UINT32_MAX);

        uint32_t n_id = bvh.num_nodes++;
        assert(n_id <= max_nodes);

        bvh_node_t* n = &nodes[n_id];
        *n = (bvh_node_t){};

        bool is_leaf = num == 1;
        if (is_leaf) {
            uint32_t idx = inds[start];
            n->pos[0] = origins[idx*3+0];
            n->pos[1] = origins[idx*3+1];
            n->pos[2] = origins[idx*3+2];
            n->radius_sqr = radiuses[idx] * radiuses[idx];
            n->flags = 0;
            n->idx = idx; // represents an index to data array
            debugf("  ");
            bvh_print_node(n);
        } else {
            // fit a sphere to 'num' smaller spheres
            // first compute origin
            float pos[3] = {0.0f, 0.0f, 0.0f};
            float mins[3] = {__FLT_MAX__, __FLT_MAX__, __FLT_MAX__};
            float maxs[3] = {-__FLT_MAX__, -__FLT_MAX__, -__FLT_MAX__};

            int count = 0;
            for (uint32_t i = start; i < start + num; i++) {
                float* p = &origins[3*i];
                for (uint32_t j = 0; j < 3; j++) {
                    pos[j] += p[j];
                    mins[j] = min(mins[j], p[j]);
                    maxs[j] = max(maxs[j], p[j]);
                    count++;
                }
            }

            float denom = 1.0f/(float)count;
            pos[0] *= denom;
            pos[1] *= denom;
            pos[2] *= denom;

            float dims[3] = {maxs[0] - mins[0], maxs[1] - mins[1], maxs[2] - mins[2]};
            int axis = 0;
            if (dims[0] < dims[1]) {
                axis = 1;
            }
            if (dims[1] < dims[2] && dims[0] < dims[2]) {
                axis = 2;
            }

            debugf("dims: (%f, %f, %f) --> axis=%d\n", dims[0], dims[1], dims[2], axis);


            int compare(const void *a, const void *b)
            {
                float fa = origins[3 * (*(int *)a) + axis];
                float fb = origins[3 * (*(int *)b) + axis];
                // debugf("%f < %f\n", fa, fb);
                if (fa < fb) {
                    return -1;
                }
                else if (fa > fb) {
                    return 1;
                }
                else {
                    return 0;
                }
            }

            qsort(&inds[start], num, sizeof(inds[0]), compare);

            // then compute max radius that holds all spheres inside
            float max_radius = 0.0f;
            for (uint32_t i = start; i < start + num; i++) {
                float* p = &origins[3*i];
                float r = radiuses[i];
                float delta[3] = {p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]};
                float dist = sqrtf(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
                max_radius = max(max_radius, dist + r); // must contain sphere origin + its farthest point
            }

            n->pos[0] = pos[0];
            n->pos[1] = pos[1];
            n->pos[2] = pos[2];
            n->radius_sqr = max_radius * max_radius;
            n->flags = 0;

            uint32_t split = start + num/2;
            uint32_t left_num = split - start;
            uint32_t right_num = num - left_num;

            if (left_num > 0) {
                n->flags |= BVH_FLAG_LEFT_CHILD;
            }
            if (right_num > 0) {
                n->flags |= BVH_FLAG_RIGHT_CHILD;
            }

            if (left_num > 0) {
                uint32_t left_id = bvh_build_range(start, left_num);
                assert(left_id == n_id + 1);
            }
            if (right_num > 0) {
                // represents an index to nodes[] array
                n->idx = bvh_build_range(split, right_num);
            }

            debugf("  ");
            debugf("   (n_id=%lu)", n_id);
            bvh_print_node(n);
        }

        return n_id;
    }

    bvh_build_range(0, num);

    const uint32_t size =  bvh.num_nodes * sizeof(bvh_node_t);
    bvh.nodes = calloc(size, 1); // calloc so that any padding bytes become zero, good for serialization
    memcpy(bvh.nodes, nodes, size);

    *out_bvh = bvh;
    return true;
}

uint32_t bvh_find_visible(const sphere_bvh_t* bvh, const plane_t* planes, uint16_t* out_data_inds, uint32_t max_data_inds)
{
    uint32_t num = 0;
    assert(max_data_inds > 0);

    for (uint32_t i = 0; i < bvh->num_nodes; i++) {
        bvh_node_t* n = &bvh->nodes[i];
        bool is_leaf = n->flags == 0;
        if (!is_leaf) continue;

        bool in_frustum = SIDE_OUT != is_sphere_inside_frustum(planes, n->pos, n->radius_sqr);

        if (in_frustum) {
            out_data_inds[num++] = n->idx;
            if (num >= max_data_inds) {
                break;
            }
        }
    }

    return num;
}