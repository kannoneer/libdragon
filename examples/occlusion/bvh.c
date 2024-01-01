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
    if (bvh->node_aabbs) free(bvh->node_aabbs);
}

void bvh_print_node(const bvh_node_t* n) {
    if (bvh_node_is_leaf(n)) {
        debugf("leaf (0x%x): ", n->flags);
    } else {
        debugf("inner (0x%x): ", n->flags);
    }
    debugf("(%f, %f, %f), rad^2=%f, idx=%u, axis=%lu\n", n->pos[0], n->pos[1], n->pos[2], n->radius_sqr, n->idx, bvh_node_get_axis(n));
}

bool bvh_validate(const sphere_bvh_t* bvh) {
    // TODO check parent bounds always contain all children
    if (!bvh->nodes) return false;
    if (bvh->num_nodes == 0) return false;
    if (bvh->num_leaves == 0) return false;
    if (bvh->num_nodes < bvh->num_leaves) return false;
    uint32_t counted_leaves=0;
    for (uint32_t i=0;i<bvh->num_nodes;i++) {
        if (bvh->nodes[i].flags == 0) {
            counted_leaves++;
        }
    }
    assert(bvh->num_leaves == counted_leaves);
    if (bvh->num_leaves != counted_leaves) return false;
    return true;
}

bool bvh_build(const float* origins, const float* radiuses, const aabb_t* aabbs, uint32_t num, sphere_bvh_t* out_bvh)
{
    sphere_bvh_t bvh = {};

    // the number of nodes for a full binary tree with 'num' leaves
    const uint32_t max_nodes = 2 * num - 1;
    debugf("max_nodes: %lu\n", max_nodes);
    // temporary array where we add nodes as we go and truncate at the end
    bvh_node_t* nodes = malloc(max_nodes * sizeof(bvh_node_t));
    aabb_t* node_aabbs = malloc(max_nodes * sizeof(aabb_t));
    // references to data arrays origins[] and radiuses[]
    uint32_t* inds = malloc(max_nodes * sizeof(uint32_t));

    // Start at identity mapping that gets permuted by sorting of recursive subranges
    for (uint32_t i = 0; i < num; i++) {
        inds[i] = i;
    }

    // Convention: 
    // variable 'i'    indexes the 'inds[]' array.
    // variable 'idx'  indexes data arrays with idx=inds[i]
    // variable 'n_id' indexes nodes[]

    DEFER(free(nodes));
    DEFER(free(node_aabbs));
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
            node_aabbs[n_id] = aabbs[idx];
            debugf("  ");
            bvh_print_node(n);
        } else {
            aabb_t bounds = (aabb_t){.lo = {__FLT_MAX__, __FLT_MAX__, __FLT_MAX__}, .hi={-__FLT_MAX__, -__FLT_MAX__, -__FLT_MAX__}};

            int count = 0;
            for (uint32_t i = start; i < start + num; i++) {
                aabb_union_(&bounds, &aabbs[inds[i]]);
                count++;
            }

            aabb_print(&bounds);
            node_aabbs[n_id] = bounds;

            float dims[3];
            aabb_get_size(&bounds, dims);
            uint32_t axis = 0;
            if (dims[0] < dims[1]) {
                axis = 1;
            }
            if (dims[1] < dims[2] && dims[0] < dims[2]) {
                axis = 2;
            }

            debugf("dims: (%f, %f, %f) --> axis=%lu\n", dims[0], dims[1], dims[2], axis);


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

            float pos[3];// = {mins[0] + 0.5f * dims[0], mins[1] + 0.5f * dims[1], mins[2] + 0.5f * dims[2]};
            aabb_get_center(&bounds, pos);

            uint32_t split = start + num/2;  // median cut by default

            // See if there's an split index that splits the bounding box roughly in two.
            // We ignore the first and last elements because that would be a degenerate split.
            // We'll fall back to default 50/50 split if no a good one was found.
            assert(num >= 1);
            assert(start < max_nodes - 1);
            for (uint32_t i = start + 1; i < start + num - 1; i++) {
                uint32_t idx = inds[i];
                const float* p = &origins[3*idx];
                if (p[axis] >= pos[axis]) {
                    split = i;
                    break;
                }
            }

            // then compute max radius that holds all spheres inside
            float max_radius = 0.0f;
            for (uint32_t i = start; i < start + num; i++) {
                uint32_t idx = inds[i];
                const float* p = &origins[3*idx];
                float r = radiuses[idx];
                float delta[3] = {p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]};
                float dist = sqrtf(delta[0]*delta[0] + delta[1]*delta[1] + delta[2]*delta[2]);
                max_radius = max(max_radius, dist + r); // must contain sphere origin + its farthest point
            }

            n->pos[0] = pos[0];
            n->pos[1] = pos[1];
            n->pos[2] = pos[2];
            n->radius_sqr = max_radius * max_radius;
            n->flags = 0;
            n->flags |= axis << 2;

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

    const uint32_t size = bvh.num_nodes * sizeof(bvh_node_t);
    bvh.nodes = calloc(size, 1); // calloc so that any padding bytes become zero, good for serialization
    memcpy(bvh.nodes, nodes, size);
    bvh.num_leaves = num;
    bvh.node_aabbs = calloc(bvh.num_nodes, sizeof(bvh.node_aabbs[0]));
    memcpy(bvh.node_aabbs, node_aabbs, bvh.num_nodes * sizeof(bvh.node_aabbs[0]));

    *out_bvh = bvh;
    return true;
}

