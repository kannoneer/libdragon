#include <libdragon.h>
#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>

#include "cube.h"
#include "sphere.h"
#include "plane.h"
#include "prim_test.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static float distance = 1.0f;
static float cam_rotate = 0.0f;

static GLuint textures[4];

static GLenum shade_model = GL_SMOOTH;

static const GLfloat environment_color[] = { 0.1f, 0.03f, 0.05f, 1.f };
// static const GLfloat environment_color[] = { 1.0f, 1.0f, 1.0f, 0.0f };

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


void load_texture(GLenum target, sprite_t *sprite)
{
    for (uint32_t i = 0; i < 7; i++)
    {
        surface_t surf = sprite_get_lod_pixels(sprite, i);
        if (!surf.buffer) break;

        glTexImageN64(target, i, &surf);
    }
}

void setup()
{
    for (uint32_t i = 0; i < 4; i++)
    {
        sprites[i] = sprite_load(texture_path[i]);
    }

    setup_sphere();
    make_sphere_mesh();

    setup_cube();

    setup_plane();
    make_plane_mesh();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_NORMALIZE);

    float aspect_ratio = (float)display_get_width() / (float)display_get_height();
    float near_plane = 0.1f;
    float far_plane = 100.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-near_plane*aspect_ratio, near_plane*aspect_ratio, -near_plane, near_plane, near_plane, far_plane);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float color[] = {1.0f, 0.0f, 1.0f, 1.0f};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, color);
    // glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    // float light_radius = 10.0f;

    // for (uint32_t i = 0; i < 8; i++)
    // {
    //     glEnable(GL_LIGHT0 + i);
    //     glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, light_diffuse[i]);
    //     glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 2.0f/light_radius);
    //     glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 1.0f/(light_radius*light_radius));
    // }

    // GLfloat mat_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    // glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);

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

