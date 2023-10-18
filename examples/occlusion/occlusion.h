// occult
// Z-Buffer Renderer and Occlusion Tester Library
//

#include "cpumath.h"
#include "transforms.h" // for vertex_t
#include <malloc.h>
#include <memory.h>
#include <n64types.h>
#include <surface.h>

#define SUBPIXEL_BITS (2)

#if SUBPIXEL_BITS == 0
#define SUBPIXEL_ROUND_BIAS (0)
#else
#define SUBPIXEL_ROUND_BIAS (1<<(SUBPIXEL_BITS-1))
#endif

#define SUBPIXEL_SCALE (1 << SUBPIXEL_BITS)
const float inv_subpixel_scale = 1.0f / SUBPIXEL_SCALE;

#define DELTA_BITS (24) // It seems 24 bits give about 1/8 mean per-pixel error of 16 bits deltas.
#define DELTA_SCALE (1<<DELTA_BITS)

#define FLOAT_TO_FIXED32(f) (int32_t)(f * 0x10000)
#define FLOAT_TO_FIXED32_ROUND(f) (int32_t)(f * 0x10000 + 0.5f)
#define FLOAT_TO_U16(f) (uint16_t)(f * 0x10000)
#define U16_TO_FLOAT(u) ((float)u * 0.0002442002f) // Approximately f/0xffff

#define OCC_MAX_Z (0xffff)

#define ZBUFFER_UINT_PTR_AT(zbuffer, x, y) ((u_uint16_t *)(zbuffer->buffer + (zbuffer->stride * y + x * sizeof(uint16_t))))

#define DUMP_WHEN_Z_OVERFLOWS 1

bool g_verbose_setup = false;
bool g_measure_error = false;
bool g_verbose_raster = false; // print depth at vertex pixels
bool g_verbose_early_out = false; // print coordinates of pixels that pass the depth test
bool g_verbose_visibility_tracking = false; // debug prints of last visible tri tracking
bool g_octagon_test = false; // intersect screenspace box with a 45 degree rotated box to get a stricter octagon test
bool config_discard_based_on_tr_code = true;

enum {
    RASTER_FLAG_BACKFACE_CULL = 1,
    RASTER_FLAG_EARLY_OUT = 1 << 1,
    RASTER_FLAG_NEG_SLOPE_BIAS = 1 << 2,   // Negative slope bias, nudge closer, minimum per-pixel depth
    RASTER_FLAG_POS_SLOPE_BIAS = 1 << 3,  // Positive slope bias, nudge farther, maximum per-pixel depth
    RASTER_FLAG_ROUND_DEPTH_UP = 1 << 4, // Round depths to the next higher 16-bit integer. Default is rounding down.
    RASTER_FLAG_DISCARD_FAR = 1 << 5,    // Discard any triangle that touches the far plane.
    RASTER_FLAG_WRITE_DEPTH = 1 << 6,    // Actually write to depth buffer
};

typedef uint32_t occ_raster_flags_t;

enum {
    CLIP_ACTION_REJECT = 0,
    CLIP_ACTION_DO_IT = 1
};

typedef uint32_t occ_clip_action_t;

occ_clip_action_t config_near_clipping_action = CLIP_ACTION_DO_IT;

#define OCC_RASTER_FLAGS_DRAW  (RASTER_FLAG_BACKFACE_CULL | RASTER_FLAG_WRITE_DEPTH |RASTER_FLAG_ROUND_DEPTH_UP | RASTER_FLAG_DISCARD_FAR)
#define OCC_RASTER_FLAGS_QUERY (RASTER_FLAG_BACKFACE_CULL | RASTER_FLAG_EARLY_OUT)

typedef struct occ_culler_s {
    struct {
        int x;
        int y;
        int width;
        int height;
    } viewport;
    matrix_t proj;
    matrix_t mvp;
    uint32_t frame;
} occ_culler_t;

typedef struct occ_result_box_s {
    int minX;
    int minY;
    int maxX;
    int maxY;
    uint16_t udepth; // depth of the visible pixel
    int hitX;        // visible pixel coordinate, otherwise -1
    int hitY;        // visible pixel coordinate, otherwise -1
} occ_result_box_t;

typedef struct occ_raster_query_result_s {
    bool visible;
    int x;
    int y;
    uint16_t depth;
    int num_tris_drawn;
} occ_raster_query_result_t;

typedef struct occ_mesh_s {
    const vertex_t *vertices;
    const uint16_t *indices;
    uint32_t num_vertices;
    uint32_t num_indices;
} occ_mesh_t;

