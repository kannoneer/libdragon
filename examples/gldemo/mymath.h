#ifndef MYMATH_H_INCLUDED
#define MYMATH_H_INCLUDED

#include <math.h>

#include <libdragon.h>

#define RAND_MAX (0xffffffffU)

static uint32_t rand_state = 1;
static uint32_t rand(void) {
	uint32_t x = rand_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return rand_state = x;
}

static uint32_t rand_func(uint32_t x) {
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return x;
}


static float randf()
{
    return rand()/(float)RAND_MAX;
}

static void vec3_normalize_(float* v) {
    float invscale = 1.0f / sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    v[0] *= invscale;
    v[1] *= invscale;
    v[2] *= invscale;
}

static void vec3_cross(const float* a, const float* b, float* c)
{
	c[0] = a[1] * b[2] - a[2] * b[1];
	c[1] = a[2] * b[0] - a[0] * b[2];
	c[2] = a[0] * b[1] - a[1] * b[0];
}

void vec3_copy(const float* from, float* to) {
    to[0]=from[0];
    to[1]=from[1];
    to[2]=from[2];
}

void vec3_add(const float* a, const float* b, float* c) {
	c[0] = a[0] + b[0];
	c[1] = a[1] + b[1];
	c[2] = a[2] + b[2];
}

void vec3_sub(const float* a, const float* b, float* c) {
	c[0] = a[0] - b[0];
	c[1] = a[1] - b[1];
	c[2] = a[2] - b[2];
}

void vec3_scale(float s, float* a) {
	a[0] *= s;
	a[1] *= s;
	a[2] *= s;
}

float vec3_length(const float* v) {
    return sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}

typedef float mat4_t[4][4];

void
mat4_ucopy(mat4_t mat, mat4_t dest) {
  dest[0][0] = mat[0][0];  dest[1][0] = mat[1][0];
  dest[0][1] = mat[0][1];  dest[1][1] = mat[1][1];
  dest[0][2] = mat[0][2];  dest[1][2] = mat[1][2];
  dest[0][3] = mat[0][3];  dest[1][3] = mat[1][3];

  dest[2][0] = mat[2][0];  dest[3][0] = mat[3][0];
  dest[2][1] = mat[2][1];  dest[3][1] = mat[3][1];
  dest[2][2] = mat[2][2];  dest[3][2] = mat[3][2];
  dest[2][3] = mat[2][3];  dest[3][3] = mat[3][3];
}

void mat4_set_identity(mat4_t a) {
    static mat4_t identity_matrix = (mat4_t){
        {1,0,0,0},
        {0,1,0,0},
        {0,0,1,0},
        {0,0,0,1},
    };
    mat4_ucopy(identity_matrix, a);
}

