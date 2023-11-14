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

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

// Random numbers. I can't recall where I copied this code.
#define XORHASH_RAND_MAX (0xffffffffU)

static uint32_t xorhash_state = 1;

static uint32_t xorhash(void) {
	uint32_t x = xorhash_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return xorhash_state = x;
}

static uint32_t xorhash_func(uint32_t x) {
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return x;
}

static float randomf()
{
    return xorhash()/(float)XORHASH_RAND_MAX;
}

// per-frame test config
typedef struct config_s {
    bool clear;
    bool render;
    bool audio_update;
} config_t;

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;

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

void setup()
{
    camera.distance = -10.0f;
    camera.rotation = 0.0f;

    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());

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
    glLoadIdentity();
    glFrustum(-near_plane*aspect_ratio, near_plane*aspect_ratio, -near_plane, near_plane, near_plane, far_plane);

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


void run_test_frame(surface_t *disp, const config_t* cfg)
{
    gl_context_begin();

    if (cfg->clear) {
        glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    glMatrixMode(GL_MODELVIEW);
    camera_transform(&camera);

    float rotation = animation * 0.5f;

    set_light_positions(rotation);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    if (cfg->render) {
        render_plane();
        render_decal();
        render_cube();
        render_skinned(&camera, animation);

        glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1) % 4]);
        render_sphere(rotation);

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        render_primitives(rotation);
    }

    gl_context_end();

    if (cfg->audio_update) {
		if (audio_can_write()) {
			short *buf = audio_write_begin();
			mixer_poll(buf, audio_get_buffer_length());
			audio_write_end();
		}
    }

}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();

    debugf("Build %s\n", __TIMESTAMP__);
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);

    rdpq_init();
    gl_init();

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

	audio_init(44100, 4);
	mixer_init(32);

    setup();

    joypad_init();

    rspq_profile_start();

	xm64player_t xm;
    xm64player_open(&xm, "rom:/AQUA.xm64");
    xm64player_set_loop(&xm, true);
    xm64player_play(&xm, 0);

#if !DEBUG_RDP
    while (1)
#endif
    {
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_buttons_t held = joypad_get_buttons_held(JOYPAD_PORT_1);
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);

        if (held.a) {
            animation++;
        }

        if (held.b) {
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

        #define LENGTH (50)

        for (int round = 1; round < 100000; round++) {
            uint32_t seed = (uint32_t)round;
            xorhash_state = seed;
            camera.distance = -10.f + -9.f * randomf();
            camera.rotation = 360.f * randomf();

            debugf("[%d] seed: %lu, length: %d\n", round, seed, LENGTH);
            debugf("[%d] camera.distance=%f, camera.rotation=%f (%lx, %lx)\n", round, camera.distance, camera.rotation, *((uint32_t*)&camera.distance), *((uint32_t*)&camera.rotation));

            config_t configs[LENGTH];
            debugf("[%d] config_t configs[] = { ", round);
            for (int i=0;i<LENGTH;i++) {
                configs[i] =(config_t){
                    .clear = (xorhash() % 2),
                    .render = (xorhash() % 2),
                    .audio_update = (xorhash() % 2)};
                debugf("{%d,%d,%d},", configs[i].clear, configs[i].render, configs[i].audio_update);
            }
            debugf(" }\n");


            surface_t *disp = display_get();
            rdpq_attach(disp, &zbuffer);

            for (int i = 0; i < LENGTH; i++) {
                run_test_frame(disp, &configs[i]);
            }

            char msg[100];
			sprintf(msg, "finished round %d", round);
            graphics_draw_text(disp, 8, 8, __TIMESTAMP__);
            graphics_draw_text(disp, 8, 16, msg);
            rdpq_detach_show();

            // wait everything to finish before starting the next round
            rspq_wait();
            wait_ms(20);
        }

        #undef LENGTH

        rspq_profile_next_frame();

        if (((frames++) % 60) == 0) {
            rspq_profile_dump();
            rspq_profile_reset();
        }

        if (DEBUG_RDP)
            rspq_wait();
    }
}