const static bool measure_perf = false;

#include <timer.h>

static float squared_distance(const float* a, const float* b) {
    float diff[3] = {
        b[0] - a[0],
        b[1] - a[1],
        b[2] - a[2],
    };
    return diff[0]*diff[0] + diff[1]*diff[1] + diff[2]*diff[2];
}

uint32_t bvh_find_visible(const sphere_bvh_t* bvh, const float* camera_pos, const plane_t* planes, cull_result_t* out_data_inds, uint32_t max_data_inds)
{
    uint32_t num = 0;

    if (max_data_inds == 0) {
        return 0;
    }

    struct {
        uint32_t num_tests;
        uint32_t num_inner;
        uint32_t num_leaves;
        uint64_t start_ticks;
    } perf = {};

    if (measure_perf) {
        perf.start_ticks = get_ticks();
    }

    void find(uint32_t i, bool camera_inside_parent, uint8_t plane_mask) {
        bvh_node_t* n = &bvh->nodes[i];

        bool camera_is_inside = false;
        //debugf("%lu, 0x%x\n",i ,inflags);

        if (camera_inside_parent) {
            camera_is_inside = squared_distance(camera_pos, n->pos) < n->radius_sqr;
        }

        bool visible = camera_is_inside;
        if (!visible) {
            bool in_frustum = SIDE_OUT != is_sphere_inside_frustum(planes, n->pos, n->radius_sqr, &plane_mask);
            visible = in_frustum;
        }

        if (measure_perf) {
            perf.num_tests++;
            if (n->flags == 0) {
                perf.num_leaves++;
            } else {
                perf.num_inner++;
            }
        }

        if (!visible) {
            return;
        }

        if (bvh_node_is_leaf(n)) {
            if (num < max_data_inds) {
                uint16_t flags = plane_mask;
                if (camera_is_inside) { flags |= VISIBLE_CAMERA_INSIDE; }

                float score = -squared_distance(camera_pos, n->pos);
                out_data_inds[num++] = (cull_result_t){.idx=n->idx, .flags=flags, .score=score};
            } else {
                return; // early out if output array size was reached
            }
        } else {
            // The left child has half the data sorted by 'axis' so as a sorting
            // heuristic we consider the sphere origin and the axis as a splitting
            // plane and traverse the camera side first. Hopefully this will give a
            // rough front-to-back traversal.
            int axis = bvh_node_get_axis(n);
            bool left_first = camera_pos[axis] < n->pos[axis];

            if (left_first) {
                if (n->flags & BVH_FLAG_LEFT_CHILD) {
                    find(i + 1, camera_is_inside, plane_mask);
                }
                if (n->flags & BVH_FLAG_RIGHT_CHILD) {
                    find(n->idx, camera_is_inside, plane_mask);
                }
            } else {
                if (n->flags & BVH_FLAG_RIGHT_CHILD) {
                    find(n->idx, camera_is_inside, plane_mask);
                }
                if (n->flags & BVH_FLAG_LEFT_CHILD) {
                    find(i + 1, camera_is_inside, plane_mask);
                }
            }
        }
    }

    // start at root node, camera is inside root's "parent", none of the clip planes are fully inside hence plane_mask=0x00
    find(0, true, 0x00);

    if (measure_perf) {
        uint64_t took = TICKS_SINCE(perf.start_ticks);
        uint64_t took_us = TICKS_TO_US(took);
        double took_ms = took_us / 1e3;
        double us_per_test = took_us / (double)perf.num_tests;
        debugf("took: % 3.3f ms, test ratio: %.1f%%, % 3.3f us/test, num_tests: %lu, inner: %lu, leaves: %lu\n",
        took_ms,
        (float)perf.num_tests/bvh->num_leaves * 100, us_per_test, perf.num_tests, perf.num_inner, perf.num_leaves);
    }

    return num;
}