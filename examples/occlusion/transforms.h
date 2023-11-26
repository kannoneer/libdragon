#ifndef TRANSFORMS_H_
#define TRANSFORMS_H_
#include <math.h>
#include <stdint.h>

#define CLIPPING_PLANE_COUNT  6
#define CLIPPING_CACHE_SIZE   9
#define CLIPPING_PLANE_SIZE   8
#define GUARD_BAND_FACTOR (4)

static const float clip_planes[CLIPPING_PLANE_COUNT][4] = {
    { 1, 0, 0, GUARD_BAND_FACTOR },
    { 0, 1, 0, GUARD_BAND_FACTOR },
    { 0, 0, 1, 1 },
    { 1, 0, 0, -GUARD_BAND_FACTOR },
    { 0, 1, 0, -GUARD_BAND_FACTOR },
    { 0, 0, 1, -1 },
};

typedef struct {
    float m[4][4];
} matrix_t;

typedef struct {
    float scale[3];
    float offset[3];
} viewport_t;

typedef struct {
    float position[4];
    // float color[4];
    // float texcoord[4];
    // float normal[3];
    // GLubyte mtx_index[VERTEX_UNIT_COUNT];
} obj_attributes_t;

typedef struct {
    float screen_pos[2];
    float depth;
    // float shade[4];
    // float texcoord[2];
    float inv_w;
    float cs_pos[4];
    obj_attributes_t obj_attributes;
    uint8_t clip_code;
    uint8_t tr_code;
    uint8_t t_l_applied;
} cpu_vtx_t;

typedef struct {
    cpu_vtx_t *vertices[CLIPPING_PLANE_COUNT + 3];
    uint32_t count;
} cpu_clipping_list_t;

typedef struct {
    // Pipeline state

    viewport_t current_viewport;

    // GLenum matrix_mode;
    //  GLint current_palette_matrix;

    // gl_matrix_t *current_matrix;

    // gl_matrix_t modelview_stack_storage[MODELVIEW_STACK_SIZE];
    // gl_matrix_t projection_stack_storage[PROJECTION_STACK_SIZE];
    // gl_matrix_t texture_stack_storage[TEXTURE_STACK_SIZE];
    // gl_matrix_t palette_stack_storage[MATRIX_PALETTE_SIZE][PALETTE_STACK_SIZE];

    // gl_matrix_stack_t modelview_stack;
    // gl_matrix_stack_t projection_stack;
    // gl_matrix_stack_t texture_stack;
    // gl_matrix_stack_t palette_stacks[MATRIX_PALETTE_SIZE];
    // gl_matrix_stack_t *current_matrix_stack;

    // gl_matrix_target_t default_matrix_target;
    // gl_matrix_target_t palette_matrix_targets[MATRIX_PALETTE_SIZE];
    // gl_matrix_target_t *current_matrix_target;

    // bool immediate_active;

    // gl_texture_object_t *texture_1d_object;
    // gl_texture_object_t *texture_2d_object;

    // gl_obj_attributes_t current_attributes;

    // uint8_t prim_size;
    // uint8_t prim_indices[3];
    // uint8_t prim_progress;
    // uint32_t prim_counter;
    // uint8_t (*prim_func)(void);
    // uint32_t prim_id;
    // bool lock_next_vertex;
    // uint8_t locked_vertex;

    // uint16_t prim_tex_width;
    // uint16_t prim_tex_height;
    // bool prim_texture;
    // bool prim_bilinear;
    // uint8_t prim_mipmaps;

    // int32_t last_array_element;

    // rdpq_trifmt_t trifmt;

    // gl_vtx_t vertex_cache[VERTEX_CACHE_SIZE];
    // uint32_t vertex_cache_ids[VERTEX_CACHE_SIZE];
    // uint32_t lru_age_table[VERTEX_CACHE_SIZE];
    // uint32_t lru_next_age;

    // gl_vtx_t *primitive_vertices[3];

    // GLfloat flat_color[4];

    // // Client state

    // const surface_t *color_buffer;

    // GLenum current_error;

    // gl_array_object_t default_array_object;
    // gl_array_object_t *array_object;

    // gl_texture_object_t *default_textures;

    // obj_map_t list_objects;
    // GLuint next_list_name;
    // GLuint list_base;
    // GLuint current_list;

    // gl_buffer_object_t *array_buffer;
    // gl_buffer_object_t *element_array_buffer;

    // gl_matrix_srv_t *matrix_stacks[3];

    // GLboolean unpack_swap_bytes;
    // GLboolean unpack_lsb_first;
    // GLint unpack_row_length;
    // GLint unpack_skip_rows;
    // GLint unpack_skip_pixels;
    // GLint unpack_alignment;

    // GLboolean map_color;
    // GLfloat transfer_scale[4];
    // GLfloat transfer_bias[4];

    // gl_pixel_map_t pixel_maps[4];

    // bool transfer_is_noop;

    // gl_deletion_list_t deletion_lists[MAX_DELETION_LISTS];
    // gl_deletion_list_t *current_deletion_list;

    // int frame_id;
    // volatile int frames_complete;

    // bool can_use_rsp;
    // bool can_use_rsp_dirty;

    // const gl_pipeline_t *current_pipeline;
} state_t;