void
mat4_mul(mat4_t m1, mat4_t m2, mat4_t dest) {
  float a00 = m1[0][0], a01 = m1[0][1], a02 = m1[0][2], a03 = m1[0][3],
        a10 = m1[1][0], a11 = m1[1][1], a12 = m1[1][2], a13 = m1[1][3],
        a20 = m1[2][0], a21 = m1[2][1], a22 = m1[2][2], a23 = m1[2][3],
        a30 = m1[3][0], a31 = m1[3][1], a32 = m1[3][2], a33 = m1[3][3],

        b00 = m2[0][0], b01 = m2[0][1], b02 = m2[0][2], b03 = m2[0][3],
        b10 = m2[1][0], b11 = m2[1][1], b12 = m2[1][2], b13 = m2[1][3],
        b20 = m2[2][0], b21 = m2[2][1], b22 = m2[2][2], b23 = m2[2][3],
        b30 = m2[3][0], b31 = m2[3][1], b32 = m2[3][2], b33 = m2[3][3];

  dest[0][0] = a00 * b00 + a10 * b01 + a20 * b02 + a30 * b03;
  dest[0][1] = a01 * b00 + a11 * b01 + a21 * b02 + a31 * b03;
  dest[0][2] = a02 * b00 + a12 * b01 + a22 * b02 + a32 * b03;
  dest[0][3] = a03 * b00 + a13 * b01 + a23 * b02 + a33 * b03;
  dest[1][0] = a00 * b10 + a10 * b11 + a20 * b12 + a30 * b13;
  dest[1][1] = a01 * b10 + a11 * b11 + a21 * b12 + a31 * b13;
  dest[1][2] = a02 * b10 + a12 * b11 + a22 * b12 + a32 * b13;
  dest[1][3] = a03 * b10 + a13 * b11 + a23 * b12 + a33 * b13;
  dest[2][0] = a00 * b20 + a10 * b21 + a20 * b22 + a30 * b23;
  dest[2][1] = a01 * b20 + a11 * b21 + a21 * b22 + a31 * b23;
  dest[2][2] = a02 * b20 + a12 * b21 + a22 * b22 + a32 * b23;
  dest[2][3] = a03 * b20 + a13 * b21 + a23 * b22 + a33 * b23;
  dest[3][0] = a00 * b30 + a10 * b31 + a20 * b32 + a30 * b33;
  dest[3][1] = a01 * b30 + a11 * b31 + a21 * b32 + a31 * b33;
  dest[3][2] = a02 * b30 + a12 * b31 + a22 * b32 + a32 * b33;
  dest[3][3] = a03 * b30 + a13 * b31 + a23 * b32 + a33 * b33;
}

void mat4_make_translation(float x, float y, float z, mat4_t dest)
{
    mat4_set_identity(dest);
    dest[3][0] = x; // [col, row]
    dest[3][1] = y;
    dest[3][2] = z;
}

// from cglm
void mat4_inv_topleft3x3(const mat4_t mat, mat4_t dest) {
  float det;
  float a = mat[0][0], b = mat[0][1], c = mat[0][2],
        d = mat[1][0], e = mat[1][1], f = mat[1][2],
        g = mat[2][0], h = mat[2][1], i = mat[2][2];

  dest[0][0] =   e * i - f * h;
  dest[0][1] = -(b * i - h * c);
  dest[0][2] =   b * f - e * c;
  dest[1][0] = -(d * i - g * f);
  dest[1][1] =   a * i - c * g;
  dest[1][2] = -(a * f - d * c);
  dest[2][0] =   d * h - g * e;
  dest[2][1] = -(a * h - g * b);
  dest[2][2] =   a * e - b * d;

  det = 1.0f / (a * dest[0][0] + b * dest[1][0] + c * dest[2][0]);

  for (int i = 0; i < 3; i++)
  {
        for (int j = 0; j < 3; j++)
        {
            dest[j][i] *= det;
        }
  }
}

// Formula from https://math.stackexchange.com/a/1315407
void mat4_invert_rigid_xform(const mat4_t src, mat4_t dest)
{
    float t[3] = {src[3][0], src[3][1], src[3][2]};
    for (int i=0;i<3;i++){
        for (int j=0;j<3;j++) {
            dest[j][i] = src[i][j];
        }
    }
    dest[0][3] = 0.0f;
    dest[1][3] = 0.0f;
    dest[2][3] = 0.0f;
    dest[3][3] = 1.0f;

    // R' t
    float nt[3] = {
        dest[0][0] * t[0] + dest[1][0] * t[1] + dest[2][0] * t[2],
        dest[0][1] * t[0] + dest[1][1] * t[1] + dest[2][1] * t[2],
        dest[0][2] * t[0] + dest[1][2] * t[1] + dest[2][2] * t[2],
    };

    dest[3][0] = -nt[0];
    dest[3][1] = -nt[1];
    dest[3][2] = -nt[2];
}