typedef struct occ_occluder_s {
    uint16_t last_visible_idx;      // last visible index buffer offset
    uint32_t last_visible_frame;    // index of the frame the object was visible
} occ_target_t;

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

void occ_set_projection_matrix(occ_culler_t *culler, matrix_t *proj)
{
    culler->proj = *proj;
}

void occ_set_mvp_matrix(occ_culler_t *culler, matrix_t *mvp)
{
    culler->mvp = *mvp;
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

static int orient2d_subpixel(vec2 a, vec2 b, vec2 c)
{
    // We multiply two I.F fixed point numbers resulting in (I-F).2F format,
    // so we shift by F to the right to get the the result in I.F format again.
    return ((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) >> SUBPIXEL_BITS;
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
    // The -0.5 bias is added to empirically match SW depth map pixels to hi-rez RDP picture.
    vec2 v0 = {SUBPIXEL_SCALE * (v0f.x-0.5f) + 0.5f, SUBPIXEL_SCALE * (v0f.y-0.5f) + 0.5f};
    vec2 v1 = {SUBPIXEL_SCALE * (v1f.x-0.5f) + 0.5f, SUBPIXEL_SCALE * (v1f.y-0.5f) + 0.5f};
    vec2 v2 = {SUBPIXEL_SCALE * (v2f.x-0.5f) + 0.5f, SUBPIXEL_SCALE * (v2f.y-0.5f) + 0.5f};

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

    vec2 p_start = { minb.x << SUBPIXEL_BITS, minb.y << SUBPIXEL_BITS };

    int A01 = -(v0.y - v1.y), B01 = -(v1.x - v0.x);
    int A12 = -(v1.y - v2.y), B12 = -(v2.x - v1.x);
    int A20 = -(v2.y - v0.y), B20 = -(v0.x - v2.x);

    int area2x = -orient2d_subpixel(v0, v1, v2);

    if ((flags & RASTER_FLAG_BACKFACE_CULL) && area2x <= 0) return;

    // Triangle area must be at least one pixel^2.
    const int min_triangle_area = SUBPIXEL_SCALE * SUBPIXEL_SCALE * 2;
    if (abs(area2x) < min_triangle_area) {
        return;
    }

    int w0_row = -orient2d_subpixel(v1, v2, p_start);
    int w1_row = -orient2d_subpixel(v2, v0, p_start);
    int w2_row = -orient2d_subpixel(v0, v1, p_start);

    int bias0 = isTopLeftEdge(v1, v2) ? 0 : -1;
    int bias1 = isTopLeftEdge(v2, v0) ? 0 : -1;
    int bias2 = isTopLeftEdge(v0, v1) ? 0 : -1;

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

    // Fixed point deltas for the integer-only inner loop. We use DELTA_BITS of precision.
    int32_t Z_row_fixed = (int32_t)(DELTA_SCALE * Zf_row);
    int32_t dZdx_fixed = (int32_t)(DELTA_SCALE * dZdx);
    int32_t dZdy_fixed = (int32_t)(DELTA_SCALE * dZdy);

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

    if (flags & RASTER_FLAG_EARLY_OUT) {
        assert(result);
        result->visible = false;
    }

    const bool super_verbose = false;

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

                uint16_t depth = (Z_incr_fixed + bias) >> (DELTA_BITS - 16);
                // if (true) { depth = 0x8000;  }

                u_uint16_t *buf = ZBUFFER_UINT_PTR_AT(zbuffer, p.x, p.y);

                if (depth < *buf) {
                    if (flags & RASTER_FLAG_WRITE_DEPTH) {
                        *buf = depth;
                    }
                    if (flags & RASTER_FLAG_EARLY_OUT) {
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
                        return; // early out was requested
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

void occ_draw_indexed_mesh_flags(occ_culler_t *occ, surface_t *zbuffer, const matrix_t *model_xform,
                                 const vertex_t *vertices, const uint16_t *indices, uint32_t num_indices,
                                 occ_target_t* target, occ_raster_flags_t flags, occ_raster_query_result_t* query_result)
{
    // We render a viewport (x,y,x+w,y+h) in zbuffer's pixel space
    cpu_glViewport(
        occ->viewport.x,
        occ->viewport.y,
        occ->viewport.width,
        occ->viewport.height,
        zbuffer->width,
        zbuffer->height);

    matrix_t* mvp = NULL;
    matrix_t mvp_new;

    if (model_xform) {
        mvp = &mvp_new;
        matrix_mult_full(mvp, &occ->mvp, model_xform);
    } else {
        mvp = &occ->mvp;
    }

    int num_tris_drawn = 0;
    int ofs = 0;
    if (target) {
        ofs = target->last_visible_idx; // start from where we last found a visible pixel
        target->last_visible_idx = 0;
    }

    for (int is = 0; is < num_indices; is += 3) {
        int wrapped_is = (is + ofs) % num_indices; // start from 'ofs' but render the whole mesh
        const uint16_t *inds = &indices[wrapped_is];
        cpu_vtx_t verts[3] = {0};
        cpu_vtx_t clipping_cache[CLIPPING_CACHE_SIZE];
        cpu_clipping_list_t clipping_list = {.count = 0};

        for (int i = 0; i < 3; i++) {
            verts[i].obj_attributes.position[0] = vertices[inds[i]].position[0];
            verts[i].obj_attributes.position[1] = vertices[inds[i]].position[1];
            verts[i].obj_attributes.position[2] = vertices[inds[i]].position[2];
            verts[i].obj_attributes.position[3] = 1.0f; // Q: where does cpu pipeline set this?
                                                        // debugf("i=%d, pos[3] = %f\n", i, verts[i].obj_attributes.position[3]);
        }

        for (int i = 0; i < 3; i++) {
            cpu_vertex_pre_tr(&verts[i], mvp);
            cpu_vertex_calc_screenspace(&verts[i]);
        }

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

        const int NEAR_PLANE_INDEX = 2;
        const int FAR_PLANE_INDEX = 5;
        const bool clips_near = (verts[0].tr_code | verts[1].tr_code | verts[2].tr_code) & (1 << NEAR_PLANE_INDEX);
        const bool clips_far = (verts[0].tr_code | verts[1].tr_code | verts[2].tr_code) & (1 << FAR_PLANE_INDEX);

        if (clips_near) {
            if (config_near_clipping_action == CLIP_ACTION_REJECT) {
                continue;
            } else if (config_near_clipping_action == CLIP_ACTION_DO_IT) {
                // tr_code   = clip against screen bounds, used for rejection
                // clip_code = clipped against guard bands, used for actual clipping
                //
                // We clip only against the near plane so they are the same.

                verts[0].clip_code = verts[0].tr_code;
                verts[1].clip_code = verts[1].tr_code;
                verts[2].clip_code = verts[2].tr_code;

                cpu_gl_clip_triangle(&verts[0], &verts[1], &verts[2], (1 << NEAR_PLANE_INDEX), clipping_cache, &clipping_list);
            } else {
                debugf("Invalid clip action %lu\n", config_near_clipping_action);
                assert(false);
            }
        }

        if (clips_far && (flags & RASTER_FLAG_DISCARD_FAR)) {
            // Reject triangles that touch the far clip plane when rendering occluders. We can't store their farthest depth anyway.
            continue;
        }

        if (clipping_list.count == 0) {
            draw_tri(
                (vec2f){verts[0].screen_pos[0], verts[0].screen_pos[1]},
                (vec2f){verts[1].screen_pos[0], verts[1].screen_pos[1]},
                (vec2f){verts[2].screen_pos[0], verts[2].screen_pos[1]},
                verts[0].depth, verts[1].depth, verts[2].depth,
                flags, query_result,
                zbuffer);
            num_tris_drawn++;
        } else {
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
                    flags, query_result,
                    zbuffer);
                num_tris_drawn++;
            }
        }

        if (query_result) { query_result->num_tris_drawn = num_tris_drawn; }

        // Early out from all triangles even if only one of them was visible
        if ((flags & RASTER_FLAG_EARLY_OUT) && query_result && query_result->visible) {
            target->last_visible_idx = wrapped_is;
            if (target && g_verbose_visibility_tracking) {
                debugf("was visible at wrapped_is = %d = (%d+%d) %% %lu\n", wrapped_is, is, ofs, num_indices);
            }
            return;
        }
    }
}

void occ_draw_mesh(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t *mesh, const matrix_t *model_xform)
{
	occ_draw_indexed_mesh_flags(occ, zbuffer, model_xform, mesh->vertices, mesh->indices, mesh->num_indices, NULL, OCC_RASTER_FLAGS_DRAW, NULL);
}

static vec2f rotate_xy_coords_45deg(float x, float y) {
    //  [ b.x ] = [ +1 -1 ] [ x ]
    //  [ b.y ]   [ -1 -1 ] [ y ]
    return (vec2f){x - y, -x - y};
}

typedef struct occ_box2df_s {
    vec2f lo; // inclusive
    vec2f hi; // exclusive
} occ_box2df_t;

static bool occ_box2df_inside(occ_box2df_t* box, vec2f* p) {
    if (p->x < box->lo.x || p->x >= box->hi.x) return false;
    if (p->y < box->lo.y || p->y >= box->hi.y) return false;
    return true;
}

int g_num_checked = 0;
static vec2 g_checked_pixels[320*240];

// [minX, maxX), [minY, maxY), i.e. upper bounds are exclusive.
bool occ_check_pixel_box_visible(occ_culler_t *occ, surface_t *zbuffer,
                                 uint16_t depth, int minX, int minY, int maxX, int maxY,
                                 occ_box2df_t* in_rotated_box, occ_result_box_t *out_box)
{
    if (minX < 0) minX = 0;
    if (minY < 0) minY = 0;
    if (maxX > zbuffer->width - 1) maxX = zbuffer->width - 1;
    if (maxY > zbuffer->height - 1) maxY = zbuffer->height - 1;

    g_num_checked = 0;

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
            g_checked_pixels[g_num_checked++] = (vec2){x,y};
            u_uint16_t *buf = ZBUFFER_UINT_PTR_AT(zbuffer, x, y);
            if (depth <= *buf) {
                if (out_box) {
                    out_box->udepth = *buf;
                    out_box->hitX = x;
                    out_box->hitY = y;
                }
                return true;
            }
        }
    }

    return false; // Every box pixel was behind the Z-buffer
}

bool occ_check_mesh_visible_rough(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t* mesh, const matrix_t *model_xform, occ_result_box_t *out_box)
{
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
        mvp = &mvp_new;
        matrix_mult_full(mvp, &occ->mvp, model_xform);
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
        cpu_vtx_t vert = {};
        vert.obj_attributes.position[0] = mesh->vertices[iv].position[0];
        vert.obj_attributes.position[1] = mesh->vertices[iv].position[1];
        vert.obj_attributes.position[2] = mesh->vertices[iv].position[2];
        vert.obj_attributes.position[3] = 1.0f;

        cpu_vertex_pre_tr(&vert, mvp);
        cpu_vertex_calc_screenspace(&vert);
        if (vert.depth < 0.f) return true; // HACK: any vertex behind camera makes the object visible
        minZ = min(minZ, vert.depth);
        minX = min(minX, vert.screen_pos[0]);
        maxX = max(maxX, vert.screen_pos[0]);
        minY = min(minY, vert.screen_pos[1]);
        maxY = max(maxY, vert.screen_pos[1]);

        if (g_octagon_test) {
            vec2f pr = rotate_xy_coords_45deg(vert.screen_pos[0], vert.screen_pos[1]);
            oct_box.lo.x = min(oct_box.lo.x, pr.x);
            oct_box.lo.y = min(oct_box.lo.y, pr.y);
            oct_box.hi.x = max(oct_box.hi.x, pr.x);
            oct_box.hi.y = max(oct_box.hi.y, pr.y);
        }
    }

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
    return occ_check_pixel_box_visible(occ, zbuffer, udepth, minX, minY, maxX, maxY, rotated_box, out_box);
}

