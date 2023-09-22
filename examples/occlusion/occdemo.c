#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <rspq_profile.h>
#include <malloc.h>
#include <math.h>

#include "camera.h"
#include "cube.h"
#include "decal.h"
#include "sphere.h"
#include "plane.h"
#include "prim_test.h"
#include "skinned.h"

#include "occlusion.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

#define CULL_W (320/4)
#define CULL_H (240/4)

static occ_culler_t* culler;

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;
static surface_t sw_zbuffer_array[2];
static surface_t* sw_zbuffer;
static matrix_t g_projection;

static uint64_t frames = 0;

static GLuint textures[4];

static GLenum shade_model = GL_SMOOTH;
static bool fog_enabled = false;

static const GLfloat environment_color[] = { 0.1f, 0.03f, 0.2f, 1.f };

static const GLfloat light_pos[8][4] = {
    { 1, 0, 0, 0 },
    { -1, 0, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, -1, 0 },
    { 8, 3, 0, 1 },
    { -8, 3, 0, 1 },
    { 0, 3, 8, 1 },
    { 0, 3, -8, 1 },
};

static const GLfloat light_diffuse[8][4] = {
    { 1.0f, 0.0f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f, 1.0f },
    { 0.0f, 0.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 1.0f, 1.0f },
    { 0.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
};

static const char *texture_path[4] = {
    "rom:/circle0.sprite",
    "rom:/diamond0.sprite",
    "rom:/pentagon0.sprite",
    "rom:/triangle0.sprite",
};

static sprite_t *sprites[4];

void compute_camera_matrix(matrix_t* matrix, const camera_t *camera)
{
    matrix_t lookat;
    cpu_gluLookAt(&lookat,
        0, -camera->distance, -camera->distance,
        0, 0, 0,
        0, 1, 0);
    matrix_t rotate = cpu_glRotatef(camera->rotation, 0, 1, 0);
    matrix_mult_full(matrix, &lookat, &rotate);
}

void setup()
{
    camera.distance = -10.0f;
    camera.rotation = 0.0f;

    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
    sw_zbuffer_array[0] = surface_alloc(FMT_RGBA16, CULL_W, CULL_H);
    sw_zbuffer_array[1] = surface_alloc(FMT_RGBA16, CULL_W, CULL_H);
    sw_zbuffer = &sw_zbuffer_array[0];

    for (uint32_t i = 0; i < 4; i++)
    {
        sprites[i] = sprite_load(texture_path[i]);
    }

    setup_sphere();
    make_sphere_mesh();

    setup_cube();

    setup_plane();
    make_plane_mesh();

    float aspect_ratio = (float)display_get_width() / (float)display_get_height();
    float near_plane = 1.0f;
    float far_plane = 50.0f;

    glMatrixMode(GL_PROJECTION);
    computeProjectionMatrix(&g_projection, 80.f, aspect_ratio, near_plane, far_plane);
    glLoadIdentity();
    glMultMatrixf(&g_projection.m[0][0]);

    occ_set_projection_matrix(culler, &g_projection);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, environment_color);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    float light_radius = 10.0f;

    for (uint32_t i = 0; i < 8; i++)
    {
        glEnable(GL_LIGHT0 + i);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, light_diffuse[i]);
        glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 2.0f/light_radius);
        glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 1.0f/(light_radius*light_radius));
    }

    GLfloat mat_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);

    glFogf(GL_FOG_START, 5);
    glFogf(GL_FOG_END, 20);
    glFogfv(GL_FOG_COLOR, environment_color);

    glEnable(GL_MULTISAMPLE_ARB);

    glGenTextures(4, textures);

    #if 0
    GLenum min_filter = GL_LINEAR_MIPMAP_LINEAR;
    #else
    GLenum min_filter = GL_LINEAR;
    #endif


    for (uint32_t i = 0; i < 4; i++)
    {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], &(rdpq_texparms_t){.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE});
    }
}

void set_light_positions(float rotation)
{
    glPushMatrix();
    glRotatef(rotation*5.43f, 0, 1, 0);

    for (uint32_t i = 0; i < 8; i++)
    {
        glLightfv(GL_LIGHT0 + i, GL_POSITION, light_pos[i]);
    }
    glPopMatrix();
}