// Inverts a 4x4 transformation matrix with an arbitrary 3x3 top-left basis matrix.
void mat4_invert_rigid_xform2(const mat4_t src, mat4_t dest)
{
    float t[3] = {src[3][0], src[3][1], src[3][2]};
    // Do a full inverse instead of just transpose.
    mat4_inv_topleft3x3(src, dest);
    dest[0][3] = 0.0f;
    dest[1][3] = 0.0f;
    dest[2][3] = 0.0f;
    dest[3][3] = 1.0f;

    // inv(R) t
    float nt[3] = {
        dest[0][0] * t[0] + dest[1][0] * t[1] + dest[2][0] * t[2],
        dest[0][1] * t[0] + dest[1][1] * t[1] + dest[2][1] * t[2],
        dest[0][2] * t[0] + dest[1][2] * t[1] + dest[2][2] * t[2],
    };

    dest[3][0] = -nt[0];
    dest[3][1] = -nt[1];
    dest[3][2] = -nt[2];
}

// Original from Mesa.
bool mat4_invert(const float* m, float* invOut)
{
    float inv[16], det;
    int i;

    inv[0] = m[5]  * m[10] * m[15] - 
             m[5]  * m[11] * m[14] - 
             m[9]  * m[6]  * m[15] + 
             m[9]  * m[7]  * m[14] +
             m[13] * m[6]  * m[11] - 
             m[13] * m[7]  * m[10];

    inv[4] = -m[4]  * m[10] * m[15] + 
              m[4]  * m[11] * m[14] + 
              m[8]  * m[6]  * m[15] - 
              m[8]  * m[7]  * m[14] - 
              m[12] * m[6]  * m[11] + 
              m[12] * m[7]  * m[10];

    inv[8] = m[4]  * m[9] * m[15] - 
             m[4]  * m[11] * m[13] - 
             m[8]  * m[5] * m[15] + 
             m[8]  * m[7] * m[13] + 
             m[12] * m[5] * m[11] - 
             m[12] * m[7] * m[9];

    inv[12] = -m[4]  * m[9] * m[14] + 
               m[4]  * m[10] * m[13] +
               m[8]  * m[5] * m[14] - 
               m[8]  * m[6] * m[13] - 
               m[12] * m[5] * m[10] + 
               m[12] * m[6] * m[9];

    inv[1] = -m[1]  * m[10] * m[15] + 
              m[1]  * m[11] * m[14] + 
              m[9]  * m[2] * m[15] - 
              m[9]  * m[3] * m[14] - 
              m[13] * m[2] * m[11] + 
              m[13] * m[3] * m[10];

    inv[5] = m[0]  * m[10] * m[15] - 
             m[0]  * m[11] * m[14] - 
             m[8]  * m[2] * m[15] + 
             m[8]  * m[3] * m[14] + 
             m[12] * m[2] * m[11] - 
             m[12] * m[3] * m[10];

    inv[9] = -m[0]  * m[9] * m[15] + 
              m[0]  * m[11] * m[13] + 
              m[8]  * m[1] * m[15] - 
              m[8]  * m[3] * m[13] - 
              m[12] * m[1] * m[11] + 
              m[12] * m[3] * m[9];

    inv[13] = m[0]  * m[9] * m[14] - 
              m[0]  * m[10] * m[13] - 
              m[8]  * m[1] * m[14] + 
              m[8]  * m[2] * m[13] + 
              m[12] * m[1] * m[10] - 
              m[12] * m[2] * m[9];

    inv[2] = m[1]  * m[6] * m[15] - 
             m[1]  * m[7] * m[14] - 
             m[5]  * m[2] * m[15] + 
             m[5]  * m[3] * m[14] + 
             m[13] * m[2] * m[7] - 
             m[13] * m[3] * m[6];

    inv[6] = -m[0]  * m[6] * m[15] + 
              m[0]  * m[7] * m[14] + 
              m[4]  * m[2] * m[15] - 
              m[4]  * m[3] * m[14] - 
              m[12] * m[2] * m[7] + 
              m[12] * m[3] * m[6];

    inv[10] = m[0]  * m[5] * m[15] - 
              m[0]  * m[7] * m[13] - 
              m[4]  * m[1] * m[15] + 
              m[4]  * m[3] * m[13] + 
              m[12] * m[1] * m[7] - 
              m[12] * m[3] * m[5];

    inv[14] = -m[0]  * m[5] * m[14] + 
               m[0]  * m[6] * m[13] + 
               m[4]  * m[1] * m[14] - 
               m[4]  * m[2] * m[13] - 
               m[12] * m[1] * m[6] + 
               m[12] * m[2] * m[5];

    inv[3] = -m[1] * m[6] * m[11] + 
              m[1] * m[7] * m[10] + 
              m[5] * m[2] * m[11] - 
              m[5] * m[3] * m[10] - 
              m[9] * m[2] * m[7] + 
              m[9] * m[3] * m[6];

    inv[7] = m[0] * m[6] * m[11] - 
             m[0] * m[7] * m[10] - 
             m[4] * m[2] * m[11] + 
             m[4] * m[3] * m[10] + 
             m[8] * m[2] * m[7] - 
             m[8] * m[3] * m[6];

    inv[11] = -m[0] * m[5] * m[11] + 
               m[0] * m[7] * m[9] + 
               m[4] * m[1] * m[11] - 
               m[4] * m[3] * m[9] - 
               m[8] * m[1] * m[7] + 
               m[8] * m[3] * m[5];

    inv[15] = m[0] * m[5] * m[10] - 
              m[0] * m[6] * m[9] - 
              m[4] * m[1] * m[10] + 
              m[4] * m[2] * m[9] + 
              m[8] * m[1] * m[6] - 
              m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

    if (det == 0)
        return false;

    det = 1.0 / det;

    for (i = 0; i < 16; i++)
        invOut[i] = inv[i] * det;

    return true;
}

