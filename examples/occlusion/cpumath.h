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

typedef struct vec2 {
    int x, y;
} vec2;

typedef struct vec3 {
    int x, y, z;
} vec3;

typedef struct vec3f {
    float x, y, z;
} vec3f;

vec2 vec2_add(vec2 a, vec2 b)
{
    vec2 c = {a.x + b.x, a.y + b.y};
    return c;
}

vec2 vec2_sub(vec2 a, vec2 b)
{
    vec2 c = {a.x - b.x, a.y - b.y};
    return c;
}

vec2 vec2_mul(vec2 a, vec2 b)
{
    vec2 c = {a.x * b.x, a.y * b.y};
    return c;
}

vec2 vec2_muls(vec2 a, float s)
{
    vec2 c = {a.x * s, a.y * s};
    return c;
}

vec2 vec2_div(vec2 a, vec2 b)
{
    vec2 c = {a.x / b.x, a.y / b.y};
    return c;
}

vec2 vec2_divs(vec2 a, int s)
{
    vec2 c = {a.x / s, a.y / s};
    return c;
}

// static int cross2d(vec2 a, vec2 b) {
// 	return a.x * b.y - a.y * b.x;
// }

vec3 cross3d(vec3 a, vec3 b)
{
    vec3 c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return c;
}

vec3f cross3df(vec3f a, vec3f b)
{
    vec3f c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return c;
}

vec3 vec3_sub(vec3 a, vec3 b)
{
    vec3 c = {a.x - b.x, a.y - b.y, a.z - b.z};
    return c;
}

vec3f vec3f_sub(vec3f a, vec3f b)
{
    vec3f c = {a.x - b.x, a.y - b.y, a.z - b.z};
    return c;
}

#endif