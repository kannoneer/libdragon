// occult
// Z-Buffer Renderer and Occlusion Tester Library
//

#include "occult.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>

#include "cpumath.h"
#include "cpu_3d.h" 
#include "vertex.h" // for vertex_t
#include "profiler.h"
#include "defer.h"

#include <malloc.h>
#include <memory.h>
#include <n64types.h>
#include <surface.h>

const float inv_subpixel_scale = 1.0f / SUBPIXEL_SCALE;

bool g_verbose_setup = false;
bool g_measure_error = false;
bool g_verbose_raster = false; // print depth at vertex pixels
bool g_verbose_early_out = false; // print coordinates of pixels that pass the depth test
bool g_verbose_visibility_tracking = false; // debug prints of last visible tri tracking
bool g_octagon_test = false; // intersect screenspace box with a 45 degree rotated box to get a stricter octagon test
bool g_draw_queries_hack = false; // render also queried objects to the depth buffer

bool config_shrink_silhouettes = true; // detect edges with flipped viewspace Z signs in each neighbor and add inner conservative flags
bool config_discard_based_on_tr_code = true;
bool config_inflate_rough_bounds = true;
bool config_report_near_clip_as_visible  = true; // if queried polygons clip the near plane, always report them as visible

enum {
    CLIP_ACTION_REJECT = 0,
    CLIP_ACTION_DO_IT = 1
};

typedef uint32_t occ_clip_action_t;

occ_clip_action_t g_near_clipping_action = CLIP_ACTION_DO_IT;

occ_culler_t *occ_alloc()
{
    occ_culler_t *culler = malloc(sizeof(occ_culler_t));
    memset(culler, 0, sizeof(occ_culler_t));
    cpu_glDepthRange(0.0, 1.0);
    return culler;
}

void occ_set_viewport(occ_culler_t *culler, int x, int y, int width, int height)
{
    culler->viewport.x = x;
    culler->viewport.y = y;
    culler->viewport.width = width;
    culler->viewport.height = height;
}

void occ_free(occ_culler_t *culler)
{
    free(culler);
}

void occ_set_view_and_projection(occ_culler_t *culler, matrix_t *view, matrix_t *proj)
{
    culler->view_matrix = *view;
    culler->proj = *proj;
    // 'mvp' is actually just 'view projection' matrix here AKA view-to-clip
    matrix_mult_full(&culler->mvp, proj, view);

    extract_planes_from_projmat(culler->mvp.m,
        culler->clip_planes[0],
        culler->clip_planes[1],
        culler->clip_planes[2],
        culler->clip_planes[3],
        culler->clip_planes[4],
        culler->clip_planes[5]);
    
    culler->camera_pos[0] = view->m[3][0];
    culler->camera_pos[1] = view->m[3][1];
    culler->camera_pos[2] = view->m[3][2];
}

void occ_next_frame(occ_culler_t *culler)
{
    culler->frame++;
}

void occ_clear_zbuffer(surface_t *zbuffer)
{
    // 0xffff = max depth
    for (int y = 0; y < zbuffer->height; y++) {
        memset(zbuffer->buffer + zbuffer->stride * y, 0xff, zbuffer->stride);
    }
}

bool isTopLeftEdge(vec2 a, vec2 b)
{
    // "In a counter-clockwise triangle, a top edge is an edge that is exactly horizontal
    //  and goes towards the left, i.e. its end point is left of its start point."
    if (a.y == b.y && b.x < a.x) return true;

    // "In a counter-clockwise triangle, a left edge is an edge that goes down,
    //  i.e. its end point is strictly below its start point."

    // But note that on the screen Y axis grows downwards so we check if the end point
    // Y coordinate is greater than start point Y.
    if (b.y > a.y) return true;
    return false;
}

int orient2d_subpixel(vec2 a, vec2 b, vec2 c)
{
    // We multiply two I.F fixed point numbers resulting in (I-F).2F format,
    // so we shift by F to the right to get the the result in I.F format again.
    return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) >> SUBPIXEL_BITS;
}

// Returns a normal vector pointing to the left of line segment ab. In screen space where y axis grows downwards.
vec2 get_edge_normal(vec2 a, vec2 b)
{
	return (vec2){ b.y - a.y, -(b.x - a.x) };
}

int compute_conservative_edge_bias(vec2 a, vec2 b, bool shrink)
{
    // See Tomas Akenine-Möller and Timo Aila, "A Simple Algorithm for Conservative and Tiled Rasterization", 2005.
    vec2 n = get_edge_normal(a, b);            // normal points inside the triangle, or the left of line segment 'ab'
    int edge_bias = (abs(n.x) + abs(n.y)) * 0.75; // factor chosen empirically to keep low-rez pixels inside high-rez RDP bounds
    if (shrink) {
        return -edge_bias;
    } else {
        return edge_bias;
    }
}

