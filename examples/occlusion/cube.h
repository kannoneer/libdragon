#ifndef CUBE_H
#define CUBE_H

#include <libdragon.h>
#include <GL/gl.h>
#include "vertex.h"

static const float cube_size = 3.0f;

static const vertex_t cube_vertices[] = {
    // +X
    { .position = { cube_size, -cube_size, -cube_size}, .texcoord = {0.f, 0.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { cube_size,  cube_size, -cube_size}, .texcoord = {1.f, 0.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { cube_size,  cube_size,  cube_size}, .texcoord = {1.f, 1.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { cube_size, -cube_size,  cube_size}, .texcoord = {0.f, 1.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },

    // -X
    { .position = {-cube_size, -cube_size, -cube_size}, .texcoord = {0.f, 0.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-cube_size, -cube_size,  cube_size}, .texcoord = {0.f, 1.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-cube_size,  cube_size,  cube_size}, .texcoord = {1.f, 1.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-cube_size,  cube_size, -cube_size}, .texcoord = {1.f, 0.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },

    // +Y
    { .position = {-cube_size,  cube_size, -cube_size}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = {-cube_size,  cube_size,  cube_size}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = { cube_size,  cube_size,  cube_size}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = { cube_size,  cube_size, -cube_size}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },

    // -Y
    { .position = {-cube_size, -cube_size, -cube_size}, .texcoord = {0.f, 0.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = { cube_size, -cube_size, -cube_size}, .texcoord = {1.f, 0.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = { cube_size, -cube_size,  cube_size}, .texcoord = {1.f, 1.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = {-cube_size, -cube_size,  cube_size}, .texcoord = {0.f, 1.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },

    // +Z
    { .position = {-cube_size, -cube_size,  cube_size}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = { cube_size, -cube_size,  cube_size}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = { cube_size,  cube_size,  cube_size}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = {-cube_size,  cube_size,  cube_size}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },

    // -Z
    { .position = {-cube_size, -cube_size, -cube_size}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = {-cube_size,  cube_size, -cube_size}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = { cube_size,  cube_size, -cube_size}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = { cube_size, -cube_size, -cube_size}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
};

static const uint16_t cube_indices[] = {
     0,  1,  2,  0,  2,  3,
     4,  5,  6,  4,  6,  7,
     8,  9, 10,  8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
};

static GLuint cube_list;
static GLuint cube_wireframe_list;

static const vertex_t unit_cube_vertices[] = {
    // +X
    { .position = { 1.0f, -1.0f, -1.0f}, .texcoord = {0.f, 0.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { 1.0f,  1.0f, -1.0f}, .texcoord = {1.f, 0.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { 1.0f,  1.0f,  1.0f}, .texcoord = {1.f, 1.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },
    { .position = { 1.0f, -1.0f,  1.0f}, .texcoord = {0.f, 1.f}, .normal = { 1.f,  0.f,  0.f}, .color = 0xFF0000FF },

    // -X
    { .position = {-1.0f, -1.0f, -1.0f}, .texcoord = {0.f, 0.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-1.0f, -1.0f,  1.0f}, .texcoord = {0.f, 1.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-1.0f,  1.0f,  1.0f}, .texcoord = {1.f, 1.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },
    { .position = {-1.0f,  1.0f, -1.0f}, .texcoord = {1.f, 0.f}, .normal = {-1.f,  0.f,  0.f}, .color = 0x00FFFFFF },

    // +Y
    { .position = {-1.0f,  1.0f, -1.0f}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = {-1.0f,  1.0f,  1.0f}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = { 1.0f,  1.0f,  1.0f}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },
    { .position = { 1.0f,  1.0f, -1.0f}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  1.f,  0.f}, .color = 0x00FF00FF },

    // -Y
    { .position = {-1.0f, -1.0f, -1.0f}, .texcoord = {0.f, 0.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = { 1.0f, -1.0f, -1.0f}, .texcoord = {1.f, 0.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = { 1.0f, -1.0f,  1.0f}, .texcoord = {1.f, 1.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },
    { .position = {-1.0f, -1.0f,  1.0f}, .texcoord = {0.f, 1.f}, .normal = { 0.f, -1.f,  0.f}, .color = 0xFF00FFFF },

    // +Z
    { .position = {-1.0f, -1.0f,  1.0f}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = { 1.0f, -1.0f,  1.0f}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = { 1.0f,  1.0f,  1.0f}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },
    { .position = {-1.0f,  1.0f,  1.0f}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  0.f,  1.f}, .color = 0x0000FFFF },

    // -Z
    { .position = {-1.0f, -1.0f, -1.0f}, .texcoord = {0.f, 0.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = {-1.0f,  1.0f, -1.0f}, .texcoord = {0.f, 1.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = { 1.0f,  1.0f, -1.0f}, .texcoord = {1.f, 1.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
    { .position = { 1.0f, -1.0f, -1.0f}, .texcoord = {1.f, 0.f}, .normal = { 0.f,  0.f, -1.f}, .color = 0xFFFF00FF },
};

static const uint16_t unit_cube_indices[] = {
     0,  1,  2,  0,  2,  3,
     4,  5,  6,  4,  6,  7,
     8,  9, 10,  8, 10, 11,
    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19,
    20, 21, 22, 20, 22, 23,
};


void draw_cube()
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(vertex_t), (void*)(0*sizeof(float) + (void*)cube_vertices));
    glTexCoordPointer(2, GL_FLOAT, sizeof(vertex_t), (void*)(3*sizeof(float) + (void*)cube_vertices));
    glNormalPointer(GL_FLOAT, sizeof(vertex_t), (void*)(5*sizeof(float) + (void*)cube_vertices));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex_t), (void*)(8*sizeof(float) + (void*)cube_vertices));

    glDrawElements(GL_TRIANGLES, sizeof(cube_indices) / sizeof(uint16_t), GL_UNSIGNED_SHORT, cube_indices);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

void draw_unit_cube()
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(vertex_t), (void*)(0*sizeof(float) + (void*)unit_cube_vertices));
    glTexCoordPointer(2, GL_FLOAT, sizeof(vertex_t), (void*)(3*sizeof(float) + (void*)unit_cube_vertices));
    glNormalPointer(GL_FLOAT, sizeof(vertex_t), (void*)(5*sizeof(float) + (void*)unit_cube_vertices));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex_t), (void*)(8*sizeof(float) + (void*)unit_cube_vertices));

    glDrawElements(GL_TRIANGLES, sizeof(unit_cube_indices) / sizeof(uint16_t), GL_UNSIGNED_SHORT, unit_cube_indices);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

void setup_cube()
{
    cube_list = glGenLists(1);
    glNewList(cube_list, GL_COMPILE);
    draw_cube();
    glEndList();

    cube_wireframe_list = glGenLists(1);

    glNewList(cube_wireframe_list, GL_COMPILE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        draw_cube();
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEndList();
}

void draw_cube_list(GLuint list)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glCallList(list);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
}

void render_wireframe_cube()
{
    rdpq_debug_log_msg("Cube");
    //glPushMatrix();
    //glTranslatef(0,-1.f,0);

    // Apply vertex color as material color.
    // Because the cube has colors set per vertex, we can color each face seperately
    glEnable(GL_COLOR_MATERIAL);

    // Apply to ambient and diffuse material properties
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    draw_cube_list(cube_wireframe_list);
    
    glDisable(GL_COLOR_MATERIAL);

    //glPopMatrix();
}

void render_cube()
{
    rdpq_debug_log_msg("Cube");
    //glPushMatrix();
    //glTranslatef(0,-1.f,0);

    // Apply vertex color as material color.
    // Because the cube has colors set per vertex, we can color each face seperately
    glEnable(GL_COLOR_MATERIAL);

    // Apply to ambient and diffuse material properties
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    draw_cube_list(cube_list);
    
    glDisable(GL_COLOR_MATERIAL);

    //glPopMatrix();
}



#endif
