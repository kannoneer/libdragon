// occult
// A Z-Buffer Renderer and Occlusion Tester Library
//

#include "cpumath.h"
#include "transforms.h" // for vertex_t
#include "profiler.h"
#include "defer.h"

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
#define SCREENSPACE_BIAS (-0.50f) // an empirical bias for (x,y) screenspace coordinates to make them cover OpenGL drawn pixels

extern bool config_shrink_silhouettes; // detect edges with flipped viewspace Z signs in each neighbor and add inner conservative flags
extern bool config_discard_based_on_tr_code;
extern bool config_inflate_rough_bounds;
extern bool config_report_near_clip_as_visible; // if queried polygons clip the near plane, always report them as visible

enum {
    RASTER_FLAG_BACKFACE_CULL = 1,
    RASTER_FLAG_EARLY_OUT = 1 << 1,         // Return when the first depth pass is found
    RASTER_FLAG_NEG_SLOPE_BIAS = 1 << 2,    // Negative slope bias, nudge closer, minimum per-pixel depth
    RASTER_FLAG_POS_SLOPE_BIAS = 1 << 3,    // Positive slope bias, nudge farther, maximum per-pixel depth
    RASTER_FLAG_ROUND_DEPTH_UP = 1 << 4,    // Round depths to the next higher 16-bit integer. Default is rounding down.
    RASTER_FLAG_DISCARD_FAR = 1 << 5,       // Discard any triangle that touches the far plane.
    RASTER_FLAG_WRITE_DEPTH = 1 << 6,       // Actually write to depth buffer
    RASTER_FLAG_REPORT_VISIBILITY = 1 << 7, // Write out visible pixel, combined with EARLY_OUT for testing-only mode
    RASTER_FLAG_SHRINK_EDGE_01 = 1 << 8,    // Inner-conservative rasterization
    RASTER_FLAG_SHRINK_EDGE_12 = 1 << 9,
    RASTER_FLAG_SHRINK_EDGE_20 = 1 << 10,
    RASTER_FLAG_EXPAND_EDGE_01 = 1 << 11,   // Outer-conservative rasterization
    RASTER_FLAG_EXPAND_EDGE_12 = 1 << 12,
    RASTER_FLAG_EXPAND_EDGE_20 = 1 << 13,
    RASTER_FLAG_RESERVED14 = 1 << 14,
    RASTER_FLAG_RESERVED15 = 1 << 15,
};

typedef uint32_t occ_raster_flags_t;

enum {
    OCCLUDER_TWO_SIDED = 1,
};
typedef uint32_t occ_occluder_flags_t;

enum {
    CLIP_ACTION_REJECT = 0,
    CLIP_ACTION_DO_IT = 1
};

typedef uint32_t occ_clip_action_t;

occ_clip_action_t config_near_clipping_action = CLIP_ACTION_DO_IT;

#define OCC_RASTER_FLAGS_DRAW  (RASTER_FLAG_BACKFACE_CULL | RASTER_FLAG_WRITE_DEPTH |RASTER_FLAG_ROUND_DEPTH_UP | RASTER_FLAG_DISCARD_FAR)
#define OCC_RASTER_FLAGS_QUERY (RASTER_FLAG_BACKFACE_CULL | RASTER_FLAG_EARLY_OUT | RASTER_FLAG_REPORT_VISIBILITY | RASTER_FLAG_EXPAND_EDGE_01 | RASTER_FLAG_EXPAND_EDGE_12 | RASTER_FLAG_EXPAND_EDGE_20)

typedef struct occ_culler_s {
    struct {
        int x;
        int y;
        int width;
        int height;
    } viewport;
    matrix_t proj;
    matrix_t mvp;
    matrix_t view_matrix;
    uint32_t frame;

    struct {
        float left[4];
        float right[4];
        float top[4];
        float bottom[4];
        float near[4];
        float far[4];
    } clip_planes;
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
    vertex_t *vertices;
    uint16_t *indices;
    uint32_t num_vertices;
    uint32_t num_indices;
} occ_mesh_t;

typedef struct occ_aabb_s {
    float lo[3]; // minimum
    float hi[3]; // maximum
} occ_aabb_t;
#define OCC_NO_EDGE_NEIGHBOR 0xffff

typedef struct occ_hull_s {
    occ_mesh_t mesh; // a hull always owns its mesh data
    vec3f* tri_normals; // num_indices/3 triangle normal vectors
    uint16_t* neighbors; // three neighbors per triangle, one for each edge. num_indices size.
                        // OCC_NO_EDGE_NEIGHBOR sentinel value marks outer edges
    float max_radius;   // largest vertex distance from origin, to be used for culling
} occ_hull_t;