void render()
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    occ_clear_zbuffer(sw_zbuffer);

    glMatrixMode(GL_MODELVIEW);
    matrix_t modelview;
    compute_camera_matrix(&modelview, &camera);
    matrix_t mvp;
    matrix_mult_full(&mvp, &g_projection, &modelview);

    glLoadMatrixf(&modelview.m[0][0]);
    occ_set_mvp_matrix(culler, &mvp);

    //camera_transform(&camera);

    float rotation = animation * 0.5f;

    set_light_positions(rotation);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);
    
    render_plane();
    //render_decal();
    render_cube();
    //render_skinned(&camera, animation);

    // typedef struct {
    //     GLvoid *data;
    //     uint32_t size;
    // } gl_storage_t;

    // typedef struct {
    //     GLenum usage;
    //     GLenum access;
    //     GLvoid *pointer;
    //     gl_storage_t storage;
    //     bool mapped;
    // } gl_buffer_object_t;

    // gl_buffer_object_t* plane_verts = (gl_buffer_object_t*)plane_buffers[0];
    // gl_buffer_object_t* plane_inds = (gl_buffer_object_t*)plane_buffers[1];
    occ_draw_indexed_mesh(culler, sw_zbuffer, plane_vertices, plane_indices, plane_index_count);
    occ_draw_indexed_mesh(culler, sw_zbuffer, cube_vertices, cube_indices, sizeof(cube_indices)/sizeof(cube_indices[0]));

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);
    //render_sphere(rotation);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // render_primitives(rotation);

    gl_context_end();

    // Show the software zbuffer

    uint16_t minz=0xffff;
    for (int y=0;y<sw_zbuffer->height;y++) {
        for (int x=0;x<sw_zbuffer->width;x++) {
            uint16_t z = ((uint16_t*)(sw_zbuffer->buffer + sw_zbuffer->stride * y))[x];
            if (z<minz) minz = z;
        }
    }
    debugf("minz: %u\n", minz);

    //rdpq_attach(disp, NULL);
    rdpq_set_mode_standard(); // Can't use copy mode if we need a 16-bit -> 32-bit conversion
    rdpq_tex_blit(sw_zbuffer, 0, 0, NULL);
    //rdpq_detach();
    rspq_flush();

    rdpq_detach_show();

    rspq_profile_next_frame();

    if (((frames++) % 60) == 0) {
        rspq_profile_dump();
        rspq_profile_reset();
    }

    if (sw_zbuffer == &sw_zbuffer_array[0]) {
        sw_zbuffer = &sw_zbuffer_array[1];
    } else {
        sw_zbuffer = &sw_zbuffer_array[0];
    }
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);

    rdpq_init();
    gl_init();

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    glDepthRange(0, 1); // This is the default but set here to draw attention it since it's also the culler's convention
    culler = occ_alloc();
    occ_set_viewport(culler, 0, 0, CULL_W, CULL_H);

    setup();

    joypad_init();

    rspq_profile_start();

#if !DEBUG_RDP
    while (1)
#endif
    {
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

        if (pressed.a) {
            animation++;
        }

        if (pressed.b) {
            animation--;
        }

        if (pressed.start) {
            debugf("%ld\n", animation);
        }

        if (pressed.r) {
            shade_model = shade_model == GL_SMOOTH ? GL_FLAT : GL_SMOOTH;
            glShadeModel(shade_model);
        }

        if (pressed.l) {
            fog_enabled = !fog_enabled;
            if (fog_enabled) {
                glEnable(GL_FOG);
            } else {
                glDisable(GL_FOG);
            }
        }

        if (pressed.c_up) {
            if (sphere_rings < SPHERE_MAX_RINGS) {
                sphere_rings++;
            }

            if (sphere_segments < SPHERE_MAX_SEGMENTS) {
                sphere_segments++;
            }

            make_sphere_mesh();
        }

        if (pressed.c_down) {
            if (sphere_rings > SPHERE_MIN_RINGS) {
                sphere_rings--;
            }

            if (sphere_segments > SPHERE_MIN_SEGMENTS) {
                sphere_segments--;
            }
            
            make_sphere_mesh();
        }

        if (pressed.c_right) {
            texture_index = (texture_index + 1) % 4;
        }

        float y = inputs.stick_y / 128.f;
        float x = inputs.stick_x / 128.f;
        float mag = x*x + y*y;

        if (fabsf(mag) > 0.01f) {
            camera.distance += y * 0.2f;
            camera.rotation = camera.rotation - x * 1.2f;
        }

        render();
        if (DEBUG_RDP)
            rspq_wait();
    }

}