void draw_tri(
    vec2f v0f,
    vec2f v1f,
    vec2f v2f,
    float Z0f,
    float Z1f,
    float Z2f,
    occ_raster_flags_t flags,
    occ_raster_query_result_t* result,
    surface_t *zbuffer)
{
    const bool super_verbose = false;

    // The screenspace bias is added to empirically match SW depth map pixels to hi-rez RDP picture.
    vec2 v0 = {SUBPIXEL_SCALE * (v0f.x+SCREENSPACE_BIAS) + 0.5f, SUBPIXEL_SCALE * (v0f.y+SCREENSPACE_BIAS) + 0.5f};
    vec2 v1 = {SUBPIXEL_SCALE * (v1f.x+SCREENSPACE_BIAS) + 0.5f, SUBPIXEL_SCALE * (v1f.y+SCREENSPACE_BIAS) + 0.5f};
    vec2 v2 = {SUBPIXEL_SCALE * (v2f.x+SCREENSPACE_BIAS) + 0.5f, SUBPIXEL_SCALE * (v2f.y+SCREENSPACE_BIAS) + 0.5f};

    int area2x = -orient2d_subpixel(v0, v1, v2);
    if (area2x <= 0) {
        if (flags & RASTER_FLAG_BACKFACE_CULL) {
            return;
        } else if (area2x != 0) {
            SWAP(v1, v2);
            SWAP(Z1f, Z2f);
        } else {
            return;
        }
    }

    vec2 minb = {
        (min(v0.x, min(v1.x, v2.x)) >> SUBPIXEL_BITS),
        (min(v0.y, min(v1.y, v2.y)) >> SUBPIXEL_BITS)
        };

    vec2 maxb = {
        ((max(v0.x, max(v1.x, v2.x)) + SUBPIXEL_SCALE-1) >> SUBPIXEL_BITS),
        ((max(v0.y, max(v1.y, v2.y)) + SUBPIXEL_SCALE-1) >> SUBPIXEL_BITS)
        };

    if (minb.x < 0) minb.x = 0;
    if (minb.y < 0) minb.y = 0;
    if (maxb.x > zbuffer->width - 1) maxb.x = zbuffer->width - 1;
    if (maxb.y > zbuffer->height - 1) maxb.y = zbuffer->height - 1;

    if (minb.x >= maxb.x || minb.y >= maxb.y) return;

    vec2 p_start = { minb.x << SUBPIXEL_BITS, minb.y << SUBPIXEL_BITS };

    int A01 = -(v0.y - v1.y), B01 = -(v1.x - v0.x);
    int A12 = -(v1.y - v2.y), B12 = -(v2.x - v1.x);
    int A20 = -(v2.y - v0.y), B20 = -(v0.x - v2.x);

    int w0_row = -orient2d_subpixel(v1, v2, p_start);
    int w1_row = -orient2d_subpixel(v2, v0, p_start);
    int w2_row = -orient2d_subpixel(v0, v1, p_start);

    int bias0 = isTopLeftEdge(v1, v2) ? 0 : -1;
    int bias1 = isTopLeftEdge(v2, v0) ? 0 : -1;
    int bias2 = isTopLeftEdge(v0, v1) ? 0 : -1;

    // Adjust edge functions based on triangle edge normals.
    assert(!((flags & RASTER_FLAG_SHRINK_EDGE_12) && (flags & RASTER_FLAG_EXPAND_EDGE_12)));
    assert(!((flags & RASTER_FLAG_SHRINK_EDGE_20) && (flags & RASTER_FLAG_EXPAND_EDGE_20)));
    assert(!((flags & RASTER_FLAG_SHRINK_EDGE_01) && (flags & RASTER_FLAG_EXPAND_EDGE_01)));

    if (flags & (RASTER_FLAG_SHRINK_EDGE_12 | RASTER_FLAG_EXPAND_EDGE_12)) {
        bias0 += compute_conservative_edge_bias(v1, v2, flags & RASTER_FLAG_SHRINK_EDGE_12);
    }
    if (flags & (RASTER_FLAG_SHRINK_EDGE_20 | RASTER_FLAG_EXPAND_EDGE_20)) {
        bias1 += compute_conservative_edge_bias(v2, v0, flags & RASTER_FLAG_SHRINK_EDGE_20);
    }
    if (flags & (RASTER_FLAG_SHRINK_EDGE_01 | RASTER_FLAG_EXPAND_EDGE_01)) {
        bias2 += compute_conservative_edge_bias(v0, v1, flags & RASTER_FLAG_SHRINK_EDGE_01);
    }

    w0_row += bias0;
    w1_row += bias1;
    w2_row += bias2;

    // Prepare Z deltas
    // Prepare inputs to a formula solved via a 3D plane equation with subpixel XY coords and Z.
    // See https://tutorial.math.lamar.edu/classes/calciii/eqnsofplanes.aspx
    // and the "Interpolated values" section at
    // https://fgiesen.wordpress.com/2011/07/08/a-trip-through-the-graphics-pipeline-2011-part-7/

    vec3f v01 = vec3f_sub((vec3f){v1.x, v1.y, Z1f}, (vec3f){v0.x, v0.y, Z0f});
    vec3f v02 = vec3f_sub((vec3f){v2.x, v2.y, Z2f}, (vec3f){v0.x, v0.y, Z0f});

    vec3f N = cross3df(v01, v02);
    N.z *= inv_subpixel_scale; // Scale back the fixed point scale multiply inside cross3df

    // N is now in subpixel scale, divide again to bring it to per-pixel scale
    N.x *= inv_subpixel_scale;
    N.y *= inv_subpixel_scale;
    N.z *= inv_subpixel_scale;
    float dZdx = -N.x / N.z;
    float dZdy = -N.y / N.z;

    // Compute Z value at the starting pixel at (minb.x, minb.y). It's computed by extrapolating the Z value at vertex 0.
    float Zf_row = Z0f
        + ((minb.x * SUBPIXEL_SCALE - v0.x) / SUBPIXEL_SCALE) * dZdx
        + ((minb.y * SUBPIXEL_SCALE - v0.y) / SUBPIXEL_SCALE) * dZdy;


    // Write a constant depth for small triangles.
    // It's a a workaround for small triangle delta precision problems.
    // Small triangle area is less than one pixel^2.
    const int min_triangle_area = SUBPIXEL_SCALE * SUBPIXEL_SCALE * 2;
    const bool is_small = abs(area2x) < min_triangle_area;

    if (is_small) {
        dZdx = 0.f;
        dZdy = 0.f;
        if (flags & RASTER_FLAG_WRITE_DEPTH) {
            // Writing a depth? Make it conservative by writing farthest depth.
            Zf_row = max(Z0f, max(Z1f, Z2f));
        } else {
            // Testing the depth? Test only the closest vertex value.
            Zf_row = min(Z0f, min(Z1f, Z2f));
        }
    }

    if (false) {
        debugf("area2x: %d\n", area2x);
        debugf("Z0f: %f, Z1f: %f, Z2f: %f\n", Z0f, Z1f, Z2f);
        debugf("Zf_row: %f\n", Zf_row);

                            debugf("minb: (%d, %d), maxb: (%d, %d), size: %dx%d\n", minb.x, minb.y, maxb.x, maxb.y, maxb.x-minb.x, maxb.y-minb.y);

                            debugf("v0f: (%f, %f), v1f: (%f, %f), v2f: (%f, %f)\n",
                                   v0f.x, v0f.y, v1f.x, v1f.y, v2f.x, v2f.y);
                            debugf("v0: (%d, %d), v1: (%d, %d), v2: (%d, %d)\n",
                                   v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);

                            debugf("v01: (%f, %f, %f), v02: (%f, %f, %f)\n",
                                   v01.x, v01.y, v01.z, v02.x, v02.y, v02.z);
    }

    // Fixed point deltas for the integer-only inner loop. We use DELTA_BITS of precision.
    int32_t Z_row_fixed = (int32_t)(DELTA_SCALE * Zf_row);
    int32_t dZdx_fixed = (int32_t)(DELTA_SCALE * dZdx);
    int32_t dZdy_fixed = (int32_t)(DELTA_SCALE * dZdy);
    int32_t max_Z_fixed = (int32_t)(DELTA_SCALE * 1.0f) - 1;

    // Problem: Negative biases make queries super conservative and you can see objects behind walls!
    if (flags & RASTER_FLAG_NEG_SLOPE_BIAS) {
        // We can make sure each interpolated depth value is less or equal to the actual triangle min depth with this bias.
        // Note: this will produce values one-pixel's worth under the triangle's original depth range.
        // See MaxDepthSlope in https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#DepthBias
        Z_row_fixed -= max(abs(dZdx_fixed), abs(dZdy_fixed));
    } else if (flags & RASTER_FLAG_POS_SLOPE_BIAS) {
        Z_row_fixed += max(abs(dZdx_fixed), abs(dZdy_fixed));
    }

    assert(!((flags & RASTER_FLAG_NEG_SLOPE_BIAS) && (flags & RASTER_FLAG_POS_SLOPE_BIAS))); // Mutually exclusive flags

    if (flags & RASTER_FLAG_REPORT_VISIBILITY) {
        assert(result);
        result->visible = false;
    }

    if (super_verbose) {
        debugf("Z_row = %f, Z_row_fixed = %ld\n", Zf_row, Z_row_fixed);
    }

    // Only 'p', 'minb' and 'maxb' are in whole-pixel coordinates here. Others are all in sub-pixel coordinates.
    vec2 p = {-1, -1};

    for (p.y = minb.y; p.y <= maxb.y; p.y++) {
        int32_t w0 = w0_row;
        int32_t w1 = w1_row;
        int32_t w2 = w2_row;
        int32_t Z_incr_fixed = Z_row_fixed;

        for (p.x = minb.x; p.x <= maxb.x; p.x++) {
            if (super_verbose) {
                debugf("| %s (%-2d, %-2d) %-8f ", (w0 | w1 | w2) >= 0 ? "X" : ".", p.x, p.y, Z_incr_fixed/(float)DELTA_SCALE);
            }
            if ((w0 | w1 | w2) >= 0) {
                int bias = 0;

                if (flags & RASTER_FLAG_ROUND_DEPTH_UP) {
                    bias = (1 << DELTA_BITS) - 1;
                }

                uint16_t depth;

                if (Z_incr_fixed < max_Z_fixed) {
                    depth = (Z_incr_fixed + bias) >> (DELTA_BITS - 16);
                } else {
                    depth = 0xffff - 1;
                }

                u_uint16_t *buf = ZBUFFER_UINT_PTR_AT(zbuffer, p.x, p.y);

                if (depth < *buf) {
                    if (flags & RASTER_FLAG_WRITE_DEPTH) {
                        *buf = depth;
                    }
                    if (flags & RASTER_FLAG_REPORT_VISIBILITY) {
                        assert(result);
                        result->visible = true;
                        result->x = p.x;
                        result->y = p.y;
                        result->depth = depth;
                        if (g_verbose_early_out) {
                            debugf("\nvisible (%d < %d) at (%d, %d), v0=(%d,%d)\n", depth, *buf, p.x, p.y, v0.x>>SUBPIXEL_BITS, v0.y>>SUBPIXEL_BITS);
                        }
                        if (super_verbose) {
                            debugf("Z_incr_fixed: %ld\n", Z_incr_fixed);
                            debugf("relative: (%d, %d)\n", p.x - minb.x, p.y - minb.y);
                            debugf("minb: (%d, %d), maxb: (%d, %d), size: %dx%d\n", minb.x, minb.y, maxb.x, maxb.y, maxb.x-minb.x, maxb.y-minb.y);
                            debugf("z0f: %f, z1f: %f, z2f: %f\n", Z0f, Z1f, Z2f);
                            debugf("v0f: (%f, %f), v1f: (%f, %f), v2f: (%f, %f)\n",
                                   v0f.x, v0f.y, v1f.x, v1f.y, v2f.x, v2f.y);
                            debugf("v0: (%d, %d), v1: (%d, %d), v2: (%d, %d)\n",
                                   v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);

                            debugf("v01: (%f, %f, %f), v02: (%f, %f, %f)\n",
                                   v01.x, v01.y, v01.z, v02.x, v02.y, v02.z);
                            debugf("N: (%f, %f, %f)\n",
                                   N.x, N.y, N.z);
                            debugf("dZdx, dZdy: (%f, %f)\n", dZdx, dZdy);
                            debugf("dZdx_fixed, dZdy_fixed: (%ld, %ld)\n", dZdx_fixed, dZdy_fixed);

                            debugf("zf_row2: %f\n", Zf_row);
                            float lambda0 = (float)(w0 - bias0) / area2x;
                            float lambda1 = (float)(w1 - bias1) / area2x;
                            float lambda2 = (float)(w2 - bias2) / area2x;
                            float Zf_bary = lambda0 * Z0f + lambda1 * Z1f + lambda2 * Z2f;
                            debugf("Zf_bary = %f; L0=%f; L1=%f; L2=%f;\n", Zf_bary, lambda0, lambda1, lambda2);
                            debugf("w0=%ld; w1=%ld; w2=%ld;\n", w0, w1, w2);

                            debugf("A01 = %d; B01 = %d\n", A01, B01);
                            debugf("A12 = %d; B12 = %d\n", A12, B12);
                            debugf("A20 = %d; B20 = %d\n", A20, B20);
                            debugf("area2x: %d\n", area2x);
                            debugf("\n");
                            debugf("set_grid(min=(%d, %d), max=(%d, %d))\n", minb.x, minb.y, maxb.x+1, maxb.y+1);
                            debugf("draw_grid()\n");
                            debugf("draw_tri([(%f, %f), (%f, %f), (%f, %f)])\n",
                                   v0f.x, v0f.y, v1f.x, v1f.y, v2f.x, v2f.y);

                            while (true) {}; // HACK
                        }
                    }

                    if (flags & RASTER_FLAG_EARLY_OUT) {
                        return;
                    }
                }

            }

            w0 += A12;
            w1 += A20;
            w2 += A01;
            Z_incr_fixed += dZdx_fixed;
        }

        w0_row += B12;
        w1_row += B20;
        w2_row += B01;
        Z_row_fixed += dZdy_fixed;

        if (super_verbose) { debugf("\n"); }
    }
}