typedef struct occ_target_s {
    uint16_t last_visible_idx;      // last visible index buffer offset
    uint32_t last_visible_frame;    // index of the frame the object was visible
} occ_target_t;

occ_culler_t *occ_alloc();


void occ_set_viewport(occ_culler_t *culler, int x, int y, int width, int height);


void occ_free(occ_culler_t *culler);

void normalize_plane(float* plane);

// The Gribb-Hartmann method, see https://stackoverflow.com/a/34960913
// and https://www8.cs.umu.se/kurser/5DV051/HT12/lab/plane_extraction.pdf
// Plane normals will point inside the frustum.
void extract_planes_from_projmat(
    const float mat[4][4],
    float left[4], float right[4],
    float bottom[4], float top[4],
    float near[4], float far[4]);

void print_clip_plane(float* p) {
    debugf("(%f, %f, %f, %f)\n", p[0], p[1], p[2], p[3]);
}

enum plane_test_result_e {
    RESULT_OUTSIDE = -1,
    RESULT_INTERSECTS = 0,
    RESULT_INSIDE = 1,
};

typedef int plane_test_result_t;
plane_test_result_t test_plane_sphere(float* plane, float* p, float radius_sqr);
bool is_sphere_inside_frustum(occ_culler_t *culler, float* pos, float radius_sqr);


void occ_set_view_and_projection(occ_culler_t *culler, matrix_t *view, matrix_t *proj);


void occ_next_frame(occ_culler_t *culler);

void occ_clear_zbuffer(surface_t *zbuffer);

void draw_tri(
    vec2f v0f,
    vec2f v1f,
    vec2f v2f,
    float Z0f,
    float Z1f,
    float Z2f,
    occ_raster_flags_t flags,
    occ_raster_query_result_t* result,
    surface_t *zbuffer);


#define OCC_MAX_MESH_VERTEX_COUNT (24) // enough for a cube with duplicated verts
#define OCC_MAX_MESH_INDEX_COUNT (30)

void occ_draw_indexed_mesh_flags(occ_culler_t *occ, surface_t *zbuffer, const matrix_t *model_xform, const occ_mesh_t* mesh,
                                vec3f* tri_normals, uint16_t* tri_neighbors,
                                occ_target_t* target, const occ_raster_flags_t flags, occ_raster_query_result_t* query_result);


void occ_draw_mesh(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t *mesh, const matrix_t *model_xform);


void occ_draw_hull(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t* hull, const matrix_t *model_xform, occ_raster_query_result_t* query, occ_occluder_flags_t flags);


typedef struct occ_box2df_s {
    vec2f lo; // inclusive
    vec2f hi; // exclusive
} occ_box2df_t;

// [minX, maxX), [minY, maxY), i.e. upper bounds are exclusive.
bool occ_check_pixel_box_visible(occ_culler_t *occ, surface_t *zbuffer,
                                 uint16_t depth, int minX, int minY, int maxX, int maxY,
                                 occ_box2df_t* in_rotated_box, occ_result_box_t *out_box);


bool occ_check_mesh_visible_rough(occ_culler_t *occ, surface_t *zbuffer, const occ_mesh_t* mesh, const matrix_t *model_xform, occ_result_box_t *out_box);


bool occ_check_hull_visible_precise(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t *hull, const matrix_t *model_xform,
                                    occ_target_t *target, occ_raster_query_result_t *out_result);


bool occ_check_target_visible(occ_culler_t *occ, surface_t *zbuffer, const occ_hull_t* hull, const matrix_t *model_xform,
occ_target_t* target, occ_raster_query_result_t *out_result);


bool occ_hull_from_flat_mesh(const occ_mesh_t* mesh_in, occ_hull_t* hull_out);



#include "../../src/model64_internal.h"

bool model_to_occ_mesh(model64_t* model, mesh_t* mesh_in, occ_mesh_t* mesh_out);

uint32_t uncompress_model64_verts(primitive_t* prim, vertex_t* vertices_out);

void aabb_get_size(occ_aabb_t* box, float* size);

void aabb_get_center(occ_aabb_t* box, float* center);

bool compute_mesh_bounds(mesh_t* mesh_in, const matrix_t* to_world,
    float* out_obj_radius, occ_aabb_t* out_obj_aabb,
    float* out_world_radius, occ_aabb_t* out_world_aabb, float* out_world_center);