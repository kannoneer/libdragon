#ifndef CPUMATH_H_
#define CPUMATH_H_
// statement expressions: https://stackoverflow.com/a/58532788
#define max(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })

#define min(a, b)               \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })

#define SWAP(a, b) ({ typeof(a) t = a; a = b; b = t; })

typedef struct vec2_s {
    int x, y;
} vec2;

typedef struct vec2f_s {
    float x, y;
} vec2f;

typedef struct vec3 {
    int x, y, z;
} vec3;

typedef struct vec3f {
    float x, y, z;
} vec3f;

vec2 vec2_add(vec2 a, vec2 b);
vec2 vec2_sub(vec2 a, vec2 b);
vec2 vec2_mul(vec2 a, vec2 b);
vec2 vec2_muls(vec2 a, float s);
vec2 vec2_div(vec2 a, vec2 b);
vec2 vec2_divs(vec2 a, int s);
vec3 cross3d(vec3 a, vec3 b);
vec3f cross3df(vec3f a, vec3f b);
vec3 vec3_sub(vec3 a, vec3 b);
vec3f vec3f_sub(vec3f a, vec3f b);
#endif