#define OCC_MAX_MESH_VERTEX_COUNT (24) // enough for a cube with duplicated verts
#define OCC_MAX_MESH_INDEX_COUNT (30)

float matrix_mult_z_only(const matrix_t *m, const float *v)
{
    return m->m[0][2] * v[0] + m->m[1][2] * v[1] + m->m[2][2] * v[2] + m->m[3][2] * v[3];
}

void occ_draw_indexed_mesh_flags(occ_culler_t *occ, surface_t *zbuffer, const matrix_t *model_xform, const occ_mesh_t* mesh,
                                vec3f* tri_normals, uint16_t* tri_neighbors,
                                occ_target_t* target, const occ_raster_flags_t flags, occ_raster_query_result_t* query_result)
{
    // We render a viewport (x,y,x+w,y+h) in zbuffer's pixel space
    cpu_glViewport(
        occ->viewport.x,
        occ->viewport.y,
        occ->viewport.width,
        occ->viewport.height,
        zbuffer->width,
        zbuffer->height);

    // Transform all vertices first
    prof_begin(REGION_TRANSFORM);

    matrix_t* mvp = &occ->mvp;
    matrix_t* modelview = NULL;
    matrix_t mvp_new, modelview_new;

    if (!model_xform) {
        // No per-object global transform: use defaults
        mvp = &occ->mvp;
        modelview = &occ->view_matrix;
    } else {
        // Otherwise compute new ModelView and MVP matrices
        mvp = &mvp_new;
        modelview = &modelview_new;
        matrix_mult_full(mvp, &occ->mvp, model_xform);
        matrix_mult_full(modelview, &occ->view_matrix, model_xform);
    }

    cpu_vtx_t all_verts[OCC_MAX_MESH_VERTEX_COUNT] = {0};
    bool tri_faces_camera[OCC_MAX_MESH_INDEX_COUNT];

    if (tri_normals) {
        int num_tris = mesh->num_indices/3;
        for (int i = 0; i < num_tris; i++) {
            float n[4] = {tri_normals[i].x, tri_normals[i].y, tri_normals[i].z, 0.f};
            //TODO use inverse transpose if non-uniform scale?
            float view_z = matrix_mult_z_only(modelview, n);
            tri_faces_camera[i] = view_z > 0;
        }
    }

    for (uint32_t i = 0; i < mesh->num_vertices; i++) {
        all_verts[i].obj_attributes.position[0] = mesh->vertices[i].position[0];
        all_verts[i].obj_attributes.position[1] = mesh->vertices[i].position[1];
        all_verts[i].obj_attributes.position[2] = mesh->vertices[i].position[2];
        all_verts[i].obj_attributes.position[3] = 1.0f; // Q: where does cpu pipeline set this?

        cpu_vertex_pre_tr(&all_verts[i], mvp);
        cpu_vertex_calc_screenspace(&all_verts[i]);
    }

    prof_end(REGION_TRANSFORM);

    int num_tris_drawn = 0;
    int ofs = 0;
    if (target) {
        ofs = target->last_visible_idx; // start from where we last found a visible pixel
        target->last_visible_idx = 0;
    }

    for (int is = 0; is < mesh->num_indices; is += 3) {
        int wrapped_is = (is + ofs) % mesh->num_indices; // start from 'ofs' but render the whole mesh
        const uint16_t *inds = &mesh->indices[wrapped_is];
        cpu_vtx_t verts[3] = {all_verts[inds[0]], all_verts[inds[1]], all_verts[inds[2]]};
        cpu_vtx_t clipping_cache[CLIPPING_CACHE_SIZE];
        cpu_clipping_list_t clipping_list = {.count = 0};

        if (g_verbose_setup) {
            debugf("pos=(%f, %f, %f, %f), cs_pos=(%f, %f, %f, %f), tr_code=%d\n",
                   verts[0].obj_attributes.position[0],
                   verts[0].obj_attributes.position[1],
                   verts[0].obj_attributes.position[2],
                   verts[0].obj_attributes.position[3],
                   verts[0].cs_pos[0],
                   verts[0].cs_pos[1],
                   verts[0].cs_pos[2],
                   verts[0].cs_pos[3],
                   verts[0].tr_code);
            debugf("screen_pos: (%f, %f), depth=%f, inv_w=%f\n",
                   verts[0].screen_pos[0],
                   verts[0].screen_pos[1],
                   verts[0].depth,
                   verts[0].inv_w);
        }

        uint8_t tr_codes = 0xff;
        tr_codes &= verts[0].tr_code;
        tr_codes &= verts[1].tr_code;
        tr_codes &= verts[2].tr_code;

        // Trivial rejection
        if (tr_codes) {
            continue;
        }

        uint32_t edge_flag_mask = 0;

        if (tri_normals) {
            int tri_idx = is/3;
            if (!tri_faces_camera[tri_idx] && (flags & RASTER_FLAG_BACKFACE_CULL)) {
                continue;
            }

            if (tri_neighbors && config_shrink_silhouettes) {
                // Silhouette edges join triangles with different view space Z signs
                for (int j = 0; j < 3; j++) {
                    uint16_t other = tri_neighbors[tri_idx * 3 + j];
                    if (other != OCC_NO_EDGE_NEIGHBOR) {
                        if (!tri_faces_camera[other]) {
                            edge_flag_mask |= (RASTER_FLAG_SHRINK_EDGE_01 << j);
                            break;
                        }
                    }
                }
            }
        }

        const int NEAR_PLANE_INDEX = 2;
        const int FAR_PLANE_INDEX = 5;
        const bool clips_near = (verts[0].tr_code | verts[1].tr_code | verts[2].tr_code) & (1 << NEAR_PLANE_INDEX);
        const bool clips_far = (verts[0].tr_code | verts[1].tr_code | verts[2].tr_code) & (1 << FAR_PLANE_INDEX);

        if (config_report_near_clip_as_visible) {
            // If we are drawing for a precise query and we hit a near clip: report as visible
            if (clips_near && (flags & RASTER_FLAG_REPORT_VISIBILITY) && !(flags & RASTER_FLAG_WRITE_DEPTH)) {
                assert(query_result && "must pass in a non-NULL query_result if asking for a visibility result");
                if (query_result) {
                    query_result->visible = true;
                    query_result->num_tris_drawn = num_tris_drawn;
                    return;
                }
            }
        }

        if (clips_near) {
            if (g_near_clipping_action == CLIP_ACTION_REJECT) {
                continue;
            } else if (g_near_clipping_action == CLIP_ACTION_DO_IT) {
                // tr_code   = clip against screen bounds, used for rejection
                // clip_code = clipped against guard bands, used for actual clipping
                //
                // We clip only against the near plane so they are the same.

                verts[0].clip_code = verts[0].tr_code;
                verts[1].clip_code = verts[1].tr_code;
                verts[2].clip_code = verts[2].tr_code;

                cpu_gl_clip_triangle(&verts[0], &verts[1], &verts[2], (1 << NEAR_PLANE_INDEX), clipping_cache, &clipping_list);
            } else {
                debugf("Invalid clip action %lu\n", g_near_clipping_action);
                assert(false);
            }
        }

        if (clips_far && (flags & RASTER_FLAG_DISCARD_FAR)) {
            // Reject triangles that touch the far clip plane when rendering occluders. We can't store their farthest depth anyway.
            continue;
        }

        if (clipping_list.count == 0) {
            prof_begin(REGION_RASTERIZATION);
            draw_tri(
                (vec2f){verts[0].screen_pos[0], verts[0].screen_pos[1]},
                (vec2f){verts[1].screen_pos[0], verts[1].screen_pos[1]},
                (vec2f){verts[2].screen_pos[0], verts[2].screen_pos[1]},
                verts[0].depth, verts[1].depth, verts[2].depth,
                flags | edge_flag_mask, query_result,
                zbuffer);
            prof_end(REGION_RASTERIZATION);
            num_tris_drawn++;
        } else {
            prof_begin(REGION_RASTERIZATION);
            for (uint32_t i = 1; i < clipping_list.count; i++) {
                vec2f sv[3];
                sv[0].x = clipping_list.vertices[0]->screen_pos[0];
                sv[0].y = clipping_list.vertices[0]->screen_pos[1];
                sv[1].x = clipping_list.vertices[i - 1]->screen_pos[0];
                sv[1].y = clipping_list.vertices[i - 1]->screen_pos[1];
                sv[2].x = clipping_list.vertices[i]->screen_pos[0];
                sv[2].y = clipping_list.vertices[i]->screen_pos[1];

                draw_tri(
                    sv[0], sv[1], sv[2],
                    clipping_list.vertices[0]->depth, clipping_list.vertices[i - 1]->depth, clipping_list.vertices[i]->depth,
                    flags | edge_flag_mask, query_result,
                    zbuffer);
                num_tris_drawn++;
            }
            prof_end(REGION_RASTERIZATION);
        }

        if (query_result) { query_result->num_tris_drawn = num_tris_drawn; }

        // Early out from all triangles even if only one of them was visible
        if ((flags & RASTER_FLAG_EARLY_OUT) && query_result && query_result->visible) {
            target->last_visible_idx = wrapped_is;
            if (target && g_verbose_visibility_tracking) {
                debugf("was visible at wrapped_is = %d = (%d+%d) %% %lu\n", wrapped_is, is, ofs, mesh->num_indices);
            }
            return;
        }
    }
}

