#ifndef CPU_3D_H_
#define CPU_3D_H_

#include <math.h>
#include <stdint.h>

#include <GL/gl.h>

#define CLIPPING_PLANE_COUNT  6
#define CLIPPING_CACHE_SIZE   9
#define CLIPPING_PLANE_SIZE   8
#define GUARD_BAND_FACTOR (4)

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

void cpu_glViewport(int x, int y, int w, int h, int screenw, int screenh);
void cpu_glDepthRange(double n, double f);
void cpu_vertex_calc_screenspace(cpu_vtx_t *v);
float cpu_dot_product4(const float *a, const float *b);
float cpu_lerp(float a, float b, float t);
uint8_t cpu_get_clip_codes(float *pos, float *ref);
void matrix_mult(float *d, const matrix_t *m, const float *v);
void matrix_mult_full(matrix_t *d, const matrix_t *l, const matrix_t *r);
void cpu_vertex_pre_tr(cpu_vtx_t *v, matrix_t *mvp);
void cpu_gl_vertex_calc_clip_code(cpu_vtx_t *v);
void cpu_intersect_line_plane(cpu_vtx_t *intersection, const cpu_vtx_t *p0, const cpu_vtx_t *p1, const float *clip_plane);

// v0, v1, v2:      vertices to clip
// plane_mask:      which planes to try to clip against
// clipping_cache:  Intersection points are stored in the clipping cache
// out_list:        the generated polygon
void cpu_gl_clip_triangle(cpu_vtx_t* v0, cpu_vtx_t* v1, cpu_vtx_t* v2, uint8_t plane_mask, cpu_vtx_t clipping_cache[static CLIPPING_CACHE_SIZE], cpu_clipping_list_t* final_list);

// Matrix ops
matrix_t cpu_glRotatef(float angle, float x, float y, float z);
matrix_t cpu_glTranslatef(float x, float y, float z);
matrix_t cpu_glScalef(float x, float y, float z);
matrix_t cpu_glFrustum(double l, double r, double b, double t, double n, double f);
matrix_t cpu_glLoadIdentity(void);

void invert_rigid_xform(matrix_t* dst, matrix_t* src);

// Reimplementations taken from src/GL/lighting.c and src/GL/glu.c

void computeProjectionMatrix(matrix_t *proj, float fovy, float aspect, float zNear, float zFar);
float cpu_gl_mag2(const GLfloat *v);
float cpu_gl_mag(const GLfloat *v);
void cpu_gl_normalize(GLfloat *d, const GLfloat *v);
void cpu_gl_cross(GLfloat *p, const GLfloat *a, const GLfloat *b);
float cpu_dot_product3(const float *a, const float *b);
void cpu_gluLookAt(matrix_t *m, float eyex, float eyey, float eyez,
                          float centerx, float centery, float centerz,
                          float upx, float upy, float upz);
void print_matrix(const matrix_t *matrix);
#endif