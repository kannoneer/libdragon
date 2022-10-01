#include <libdragon.h>
#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <malloc.h>
#include <math.h>

#include "cube.h"

static uint32_t animation = 3283;
static uint32_t texture_index = 0;

static GLuint textures[4];

static GLenum shade_model = GL_SMOOTH;

static const char *texture_path[4] = {
    "rom:/circle0.sprite",
    "rom:/diamond0.sprite",
    "rom:/pentagon0.sprite",
    "rom:/triangle0.sprite",
};

static sprite_t *sprites[4];

void load_texture(GLenum target, sprite_t *sprite)
{
    for (uint32_t i = 0; i < 7; i++)
    {
        surface_t surf = sprite_get_lod_pixels(sprite, i);
        if (!surf.buffer) break;

        data_cache_hit_writeback(surf.buffer, surf.stride * surf.height);

        glTexImageN64(GL_TEXTURE_2D, i, &surf);
    }
}

void setup()
{
    for (uint32_t i = 0; i < 4; i++)
    {
        sprites[i] = sprite_load(texture_path[i]);
    }

    setup_cube();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    float aspect_ratio = (float)display_get_width() / (float)display_get_height();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1*aspect_ratio, 1*aspect_ratio, -1, 1, 1, 20);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_LIGHT0);
    GLfloat light_pos[] = { 0, 0, -3, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);

    GLfloat light_diffuse[] = { 0.8f, 0.8f, 0.8f, 1.f };
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 0.0f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 1.0f/10.0f);

    GLfloat mat_diffuse[] = { 0.3f, 0.5f, 0.9f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_diffuse);

    glGenTextures(4, textures);

    #if 0
    GLenum min_filter = GL_LINEAR_MIPMAP_LINEAR;
    #else
    GLenum min_filter = GL_LINEAR;
    #endif


    for (uint32_t i = 0; i < 4; i++)
    {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        load_texture(GL_TEXTURE_2D, sprites[i]);
    }
}

void render_just_cube()
{
    float rotation = animation * 0.5f;

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glCullFace(GL_FRONT);

    glPushMatrix();

    glTranslatef(0, 0.f, -3.0f);
    glRotatef(rotation*0.46f, 0, 1, 0);
    glRotatef(rotation*1.35f, 1, 0, 0);
    glRotatef(rotation*1.81f, 0, 0, 1);

    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glCullFace(GL_BACK);

    draw_cube();

    glPopMatrix();
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE_FETCH_ALWAYS);

    gl_init();

    setup();

    controller_init();

    // a hacky zoom-in for smaller FoV so that the cube fills the whole screen
    glMatrixMode(GL_PROJECTION);
    glScalef(4.0f, 4.0f, 1.0f);

    while (1)
    {
        controller_scan();
        struct controller_data pressed = get_keys_pressed();
        struct controller_data down = get_keys_down();

        if (pressed.c[0].A) {
            animation++;
        }

        if (pressed.c[0].B) {
            animation--;
        }

        if (down.c[0].start) {
            debugf("%ld\n", animation);
        }

        if (down.c[0].R) {
            shade_model = shade_model == GL_SMOOTH ? GL_FLAT : GL_SMOOTH;
            glShadeModel(shade_model);
        }

        if (down.c[0].C_right) {
            texture_index = (texture_index + 1) % 4;
        }

        #define NUM_TESTS (2)

        uint32_t results[NUM_TESTS];
        const char* test_names[] = {"depth test on", "depth test off"};
        const int num_overdraw = 20;

        int test = 0; // toggle this to switch between the two tests

        if (test == 0) {
            glEnable(GL_DEPTH_TEST);
        }
        else if (test == 1) {
            glDisable(GL_DEPTH_TEST);
        }
        else {
            debugf("BUG\n");
            return 1;
        }
        glClearColor(0.3f, 0.1f, 0.6f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        uint32_t t_start = TICKS_READ();
        for (int i = 0; i < num_overdraw; i++) {
            render_just_cube();
        }
        rspq_flush();
        uint32_t t_end = TICKS_READ();
        results[test] = TICKS_DISTANCE(t_start, t_end);
        uint32_t took = results[test];
        float millis = (float)(took * 1000) / TICKS_PER_SECOND;
        debugf("[%s] ticks: %lu = %.5f ms\n", test_names[test], took, millis);

        gl_swap_buffers();
    }
}