void occ_draw_mesh(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t *mesh, const matrix_t *model_xform)
{
	occ_draw_indexed_mesh_flags(occ, zbuffer, model_xform, mesh,
        /* mesh_normals = */ NULL,
        /* mesh_neighbors = */ NULL,
        /* target = */ NULL,
        OCC_RASTER_FLAGS_DRAW,
        /* query_result = */ NULL);
}

void occ_draw_hull(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t* hull, const matrix_t *model_xform, occ_raster_query_result_t* query, occ_occluder_flags_t flags)
{
    occ_raster_flags_t raster_flags = OCC_RASTER_FLAGS_DRAW;
    if (flags & OCCLUDER_TWO_SIDED) raster_flags &= ~RASTER_FLAG_BACKFACE_CULL;
	occ_draw_indexed_mesh_flags(occ, zbuffer, model_xform, &hull->mesh, hull->tri_normals, hull->neighbors, NULL, raster_flags, query);
}

vec2f rotate_xy_coords_45deg(float x, float y) {
    //  [ b.x ] = [ +1 -1 ] [ x ]
    //  [ b.y ]   [ -1 -1 ] [ y ]
    return (vec2f){x - y, -x - y};
}

bool occ_box2df_inside(occ_box2df_t* box, vec2f* p) {
    if (p->x < box->lo.x || p->x >= box->hi.x) return false;
    if (p->y < box->lo.y || p->y >= box->hi.y) return false;
    return true;
}

