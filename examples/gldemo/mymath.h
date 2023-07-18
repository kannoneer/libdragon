#include <math.h>

#include <libdragon.h>

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

void mat4_mul_vec4(const mat4_t A, const float* x, float* y)
{
    for (int i=0;i<4;i++) {
        y[i] += A[0][i] * x[0] + A[1][i] * x[1] + A[2][i] * x[2] + A[3][i] * x[3];
    }
}

void print_vec4(float* v) {
    debugf("[%f,%f,%f,%f]\n", v[0], v[1], v[2], v[3]);
}