static state_t state;

void cpu_glViewport(int x, int y, int w, int h, int screenw, int screenh)
{
    state.current_viewport.scale[0] = w * 0.5f;
    state.current_viewport.scale[1] = h * -0.5f;
    state.current_viewport.offset[0] = x + w * 0.5f;
    state.current_viewport.offset[1] = screenh - y - h * 0.5f;

#if 0
    debugf("%s(%d, %d, %d, %d, %d, %d)\n",
        __FUNCTION__,
        x, y, w, h, screenw, screenh);

    debugf("%s viewport.scale=(%f, %f), viewport.offset=(%f, %f)\n",
        __FUNCTION__,
        state.current_viewport.scale[0],
        state.current_viewport.scale[1],
        state.current_viewport.offset[0],
        state.current_viewport.offset[1]
    );
#endif
}

void cpu_glDepthRange(double n, double f)
{
    state.current_viewport.scale[2] = (f - n) * 0.5f;
    state.current_viewport.offset[2] = n + (f - n) * 0.5f;
}

static void cpu_vertex_calc_screenspace(cpu_vtx_t *v)
{
    v->inv_w = v->cs_pos[3] != 0.0f ? 1.0f / v->cs_pos[3] : 0x7FFF;

    v->screen_pos[0] = v->cs_pos[0] * v->inv_w * state.current_viewport.scale[0] + state.current_viewport.offset[0];
    v->screen_pos[1] = v->cs_pos[1] * v->inv_w * state.current_viewport.scale[1] + state.current_viewport.offset[1];

    assert(state.current_viewport.scale[2] > 0.0f && "depth range not set");

    v->depth = v->cs_pos[2] * v->inv_w * state.current_viewport.scale[2] + state.current_viewport.offset[2];

    // debugf("%s viewport.scale=(%f, %f), viewport.offset=(%f, %f)\n",
    //     __FUNCTION__,
    //     state.current_viewport.scale[0],
    //     state.current_viewport.scale[1],
    //     state.current_viewport.offset[0],
    //     state.current_viewport.offset[1]
    // );

    // debugf("%s cs_pos=(%f, %f, %f, %f), inv_w=%f, depth=%f\n",
    //     __FUNCTION__,
    //     v->cs_pos[0],
    //     v->cs_pos[1],
    //     v->cs_pos[2],
    //     v->cs_pos[3],
    //     v->inv_w,
    //     v->depth
    // );

    // float da  = v->cs_pos[2] * v->inv_w;
    // float db = state.current_viewport.scale[2];
    // float dc = da * db;
    // debugf("%s v->depth = %f * %f + %f = %f + %f = %f\n", __FUNCTION__, da, db, state.current_viewport.offset[2], dc, state.current_viewport.offset[2], v->depth);
}