// [minX, maxX), [minY, maxY), i.e. upper bounds are exclusive.
bool occ_check_pixel_box_visible(occ_culler_t *occ, surface_t *zbuffer,
                                 uint16_t depth, int minX, int minY, int maxX, int maxY,
                                 occ_box2df_t* in_rotated_box, occ_result_box_t *out_box)
{

    prof_begin(REGION_TESTING);
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > zbuffer->width - 1) maxX = zbuffer->width - 1;
    if (maxY > zbuffer->height - 1) maxY = zbuffer->height - 1;


    if (out_box) {
        out_box->minX = minX;
        out_box->minY = minY;
        out_box->maxX = maxX;
        out_box->maxY = maxY;
        out_box->udepth = OCC_MAX_Z;
        out_box->hitX = -1;
        out_box->hitY = -1;
    }

    for (int y = minY; y < maxY; y++) {
        for (int x = minX; x < maxX; x++) {
            if (in_rotated_box) {
                vec2f r = rotate_xy_coords_45deg(x, y); //TODO rounding?
                if (!occ_box2df_inside(in_rotated_box, &r)) {
                    continue;
                }
            }
            u_uint16_t *buf = ZBUFFER_UINT_PTR_AT(zbuffer, x, y);
            if (depth <= *buf) {
                if (out_box) {
                    out_box->udepth = *buf;
                    out_box->hitX = x;
                    out_box->hitY = y;
                }
                prof_end(REGION_TESTING);
                return true;
            }
        }
    }

    prof_end(REGION_TESTING);
    return false; // Every box pixel was behind the Z-buffer
}