static void draw_quad()
{
    glBegin(GL_QUADS);
        glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
        glVertex2f(-0.5f, -0.5f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertex2f(0.5f, -0.5f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertex2f(0.5f, 0.5f);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glVertex2f(-0.5f, 0.5f);

        //glVertex2f(1.f, 1.f);
        //glVertex2f(0.f, 1.f);
        //glVertex2f(0.f, 0.f);
        //glVertex2f(1.f, 0.f);
    glEnd();
}

static void draw_star(float r, float g, float b, float alpha)
{
    // glBegin(GL_QUADS);
    glBegin(GL_TRIANGLE_FAN);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
        glVertex2f(0.0f, 0.0f);

        glColor4f(r,g,b, 0.0f);
        glVertex2f(-0.5f, -0.5f);
        glColor4f(r,g,b, 0.0f);
        glVertex2f(0.5f, -0.5f);
        glColor4f(r,g,b, 0.0f);
        glVertex2f(0.5f, 0.5f);
        glColor4f(r,g,b, 0.0f);
        glVertex2f(-0.5f, 0.5f);

        glColor4f(r,g,b, 0.0f);
        glVertex2f(-0.5f, -0.5f);
    glEnd();
}
    

// an awful random number generator
static uint32_t rnd_state = 13;
#define RND_MAX (0xffffffff)

static uint32_t get_rnd()
{
    return rnd_state = (rnd_state * 1103515245 + 12345) & RND_MAX;
}

static void set_rnd_seed(uint32_t seed)
{
    rnd_state = seed;
    get_rnd();
}

static float get_rndf()
{
    return get_rnd() / (float)RND_MAX;
}

static float get_rndf_signed()
{
    return 2.0f * (get_rndf()-0.5f);
}

static float time;

#define NUM_STARS (100)
static struct Star {
    float x,y,z;
    float r,g,b;
    float alpha;
    float scale;
} stars[NUM_STARS];
static int order[NUM_STARS];

static int compare_stars(void* ctx, const void* ptr_a, const void* ptr_b)
{
    // struct Star* array = (struct Star*)ctx;
    int idx_a = *(uint32_t*)ptr_a;
    int idx_b = *(uint32_t*)ptr_b;
    float za = stars[idx_a].z;
    float zb = stars[idx_b].z;
    if (za > zb) return 1;
    if (za < zb) return -1;
    return 0;
}


void render()
{
    glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    // glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    // glRotatef(45, 1, 0, 0);
    glTranslatef(0, 0, distance);
    // glRotatef(cam_rotate, 0, 1, 0);

    float rotation = animation * 0.5f;

    // glPushMatrix();

    // glRotatef(rotation*5.43f, 0, 1, 0);

    // for (uint32_t i = 0; i < 8; i++)
    // {
    //     glLightfv(GL_LIGHT0 + i, GL_POSITION, light_pos[i]);
    // }

    // glPopMatrix();

    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    // glEnable(GL_LIGHTING);
    // glEnable(GL_TEXTURE_2D);

    // glPushMatrix();
    // glColor3f(1, 1, 1);
    // // draw_plane();
    // glTranslatef(0,-1.f,0);
    // glEnable(GL_COLOR_MATERIAL);
    // // draw_cube();
    // glDisable(GL_COLOR_MATERIAL);
    // glPopMatrix();

    // glPushMatrix();

    // glRotatef(rotation*0.23f, 1, 0, 0);
    // glRotatef(rotation*0.98f, 0, 0, 1);
    // glRotatef(rotation*1.71f, 0, 1, 0);

    // glCullFace(GL_FRONT);
    // // draw_sphere();
    // glCullFace(GL_BACK);

    // glPopMatrix();

    glPushMatrix();

    glTranslatef(0, 0, 0);
    // glRotatef(-rotation*2.46f, 0, 1, 0);

    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    // glEnable(GL_TEXTURE_2D);
    // glBindTexture(GL_TEXTURE_2D, textures[1]);
    glDisable(GL_DEPTH_TEST);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  
    // glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);  
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE);  

    glRotatef(time*10.f + 90.0f * sin(time*0.2f), 0.0f, 0.0f, -1.0f);

    // prim_test();
    // glPushMatrix();
    set_rnd_seed(130);
    // const float far = 5.0f;

    int noise1=1;
    int noise2=1;
    for (int i=0;i<NUM_STARS;i++) {
        order[i] = i;
        float travel = (i*3.123 + time * 0.1f);
        int rounds = (int)travel;
        // if (i == 5) debugf("[%d] %f -->", i, travel);
        travel = fmodf(travel, 1.0f);
        // if (i==5) debugf(" %f\n", travel);
        float z = -(60 + 10.0*sin(time*0.2))*(1.0f-travel);
        noise1 += i*16807;
        noise2 += i*48271;
        float xofs = ((noise1) & 0xff) / 255.f - 0.5f;
        float yofs = ((noise2) & 0xff) / 255.f - 0.5f;
        float x = (xofs + get_rndf_signed()) * 50.0f * 0.5f;
        float y = (yofs + get_rndf_signed()) * 50.0f * 0.5f;
        (void)get_rndf_signed();
        // debugf("[%d] %i, (%f, %f)\n", i, rounds, x, y);
        stars[i].x = x;
        stars[i].y = y;
        stars[i].z = z;
        stars[i].r = (i+1 % 32) / 32.0f;
        stars[i].g = (i+3 % 64) / 256.0f;
        stars[i].b = (i+2 % 64) / 64.0f;
        float alpha = travel*2; // will be clipped by opengl
        // if (time < 5.0) alpha *= time*0.2f;
        const float limit = 0.9;
        if (travel > limit) alpha -= (travel - limit) * 40;
        stars[i].alpha = alpha;
        // stars[i].scale = 1.0f + 0.5f*sin(i*2);
    }

    qsort_r((void *)order,
            (size_t)NUM_STARS,
            sizeof(order[0]),
            (void *)stars,
            compare_stars);

    glPushMatrix();
        for (int i=0;i<NUM_STARS;i++) {
            int idx = order[i];
            struct Star* star = &stars[idx];

            //glRotatef(10, 0, 0, -1);
            glPushMatrix();
            glTranslatef(star->x, star->y, star->z);
            glRotatef(idx*10.0f - time*4.0f*sin(idx), 0.0f, 0.0f, -1.0f);
            // draw_quad();
            // glScalef(star->scale, star->scale, star->scale);
            glScalef(2,2,2);
            draw_star(star->r, star->g, star->b, star->alpha);
            glPopMatrix();
        }

    glPopMatrix();

    glPopMatrix();

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glPopMatrix();
}


int main()
{
	debug_init_isviewer();
	debug_init_usblog();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE_FETCH_ALWAYS);

    gl_init();

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    setup();

    controller_init();

#if !DEBUG_RDP
    while (1)
#endif
    {
        controller_scan();
        struct controller_data pressed = get_keys_pressed();
        struct controller_data down = get_keys_down();

        time = get_ticks_ms()/1000.0f;


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

        if (down.c[0].C_up) {
            if (sphere_rings < SPHERE_MAX_RINGS) {
                sphere_rings++;
            }

            if (sphere_segments < SPHERE_MAX_SEGMENTS) {
                sphere_segments++;
            }

            make_sphere_mesh();
        }

        if (down.c[0].C_down) {
            if (sphere_rings > SPHERE_MIN_RINGS) {
                sphere_rings--;
            }

            if (sphere_segments > SPHERE_MIN_SEGMENTS) {
                sphere_segments--;
            }
            
            make_sphere_mesh();
        }

        if (down.c[0].C_right) {
            texture_index = (texture_index + 1) % 4;
        }

        float y = pressed.c[0].y / 128.f;
        float x = pressed.c[0].x / 128.f;
        float mag = x*x + y*y;

        if (fabsf(mag) > 0.01f) {
            distance += y * 0.2f;
            cam_rotate = cam_rotate - x * 1.2f;
        }

        render();

        gl_swap_buffers();
    }
}