static float cpu_dot_product4(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

static float cpu_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static uint8_t cpu_get_clip_codes(float *pos, float *ref)
{
    // This corresponds to vcl + vch on RSP
    uint8_t codes = 0;
    for (uint32_t i = 0; i < 3; i++) {
        if (pos[i] < -ref[i]) {
            codes |= 1 << i;
        }
        else if (pos[i] > ref[i]) {
            codes |= 1 << (i + 3);
        }
    }
    return codes;
}

static void matrix_mult(float *d, const matrix_t *m, const float *v)
{
    d[0] = m->m[0][0] * v[0] + m->m[1][0] * v[1] + m->m[2][0] * v[2] + m->m[3][0] * v[3];
    d[1] = m->m[0][1] * v[0] + m->m[1][1] * v[1] + m->m[2][1] * v[2] + m->m[3][1] * v[3];
    d[2] = m->m[0][2] * v[0] + m->m[1][2] * v[1] + m->m[2][2] * v[2] + m->m[3][2] * v[3];
    d[3] = m->m[0][3] * v[0] + m->m[1][3] * v[1] + m->m[2][3] * v[2] + m->m[3][3] * v[3];
}

void matrix_mult_full(matrix_t *d, const matrix_t *l, const matrix_t *r)
{
    matrix_mult(d->m[0], l, r->m[0]);
    matrix_mult(d->m[1], l, r->m[1]);
    matrix_mult(d->m[2], l, r->m[2]);
    matrix_mult(d->m[3], l, r->m[3]);
}

static void cpu_vertex_pre_tr(cpu_vtx_t *v, matrix_t *mvp)
{
    // gl_vtx_t *v = &state.vertex_cache[cache_index];
    // memcpy(&v->obj_attributes, &state.current_attributes, sizeof(gl_obj_attributes_t));

    // gl_matrix_target_t* mtx_target = gl_get_matrix_target(v->obj_attributes.mtx_index[0]);
    matrix_mult(v->cs_pos, mvp, v->obj_attributes.position);

#if 0
    debugf("VTX ID: %d\n", id);
    debugf("     OBJ: %8.2f %8.2f %8.2f %8.2f\n", v->obj_pos[0], v->obj_pos[1],v->obj_pos[2], v->obj_pos[3]);
    debugf("          [%08lx %08lx %08lx %08lx]\n",
        fx16(OBJ_SCALE*v->obj_pos[0]), fx16(OBJ_SCALE*v->obj_pos[1]), fx16(OBJ_SCALE*v->obj_pos[2]), fx16(OBJ_SCALE*v->obj_pos[3]));
    debugf("   CSPOS: %8.2f %8.2f %8.2f %8.2f\n", v->cs_pos[0], v->cs_pos[1], v->cs_pos[2], v->cs_pos[3]);
    debugf("          [%08lx %08lx %08lx %08lx]\n", fx16(OBJ_SCALE*v->cs_pos[0]), fx16(OBJ_SCALE*v->cs_pos[1]), fx16(OBJ_SCALE*v->cs_pos[2]), fx16(OBJ_SCALE*v->cs_pos[3]));
#endif

    float tr_ref[] = {
        v->cs_pos[3],
        v->cs_pos[3],
        v->cs_pos[3]};

    v->tr_code = cpu_get_clip_codes(v->cs_pos, tr_ref);
    v->t_l_applied = false;
}

static void cpu_gl_vertex_calc_clip_code(cpu_vtx_t *v)
{
    GLfloat clip_ref[] = { 
        v->cs_pos[3] * GUARD_BAND_FACTOR,
        v->cs_pos[3] * GUARD_BAND_FACTOR,
        v->cs_pos[3]
    };

    v->clip_code = cpu_get_clip_codes(v->cs_pos, clip_ref);
}

static void cpu_intersect_line_plane(cpu_vtx_t *intersection, const cpu_vtx_t *p0, const cpu_vtx_t *p1, const float *clip_plane)
{
    float d0 = cpu_dot_product4(p0->cs_pos, clip_plane);
    float d1 = cpu_dot_product4(p1->cs_pos, clip_plane);
    
    float a = d0 / (d0 - d1);

    assertf(a >= 0.f && a <= 1.f, "invalid a: %f", a);

    intersection->cs_pos[0] = cpu_lerp(p0->cs_pos[0], p1->cs_pos[0], a);
    intersection->cs_pos[1] = cpu_lerp(p0->cs_pos[1], p1->cs_pos[1], a);
    intersection->cs_pos[2] = cpu_lerp(p0->cs_pos[2], p1->cs_pos[2], a);
    intersection->cs_pos[3] = cpu_lerp(p0->cs_pos[3], p1->cs_pos[3], a);

    // intersection->shade[0] = lerp(p0->shade[0], p1->shade[0], a);
    // intersection->shade[1] = lerp(p0->shade[1], p1->shade[1], a);
    // intersection->shade[2] = lerp(p0->shade[2], p1->shade[2], a);
    // intersection->shade[3] = lerp(p0->shade[3], p1->shade[3], a);

    // intersection->texcoord[0] = lerp(p0->texcoord[0], p1->texcoord[0], a);
    // intersection->texcoord[1] = lerp(p0->texcoord[1], p1->texcoord[1], a);

    cpu_gl_vertex_calc_clip_code(intersection);
}

// v0, v1, v2:      vertices to clip
// plane_mask:      which planes to try to clip against
// clipping_cache:  Intersection points are stored in the clipping cache
// out_list:        the generated polygon
void cpu_gl_clip_triangle(cpu_vtx_t* v0, cpu_vtx_t* v1, cpu_vtx_t* v2, uint8_t plane_mask, cpu_vtx_t clipping_cache[static CLIPPING_CACHE_SIZE], cpu_clipping_list_t* final_list)
{
    uint8_t any_clip = v0->clip_code | v1->clip_code | v2->clip_code;

    any_clip &= plane_mask;

    if (!any_clip) {
        return;
    }

    // Originally by snacchus. Copied from libdragon's cpu_pipeline.c
    // Polygon clipping using the Sutherland-Hodgman algorithm
    // See https://en.wikipedia.org/wiki/Sutherland%E2%80%93Hodgman_algorithm
    
    uint32_t cache_used = 0;

    cpu_clipping_list_t lists[2];

    cpu_clipping_list_t *in_list = &lists[0];
    cpu_clipping_list_t *out_list = &lists[1];

    out_list->vertices[0] = v0;
    out_list->vertices[1] = v1;
    out_list->vertices[2] = v2;
    out_list->count = 3;

    for (uint32_t c = 0; c < CLIPPING_PLANE_COUNT; c++)
    {
        // If nothing clips this plane, skip it entirely
        if ((any_clip & (1<<c)) == 0) {
            continue;
        }

        const float *clip_plane = clip_planes[c];

        SWAP(in_list, out_list);
        out_list->count = 0;

        for (uint32_t i = 0; i < in_list->count; i++)
        {
            uint32_t prev_index = (i + in_list->count - 1) % in_list->count;

            cpu_vtx_t *cur_point = in_list->vertices[i];
            cpu_vtx_t *prev_point = in_list->vertices[prev_index];

            bool cur_inside = (cur_point->clip_code & (1<<c)) == 0;
            bool prev_inside = (prev_point->clip_code & (1<<c)) == 0;

            if (cur_inside ^ prev_inside) {
                cpu_vtx_t *intersection = NULL;

                for (uint32_t n = 0; n < CLIPPING_CACHE_SIZE; n++)
                {
                    if ((cache_used & (1<<n)) == 0) {
                        intersection = &clipping_cache[n];
                        cache_used |= (1<<n);
                        break;
                    }
                }

                assertf(intersection, "clipping cache full!");
                assertf(intersection != cur_point, "invalid intersection");

                cpu_vtx_t *p0 = cur_point;
                cpu_vtx_t *p1 = prev_point;

                // For consistent calculation of the intersection point
                if (prev_inside) {
                    SWAP(p0, p1);
                }

                cpu_intersect_line_plane(intersection, p0, p1, clip_plane);

                out_list->vertices[out_list->count] = intersection;
                out_list->count++;
            }

            if (cur_inside) {
                out_list->vertices[out_list->count] = cur_point;
                out_list->count++;
            } else {
                // If the point is in the clipping cache, remember it as unused
                uint32_t diff = cur_point - clipping_cache;
                if (diff >= 0 && diff < CLIPPING_CACHE_SIZE) {
                    cache_used &= ~(1<<diff);
                }
            }
        }
    }

    final_list->count = out_list->count;

    for (uint32_t i = 0; i < out_list->count; i++)
    {
        final_list->vertices[i] = out_list->vertices[i];
        cpu_vertex_calc_screenspace(final_list->vertices[i]);
    }
}

// Matrix ops

matrix_t cpu_glRotatef(float angle, float x, float y, float z)
{
    float a = angle * (M_PI / 180.0f);
    float c = cosf(a);
    float s = sinf(a);
    float ic = 1.f - c;

    float mag = sqrtf(x * x + y * y + z * z);
    x /= mag;
    y /= mag;
    z /= mag;

    return (matrix_t){.m = {
                          {x * x * ic + c, y * x * ic + z * s, z * x * ic - y * s, 0.f},
                          {x * y * ic - z * s, y * y * ic + c, z * y * ic + x * s, 0.f},
                          {x * z * ic + y * s, y * z * ic - x * s, z * z * ic + c, 0.f},
                          {0.f, 0.f, 0.f, 1.f},
                      }};
}

matrix_t cpu_glTranslatef(float x, float y, float z)
{
    return (matrix_t){.m = {
                          {1.f, 0.f, 0.f, 0.f},
                          {0.f, 1.f, 0.f, 0.f},
                          {0.f, 0.f, 1.f, 0.f},
                          {x, y, z, 1.f},
                      }};
}

matrix_t cpu_glScalef(float x, float y, float z)
{
    return (matrix_t){.m = {
                          {x, 0.f, 0.f, 0.f},
                          {0.f, y, 0.f, 0.f},
                          {0.f, 0.f, z, 0.f},
                          {0.f, 0.f, 0.f, 1.f},
                      }};
}

matrix_t cpu_glFrustum(double l, double r, double b, double t, double n, double f)
{
    return (matrix_t){.m = {
                          {(2 * n) / (r - l),   0.f,                 0.f,                    0.f},
                          {0.f,                 (2.f * n) / (t - b), 0.f,                    0.f},
                          {(r + l) / (r - l),   (t + b) / (t - b),   -(f + n) / (f - n),     -1.f},
                          {0.f,                 0.f,                 -(2 * f * n) / (f - n), 0.f},
                      }};
}

matrix_t cpu_glLoadIdentity(void)
{
    return (matrix_t){.m = {
                          {1, 0, 0, 0},
                          {0, 1, 0, 0},
                          {0, 0, 1, 0},
                          {0, 0, 0, 1},
                      }};
}

// Reimplementations taken from src/GL/lighting.c and src/GL/glu.c

static void computeProjectionMatrix(matrix_t *proj, float fovy, float aspect, float zNear, float zFar)
{
    float sine, cotangent, deltaZ;
    float radians = fovy / 2 * (float)M_PI / 180;
    deltaZ = zFar - zNear;
    sine = sinf(radians);
    if ((deltaZ == 0) || (sine == 0) || (aspect == 0)) {
        return;
    }
    cotangent = cosf(radians) / sine;

    memset(&proj->m[0][0], 0, sizeof(matrix_t));
    proj->m[0][0] = cotangent / aspect;
    proj->m[1][1] = cotangent;
    proj->m[2][2] = -(zFar + zNear) / deltaZ;
    proj->m[2][3] = -1;
    proj->m[3][2] = -2 * zNear * zFar / deltaZ;
    proj->m[3][3] = 0;
}

static float cpu_gl_mag2(const GLfloat *v)
{
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static float cpu_gl_mag(const GLfloat *v)
{
    return sqrtf(cpu_gl_mag2(v));
}

static void cpu_gl_normalize(GLfloat *d, const GLfloat *v)
{
    float inv_mag = 1.0f / cpu_gl_mag(v);

    d[0] = v[0] * inv_mag;
    d[1] = v[1] * inv_mag;
    d[2] = v[2] * inv_mag;
}

static void cpu_gl_cross(GLfloat *p, const GLfloat *a, const GLfloat *b)
{
    p[0] = (a[1] * b[2] - a[2] * b[1]);
    p[1] = (a[2] * b[0] - a[0] * b[2]);
    p[2] = (a[0] * b[1] - a[1] * b[0]);
};

static float cpu_dot_product3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void cpu_gluLookAt(matrix_t *m, float eyex, float eyey, float eyez,
                          float centerx, float centery, float centerz,
                          float upx, float upy, float upz)
{
    GLfloat eye[3] = {eyex, eyey, eyez};
    GLfloat f[3] = {centerx - eyex, centery - eyey, centerz - eyez};
    GLfloat u[3] = {upx, upy, upz};
    GLfloat s[3];

    cpu_gl_normalize(f, f);

    cpu_gl_cross(s, f, u);
    cpu_gl_normalize(s, s);

    cpu_gl_cross(u, s, f);

    m->m[0][0] = s[0];
    m->m[0][1] = u[0];
    m->m[0][2] = -f[0];
    m->m[0][3] = 0;

    m->m[1][0] = s[1];
    m->m[1][1] = u[1];
    m->m[1][2] = -f[1];
    m->m[1][3] = 0;

    m->m[2][0] = s[2];
    m->m[2][1] = u[2];
    m->m[2][2] = -f[2];
    m->m[2][3] = 0;

    m->m[3][0] = -cpu_dot_product3(s, eye);
    m->m[3][1] = -cpu_dot_product3(u, eye);
    m->m[3][2] = cpu_dot_product3(f, eye);
    m->m[3][3] = 1;
};

void print_matrix(const matrix_t *matrix)
{
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            debugf("%f ", matrix->m[row][col]);
        }
        debugf("\n");
    }
}
#endif