bool occ_check_mesh_visible_rough(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t* mesh, const matrix_t *model_xform, occ_result_box_t *out_box)
{
    prof_begin(REGION_TESTING);
    // 1. transform and project each point to screen space
    // 2. compute the XY bounding box
    // 3. compute min Z
    // 3. check the bounding box against zbuffer with the given minZ

    // octagon test:
    // 1. for each vertex, compute unscaled rotated screen coordinates:
    //  [ xr ] = [ +1 -1 ] [ x ]
    //  [ yr ]   [ -1 -1 ] [ y ]
    // 2. keep track of AABB of those
    // 3. at query time, for each pixel, compute rotated unscaled rotated screen coordinates
    // 4. check pixel only if inside the rotated coords

    matrix_t* mvp = NULL;
    matrix_t mvp_new;

    if (model_xform) {
        prof_begin(REGION_TRANSFORM);
        mvp = &mvp_new;
        matrix_mult_full(mvp, &occ->mvp, model_xform);
        prof_end(REGION_TRANSFORM);
    }
    else {
        mvp = &occ->mvp;
    }

    float minZ = __FLT_MAX__;
    float minX = __FLT_MAX__;
    float maxX = -__FLT_MAX__;
    float minY = __FLT_MAX__;
    float maxY = -__FLT_MAX__;

    occ_box2df_t oct_box = {{__FLT_MAX__, __FLT_MAX__}, {-__FLT_MAX__, -__FLT_MAX__}};

    for (int iv = 0; iv < mesh->num_vertices; iv++) {
        prof_begin(REGION_TRANSFORM);
        cpu_vtx_t vert = {};
        vert.obj_attributes.position[0] = mesh->vertices[iv].position[0];
        vert.obj_attributes.position[1] = mesh->vertices[iv].position[1];
        vert.obj_attributes.position[2] = mesh->vertices[iv].position[2];
        vert.obj_attributes.position[3] = 1.0f;

        cpu_vertex_pre_tr(&vert, mvp);
        cpu_vertex_calc_screenspace(&vert);
        vert.screen_pos[0] += SCREENSPACE_BIAS;
        vert.screen_pos[1] += SCREENSPACE_BIAS;
        prof_end(REGION_TRANSFORM);
        //if (vert.depth < 0.f) return true; // HACK: any vertex behind camera makes the object visible

        if (vert.depth > 0.f) {
            minZ = min(minZ, vert.depth);
            minX = min(minX, vert.screen_pos[0]);
            maxX = max(maxX, vert.screen_pos[0]);
            minY = min(minY, vert.screen_pos[1]);
            maxY = max(maxY, vert.screen_pos[1]);
        }

        if (g_octagon_test) {
            vec2f pr = rotate_xy_coords_45deg(vert.screen_pos[0], vert.screen_pos[1]);
            oct_box.lo.x = min(oct_box.lo.x, pr.x);
            oct_box.lo.y = min(oct_box.lo.y, pr.y);
            oct_box.hi.x = max(oct_box.hi.x, pr.x);
            oct_box.hi.y = max(oct_box.hi.y, pr.y);
        }
    }

    if (config_inflate_rough_bounds) {
        // Inflate bounds by 1 pixel to make it work with outer-conservative rasterization.
        // In check_pixel_box_visible() these are clipped to screen bounds.
        minX -= 1;
        minY -= 1;
        maxX += 1;
        maxY += 1;
    }

    // Upper bounds are exclusive so round up to include the pixels right at the edge as well.
    //maxX = ceilf(maxX);
    //maxY = ceilf(maxY);

    if (g_octagon_test) {
        oct_box.lo.x = floorf(oct_box.lo.x);
        oct_box.lo.y = floorf(oct_box.lo.y);
        oct_box.hi.x = ceilf(oct_box.hi.x);
        oct_box.hi.y = ceilf(oct_box.hi.y);
    }

    uint16_t udepth = FLOAT_TO_U16(minZ);
    occ_box2df_t* rotated_box = NULL;
    if (g_octagon_test) {
        rotated_box = &oct_box;
    }
    prof_end(REGION_TESTING);
    return occ_check_pixel_box_visible(occ, zbuffer, udepth, minX, minY, maxX, maxY, rotated_box, out_box);
}

bool occ_check_hull_visible_precise(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t *hull, const matrix_t *model_xform,
                                    occ_target_t *target, occ_raster_query_result_t *out_result)
{
    occ_raster_query_result_t result = {};
    uint32_t flags = OCC_RASTER_FLAGS_QUERY;
    if (g_draw_queries_hack) {
        flags &= ~RASTER_FLAG_EARLY_OUT;
        flags |= RASTER_FLAG_WRITE_DEPTH;
    }
    occ_draw_indexed_mesh_flags(occ, zbuffer, model_xform, &hull->mesh, NULL, NULL, target, flags, &result);

    if (out_result) {
        *out_result = result;
    }
    return result.visible;
}


bool occ_check_target_visible(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t* hull, const matrix_t *model_xform,
occ_target_t* target, occ_raster_query_result_t *out_result)
{
    // debugf("%s mesh=%p, model_xform=%p, target=%p, out_result=%p\n", __FUNCTION__, mesh, model_xform, target ,out_result);
    occ_result_box_t box = {};
    bool pass = true;
    const bool force_rough_only = false;

    // Do a rough check only if target was not visible last time.
    if (target->last_visible_frame != occ->frame - 1 || force_rough_only) {
        pass = occ_check_mesh_visible_rough(occ, zbuffer, &hull->mesh, model_xform, &box);

        if (!pass) {
            if (out_result) {
                out_result->visible = false;
                out_result->x = box.hitX;
                out_result->y = box.hitY;
                out_result->depth = box.udepth;
            }
            if (g_verbose_visibility_tracking) {
                debugf("coarse fail\n");
            }
            return false;
        }
    }

    if (force_rough_only) {
        goto finish;
    }

    pass = occ_check_hull_visible_precise(occ, zbuffer, hull, model_xform, target, out_result);

    if (g_verbose_visibility_tracking) {
        debugf("tris drawn: %d, last_visible = %d\n", out_result->num_tris_drawn, target->last_visible_idx);
    }

    finish:
    if (pass) {
        target->last_visible_frame = occ->frame;
    } else {
        if (g_verbose_visibility_tracking) {
            debugf("precise fail\n");
        }
    }

    return pass;
}