void print_mat4(mat4_t m)
{
    for (int i = 0; i < 4; i++) {
        debugf("[ ");
        for (int j = 0; j < 4; j++) {
            debugf("%f", m[j][i]);
            if (j<3) debugf(",");
            debugf(" ");
        }
        debugf("]");
        if (i<3) debugf(",");
        debugf("\n");
    }
    // for (int i = 0; i < 16; i++) {
    //     debugf("m[%d] = %f\n", i, ((float *)m)[i]);
    // }
}

void mat4_mul_vec4(const mat4_t A, const float* x, float* out)
{
    float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    for (int i=0;i<4;i++) {
        c[i] += A[0][i] * x[0] + A[1][i] * x[1] + A[2][i] * x[2] + A[3][i] * x[3];
    }

    for (int i=0;i<4;i++) {
        out[i] = c[i];
    }
}

void print_vec3(const float* v) {
    debugf("[%f,%f,%f]\n", v[0], v[1], v[2]);
}

void print_vec4(const float* v) {
    debugf("[%f,%f,%f,%f]\n", v[0], v[1], v[2], v[3]);
}

float vec3_dot(const float *a, const float *b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

// Adapted from https://www.scratchapixel.com/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes/ray-plane-and-ray-disk-intersection.html
// 
// n:   plane normal
// p0:  point on plane
// l0:  ray origin
// l:   ray direction
// out_t: ray length output

// Returns true if the plane was hit.
bool intersect_plane(const float *n, const float *p0, const float *l0, const float* l, float *out_t)
{
    const bool verbose = false;
    // assuming vectors are all normalized
    float denom = vec3_dot(n, l);

    if (verbose) {
        debugf("n: ");
        print_vec3(n);
        debugf("l: ");
        print_vec3(l);
        debugf("denom = %f\n", denom);
    }

    if (denom > 1e-6) {
        float p0l0[3]; // = p0 - l0;
        vec3_sub(p0, l0, p0l0);
        *out_t = vec3_dot(p0l0, n) / denom; 
        if (verbose) debugf("%s out_t=%f\n", __FUNCTION__, *out_t);
        return (*out_t >= 0);
    }

    return false;
}
#endif