bool occ_check_mesh_visible_precise(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t *mesh, const matrix_t *model_xform,
                                    occ_target_t *target, occ_raster_query_result_t *out_result)
{
    occ_raster_query_result_t result = {};
    occ_draw_indexed_mesh_flags(occ, zbuffer, model_xform, mesh->vertices, mesh->indices, mesh->num_indices, target, OCC_RASTER_FLAGS_QUERY, &result);
    if (out_result) {
        *out_result = result;
    }
    return result.visible;
}

bool occ_check_target_visible(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t* mesh, const matrix_t *model_xform,
occ_target_t* target, occ_raster_query_result_t *out_result)
{
    // debugf("%s mesh=%p, model_xform=%p, target=%p, out_result=%p\n", __FUNCTION__, mesh, model_xform, target ,out_result);
    occ_result_box_t box = {};
    bool pass = true;

    // Do a rough check only if target was not visible last time.
    if (target->last_visible_frame != occ->frame - 1) {
        pass = occ_check_mesh_visible_rough(occ, zbuffer, mesh, model_xform, &box);

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

    pass = occ_check_mesh_visible_precise(occ, zbuffer, mesh, model_xform, target, out_result);

    if (g_verbose_visibility_tracking) {
        debugf("tris drawn: %d, last_visible = %d\n", out_result->num_tris_drawn, target->last_visible_idx);
    }

    if (pass) {
        target->last_visible_frame = occ->frame;
    } else {
        if (g_verbose_visibility_tracking) {
            debugf("precise fail\n");
        }
    }

    return pass;
}