bool occ_hull_from_flat_mesh(const occ_mesh_t* mesh_in, occ_hull_t* hull_out)
{
    occ_mesh_t* m = &hull_out->mesh;
    vertex_t* scratch = malloc(sizeof(vertex_t) * OCC_MAX_MESH_VERTEX_COUNT);
    DEFER(free(scratch));
    int* old_to_new = malloc(sizeof(int) * mesh_in->num_vertices);
    DEFER(free(old_to_new));

    // Deduplicate vertices based on position only

    m->num_vertices = 0; // will be incremented in the loop below

    bool verbose = false;

    for (uint32_t vertex_idx=0; vertex_idx < mesh_in->num_vertices; vertex_idx++) {
        float f[3] = {
            mesh_in->vertices[vertex_idx].position[0],
            mesh_in->vertices[vertex_idx].position[1],
            mesh_in->vertices[vertex_idx].position[2]
        };

        int new_idx = -1;
        for (int slot=0; slot < m->num_vertices; slot++) {
            if (scratch[slot].position[0] == f[0]
            && scratch[slot].position[1] == f[1]
            && scratch[slot].position[2] == f[2]) {
                new_idx = slot;
                break;
            }
        }

        if (new_idx == -1) {
            if (m->num_vertices == OCC_MAX_MESH_VERTEX_COUNT) {
                debugf("max vertex count limit %d reached\n", OCC_MAX_MESH_VERTEX_COUNT);
                return false;
            }

            new_idx = m->num_vertices++;
            scratch[new_idx].position[0] = f[0];
            scratch[new_idx].position[1] = f[1];
            scratch[new_idx].position[2] = f[2];

        }

        old_to_new[vertex_idx] = new_idx;
    }

    if (verbose) {
        for (int i = 0; i < mesh_in->num_vertices; i++) {
            debugf("old_to_new[%d] = %d\n", i, old_to_new[i]);
        }
    }

    m->indices = malloc(sizeof(uint16_t) * mesh_in->num_indices);

    for (uint32_t i=0; i < mesh_in->num_indices; i++) {
        m->indices[m->num_indices++] = old_to_new[mesh_in->indices[i]];
    }

    m->vertices = malloc(m->num_vertices * sizeof(vertex_t));
    memcpy(m->vertices, scratch, m->num_vertices * sizeof(vertex_t));

    if (verbose) {
        debugf("index buffer:\n");
        for (int i = 0; i < m->num_indices; i++) {
            debugf("%d, ", m->indices[i]);
            if (i%3 == 2) debugf(" ");
        }
        debugf("\n");
        debugf("vertex count before: %lu, after: %lu. %s\n", mesh_in->num_vertices, m->num_vertices, m->num_vertices < mesh_in->num_vertices/2 ? "nice!" : "");
    }

    // Compute normals for the new mesh
    assert(m->num_indices % 3 == 0);
    int num_tris = m->num_indices / 3;
    hull_out->tri_normals = malloc(num_tris * sizeof(vec3f));
    for (int tri_idx = 0; tri_idx < num_tris; tri_idx++) {
        uint16_t *inds = &m->indices[tri_idx * 3];
        vec3f v0 = (vec3f){m->vertices[inds[0]].position[0], m->vertices[inds[0]].position[1], m->vertices[inds[0]].position[2]};
        vec3f v1 = (vec3f){m->vertices[inds[1]].position[0], m->vertices[inds[1]].position[1], m->vertices[inds[1]].position[2]};
        vec3f v2 = (vec3f){m->vertices[inds[2]].position[0], m->vertices[inds[2]].position[1], m->vertices[inds[2]].position[2]};
        vec3f v01 = vec3f_sub(v1, v0);
        vec3f v02 = vec3f_sub(v2, v0);
        vec3f N_unnormalized = cross3df(v01, v02);
        vec3f N;
        cpu_gl_normalize(&N.x, &N_unnormalized.x);
        hull_out->tri_normals[tri_idx] = N;
    }

    if (verbose) {
        debugf("Triangle normals:\n");
        for (int tri_idx = 0; tri_idx < num_tris; tri_idx++) {
            vec3f N = hull_out->tri_normals[tri_idx];
            debugf("[%-2d] (%f, %f, %f)\n", tri_idx, N.x, N.y, N.z);
        }
    }

    // Compute neighbors for each edge with brute force.

    // Two triangles are neighbors if they both have an edge with the same vertex indices.
    // We canonicalize the edges below by putting the smaller index first. This way
    // they can be compared regardless of triangle's winding order.

    hull_out->neighbors = malloc(num_tris * 3 * sizeof(uint16_t));
    uint16_t* neighbors = hull_out->neighbors;
    const uint16_t* indices = hull_out->mesh.indices;

    for (int tri_a = 0; tri_a < num_tris; tri_a++) {
        for (int i = 0; i < 3; i++) {
            uint16_t edge_a0 = indices[tri_a * 3 + i];              // vertex at  i
            uint16_t edge_a1 = indices[tri_a * 3 + ((1 << i) & 3)]; // vertex at (i+1) % 3
            if (edge_a0 > edge_a1) {
                SWAP(edge_a0, edge_a1);
            }

            bool neighbor_found = false;

            for (int tri_b = 0; tri_b < num_tris; tri_b++) {
                if (tri_a == tri_b) continue;
                for (int j = 0; j < 3; j++) {
                    uint16_t edge_b0 = indices[tri_b * 3 + j];
                    uint16_t edge_b1 = indices[tri_b * 3 + ((1 << j) & 3)];
                    if (edge_b0 > edge_b1) {
                        SWAP(edge_b0, edge_b1);
                    }

                    if (edge_a0 == edge_b0 && edge_a1 == edge_b1) {
                        neighbors[tri_a * 3 + i] = tri_b;
                        neighbor_found = true;
                        break;
                    }
                }

                if (neighbor_found) break;
            }

            if (!neighbor_found) {
                neighbors[tri_a * 3 + i] = OCC_NO_EDGE_NEIGHBOR;
            }
        }
    }

    // Check validity of the neighbor array
    for (int tri_idx = 0; tri_idx < num_tris; tri_idx++) {
        for (int i = 0; i < 3; i++) {
            uint16_t other = neighbors[tri_idx * 3 + i];
            if (other != OCC_NO_EDGE_NEIGHBOR) {
                bool found = false;
                for (int j = 0; j < 3; j++) {
                    if (neighbors[other * 3 + j] == tri_idx) {
                        found = true;
                    }
                }
                if (!found) {
                    debugf("Error: Neighbor array wasn't symmetric at tri_idx=%d, i=%d, other=%u\n", tri_idx, i, other);
                    free(hull_out->neighbors);
                    hull_out->neighbors = NULL;
                    return false;
                }
            }
        }

    }

    // Compute max distance from origin
    hull_out->max_radius = 0.0;

    for (uint32_t i=0;i<m->num_vertices;i++) {
        float* p = &m->vertices[i].position[0];
        float radius = sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);

        hull_out->max_radius = max(radius, hull_out->max_radius);
    }

    assert(hull_out->max_radius > 1e-3);

    if (verbose) {
        debugf("max_radius: %f\n", hull_out->max_radius);
    }

    if (verbose) {
        for (int tri_idx = 0; tri_idx < num_tris; tri_idx++) {
            debugf("tri %-2d: ", tri_idx);
            for (int i = 0; i < 3; i++) {
                uint16_t neighbor = neighbors[tri_idx * 3 + i];
                debugf("%-5u ", neighbor);
            }
            debugf("\n");
        }
    }

    return true;
}


// model64 interop

#include "../../src/model64_internal.h"

