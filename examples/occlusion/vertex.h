#ifndef VERTEX_H
#define VERTEX_H

#include <stdint.h>

typedef struct {
    float position[3];
    float texcoord[2];
    float normal[3];
    uint32_t color;
} vertex_t;

typedef struct {
    float position[3];
} cvertex_t;

#endif // VERTEX_H
