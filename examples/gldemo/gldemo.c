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

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;

static uint64_t frames = 0;

static GLuint textures[4];

static GLenum shade_model = GL_SMOOTH;
static bool fog_enabled = false;

static const GLfloat environment_color[] = { 0.05f, 0.05f, 0.05f, 1.f };

#define NUM_LIGHTS (0)

static GLfloat light_pos[8][4] = {
    { 0, 2, 0, 1 },
    { -1, 0, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, -1, 0 },
    { 8, 3, 0, 1 },
    { -8, 3, 0, 1 },
    { 0, 3, 8, 1 },
    { 0, 3, -8, 1 },
};

static const GLfloat light_diffuse[8][4] = {
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f, 1.0f },
    { 0.0f, 0.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 1.0f, 1.0f },
    { 0.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
};

static const char *texture_path[4] = {
    "rom:/bumped.ia8.sprite",
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

    for (uint32_t i = 0; i < NUM_LIGHTS; i++)
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
    glRotatef(rotation, 0, 1, 0);

    for (uint32_t i = 0; i < NUM_LIGHTS; i++)
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

    glMatrixMode(GL_MODELVIEW);
    camera_transform(&camera);

    float rotation = animation * 0.5f;

    static float angle;
    angle = fmodf(angle + 0.05f, M_PI * 2);
    angle = -camera.rotation/360.f*(M_PI*2);
    // set_light_positions(angle);

    const float ds = 3.0f;
    float lpos[] = {ds*cos(angle), 2.0f, ds*sin(angle), 1.0f};
    glLightfv(GL_LIGHT0 , GL_POSITION, lpos);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    if (true) {
        render_cube(); // HACK: Draw something so that old texture size will stay in effect for RDPQ
        glEnable(GL_RDPQ_TEXTURING_N64);

        glBindTexture(GL_TEXTURE_2D, textures[0]);

        // rdpq_tex_reuse
        // rdpq_tex_multi_begin();
        // rdpq_texparms_t parms = {};
        // (void)parms;
        // rdpq_sprite_upload(TILE0, sprites[0], NULL);

        const float sz = 1.5f;

        surface_t surf = sprite_get_lod_pixels(sprites[0], 0);
        rdpq_tex_multi_begin();
        rdpq_tex_upload(TILE0, &surf, &(rdpq_texparms_t){.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE, .s.scale_log = 0, .t.scale_log = 0});
        rdpq_texparms_t parms = {.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE,
            .s.scale_log = 0, .t.scale_log = 0,
            .t.translate = sz * cos(angle), .s.translate = sz * sin(angle)};
        debugf("angle: %f, tx: %f, ty: %f\n", angle, parms.s.translate, parms.t.translate);
        rdpq_tex_reuse(TILE1, &parms);
        rdpq_tex_multi_end();

        // rdpq_set_tile(
        //  rdpq_sprite_upload(TILE1, sprite1, NULL);
        //  rdpq_tex_multi_end();

        glEnable(GL_RDPQ_MATERIAL_N64);
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_mipmap(MIPMAP_NONE, 1);
        uint8_t env = 100;
        uint8_t prim = 255;
        rdpq_set_env_color((color_t){env, env, env, 255});
        rdpq_set_prim_color((color_t){prim, prim, prim, 255});
        rdpq_set_fog_color((color_t){255, 0, 0, 255});
        rdpq_set_blend_color((color_t){255, 255, 255, 255});
        // rdpq_set_tile(1, sprite_get_format(sprites[0]), 0, 256, NULL);
        // rdpq_texparms_t parms = {};
        // rdpq_tex_reuse(1, &parms);

        //rdpq_mode_combiner(RDPQ_COMBINER2((0, 0, 0, TEX0), (0, 0, 0, 1), (COMBINED, TEX1, PRIM, 0), (0, 0, 0, COMBINED)));
        rdpq_mode_combiner(RDPQ_COMBINER2(
            (TEX0, SHADE, ENV, 0), (0, 0, 0, TEX0),
            (0, 0, 0, COMBINED), (COMBINED, TEX1, PRIM, 0)));
        // rdpq_mode_combiner(
        // rdpq_mode_combiner( RDPQ_COMBINER1((0, 0, 0, TEX0), (0, 0, 0, TEX0)));
        //RDPQ_COMBINER2( (0, 0, 0, TEX0), (0, 0, 0, TEX0),
        //   (0, 0, 0, TEX1), (0, 0, 0, TEX1));
        // Can't use 2-pass blender because it forces B=INV_MUX_ALPHA so we couldn't actually add to color
        rdpq_mode_blender(RDPQ_BLENDER((BLEND_RGB, IN_ALPHA, IN_RGB, 1))); 
        // rdpq_mode_combiner(RDPQ_COMBINER2(
        //     (TEX0, 0, SHADE, ENV), (0, 0, 0, 1),
        //     (COMBINED, ENV, PRIM, COMBINED), (0, 0, 0, COMBINED)));
        // rdpq_mode_fog(RDPQ_FOG_STANDARD);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }


    // render_plane();
    // render_decal();
    render_cube();
    render_skinned(&camera, animation);

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);
    // render_sphere(rotation);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // render_primitives(rotation);

    gl_context_end();

    rdpq_detach_wait();

    // char buf[80];
    // sprintf(buf, "Memory: %d MiB", get_memory_size() >> 20);
    // graphics_draw_text(disp, 30, 30, buf);

    display_show(disp);

    rspq_profile_next_frame();

    if (((frames++) % 60) == 0) {
        rspq_profile_dump();
        rspq_profile_reset();
        debugf("frame %lld\n", frames);
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

    setup();

    joypad_init();

    rspq_profile_start();

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

        render();
        if (DEBUG_RDP)
            rspq_wait();
    }

}