bool model_to_occ_mesh(model64_t* model, mesh_t* mesh_in, occ_mesh_t* mesh_out)
{
    bool verbose = false;

    primitive_t* prim = &mesh_in->primitives[0];
    attribute_t* attr = &prim->position;

    if (verbose) {
        debugf("Num primitives: %lu\n", mesh_in->num_primitives);
        debugf("Num vertices: %lu\n", prim->num_vertices);
        debugf("Num indices: %lu\n", prim->num_indices);

        debugf("Primitive 0 pos attribute:\nsize=%lu, type=%lu, stride=%lu, pointer=%p\n",
               attr->size,
               attr->type,
               attr->stride,
               attr->pointer);
    }
    assert(mesh_in->num_primitives == 1 && "we can handle only a single primitive per mesh");

    assert(prim->position.type == GL_HALF_FIXED_N64);

    int bits = prim->vertex_precision;
    float scale = 1.0f / (1 << bits);
    if (verbose) debugf("position bits: %d, scale: %f\n", bits, scale);

    attribute_t* position = &prim->position;
    assert(position->size == 3);

    typedef int16_t u_int16_t __attribute__((aligned(1)));

    mesh_out->num_vertices = prim->num_vertices;
    mesh_out->vertices = malloc(mesh_out->num_vertices * sizeof(mesh_out->vertices[0]));

    for (uint32_t vertex_id=0; vertex_id < prim->num_vertices; vertex_id++) {
        u_int16_t* pos = (u_int16_t*)(position->pointer + position->stride * vertex_id);
        float f[3] = {scale * pos[0], scale * pos[1], scale * pos[2]};

        memset(&mesh_out->vertices[vertex_id], 0, sizeof(mesh_out->vertices[0]));
        mesh_out->vertices[vertex_id].position[0] = f[0];
        mesh_out->vertices[vertex_id].position[1] = f[1];
        mesh_out->vertices[vertex_id].position[2] = f[2];
    }

    mesh_out->num_indices = prim->num_indices;
    mesh_out->indices = malloc(mesh_out->num_indices * sizeof(uint16_t));

    uint16_t* prim_indices = (uint16_t*)prim->indices;
    assert(prim->index_type == 0x1403); // GL_UNSIGNED_SHORT

    for (uint32_t i=0;i<prim->num_indices;i++) {
        mesh_out->indices[i] = prim_indices[i];
    }

    if (verbose) {
        debugf("index buffer:\n");
        for (int i = 0; i < mesh_out->num_indices; i++) {
            debugf("%d, ", mesh_out->indices[i]);
        }
        debugf("\n");
    }
    return true;
}

uint32_t uncompress_model64_verts(primitive_t* prim, vertex_t* vertices_out) {
    assert(prim->position.type == GL_HALF_FIXED_N64);

    int bits = prim->vertex_precision;
    float scale = 1.0f / (1 << bits);

    attribute_t* position = &prim->position;
    assert(position->size == 3);

    typedef int16_t u_int16_t __attribute__((aligned(1)));

    uint32_t vertex_id=0;
    for (; vertex_id < prim->num_vertices; vertex_id++) {
        u_int16_t* pos = (u_int16_t*)(position->pointer + position->stride * vertex_id);
        float f[3] = {scale * pos[0], scale * pos[1], scale * pos[2]};

        memset(&vertices_out[vertex_id], 0, sizeof(vertices_out[0]));
        vertices_out[vertex_id].position[0] = f[0];
        vertices_out[vertex_id].position[1] = f[1];
        vertices_out[vertex_id].position[2] = f[2];
    }

    return vertex_id;
}

bool compute_mesh_bounds(mesh_t* mesh_in, const matrix_t* to_world,
    float* out_obj_radius, aabb_t* out_obj_aabb,
    float* out_world_radius, aabb_t* out_world_aabb, float* out_world_center)
{
    bool verbose = false;

    primitive_t* prim = &mesh_in->primitives[0];
    attribute_t* attr = &prim->position;

    if (verbose) {
        debugf("Num primitives: %lu\n", mesh_in->num_primitives);
        debugf("Num vertices: %lu\n", prim->num_vertices);
        debugf("Num indices: %lu\n", prim->num_indices);

        debugf("Primitive 0 pos attribute: size=%lu, type=%lu, stride=%lu, pointer=%p\n",
               attr->size,
               attr->type,
               attr->stride,
               attr->pointer);
    }
    assert(mesh_in->num_primitives == 1 && "we can handle only a single primitive per mesh");

    vertex_t* vertices =  malloc(prim->num_vertices * sizeof(vertex_t));
    DEFER(free(vertices));

    uint32_t count = uncompress_model64_verts(prim, vertices);

    if (count != prim->num_vertices) {
        debugf("model64 uncompression failed\n");
        return 0.0f;
    }

    float max_obj_radius = 0.0f;

    for (int j = 0; j < 3; j++) {
        out_obj_aabb->lo[j] = __FLT_MAX__;
        out_obj_aabb->hi[j] = -__FLT_MAX__;
        out_world_aabb->lo[j] = __FLT_MAX__;
        out_world_aabb->hi[j] = -__FLT_MAX__;
    }

    for (uint32_t i = 0; i < count; i++) {
        float* p = &vertices[i].position[0];
        float v[4] = {p[0], p[1], p[2], 1.0f};
        float world[4];

        matrix_mult(&world[0], to_world, &v[0]);

        float obj_radius = sqrtf(p[0]*p[0] + p[1]*p[1] + p[2]*p[2]);


        for (int j=0;j<3;j++) {
            out_obj_aabb->lo[j] = min(out_obj_aabb->lo[j], p[j]);
            out_obj_aabb->hi[j] = max(out_obj_aabb->hi[j], p[j]);
        }

        max_obj_radius = max(obj_radius, max_obj_radius);
    }

    float obj_center[4];
    aabb_get_center(out_obj_aabb, &obj_center[0]);
    obj_center[3] = 1.0f;

    float center[4];
    matrix_mult(&center[0], to_world, &obj_center[0]);

    float max_world_radius = 0.0f;

    for (uint32_t i = 0; i < count; i++) {
        float* p = &vertices[i].position[0];
        float v[4] = {p[0], p[1], p[2], 1.0f};
        float world[4];

        matrix_mult(&world[0], to_world, &v[0]);
        // world is now a vertex in world coordinates
        // compute vector 'diff' from world centroid to vertex
        float diff[3] = {
            world[0] - center[0],
            world[1] - center[1],
            world[2] - center[2]};

        float world_radius = sqrtf(diff[0]*diff[0] + diff[1]*diff[1] + diff[2]*diff[2]);

        for (int j=0;j<3;j++) {
            out_world_aabb->lo[j] = min(out_world_aabb->lo[j], world[j]);
            out_world_aabb->hi[j] = max(out_world_aabb->hi[j], world[j]);
        }

        max_world_radius = max(world_radius, max_world_radius);
    }

    *out_obj_radius = max_obj_radius;
    *out_world_radius = max_world_radius;
    for (int j=0;j<3;j++) {
        out_world_center[j] = center[j];
    }

    return true;
}
