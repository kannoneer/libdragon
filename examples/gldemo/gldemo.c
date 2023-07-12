#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
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

static const bool music_enabled = false;

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;

static float time_secs = 0.0f;

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

#define SIM_MAX_POINTS (100)
#define MAX_SPRINGS (20)

static struct Simulation {
    float x[SIM_MAX_POINTS*3];
    float oldx[SIM_MAX_POINTS*3];
    int num_points;

    struct Spring {
        int from;
        int to;
        float length;
    } springs[MAX_SPRINGS];

    int num_springs;
} sim;

void sim_init()
{
    memset(&sim, 0, sizeof(sim));

    memcpy(sim.oldx, sim.x, sizeof(sim.x));

    //sim.num_springs = SIM_NUM_POINTS - 1;
    //for (int i=0;i<SIM_NUM_POINTS-1;i++) {
    //    const float length = 0.1f;
    //    sim.springs[i] = (struct Spring){i, i+1, length*length};
    //}
    const float rope_segment = 0.5f;
    const float side = 2.0f;
    const float diag = sqrt(2.f) * side;

    int h=-1;
    for (int i=1;i<4;i++) {
        sim.springs[sim.num_springs++] = (struct Spring){i-1, i, rope_segment};
        h = i;
    }

    sim.springs[sim.num_springs++] = (struct Spring){h, h+1,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h, h+2,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h, h+3,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h+1, h+2,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h+2, h+3,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h+3, h+1,   side};

    sim.springs[sim.num_springs++] = (struct Spring){h+1, h+4,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h+2, h+4,   side};
    sim.springs[sim.num_springs++] = (struct Spring){h+3, h+4,   side};
    // sim.springs[sim.num_springs++] = (struct Spring){h+1, h+4, diag};
    // sim.springs[sim.num_springs++] = (struct Spring){h+3, h+4, diag};

    sim.num_points = h+5;

    for (int i=0;i<sim.num_points-1;i++) {
        int idx = i*3;
        sim.x[idx + 0] = i*0.5f;
        sim.x[idx + 1] = i*0.1f;
        sim.x[idx + 2] = i*0.15f;
    }

}

void sim_update()
{
    const float part_length = 0.1f;
    const int num_iters = 3;
    const float gravity = -0.1f;

    // Verlet integration

    sim.x[0] = 0.0f;
    sim.x[1] = 0.0f;
    sim.x[2] = 0.0f;
    sim.oldx[0] = 0.0f;
    sim.oldx[1] = 0.0f;
    sim.oldx[2] = 0.0f;

    for (int i = 0; i < sim.num_points; i++) {
        int idx = i * 3;
        float* pos = &sim.x[idx];
        float* old = &sim.oldx[idx];

        float acc[3] = {0.0f, gravity, 0.0f};
        float temp[3];

        temp[0] = pos[0];
        temp[1] = pos[1];
        temp[2] = pos[2];

        pos[0] += pos[0] - old[0] + acc[0];
        pos[1] += pos[1] - old[1] + acc[1];
        pos[2] += pos[2] - old[2] + acc[2];

        old[0] = temp[0];
        old[1] = temp[1];
        old[2] = temp[2];
    }

    sim.x[0] = 0.0f;
    sim.x[1] = 0.0f;
    sim.x[2] = 0.0f;

    // Satisfy constraints

    for (int iter = 0; iter < num_iters; iter++) {
        for (int i=0;i<sim.num_springs;i++) {
            struct Spring spring = sim.springs[i];
            float* pa = &sim.x[spring.from*3];
            float* pb = &sim.x[spring.to*3];

            float delta[3] = {
                pb[0] - pa[0],
                pb[1] - pa[1],
                pb[2] - pa[2]
            };

            float length = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
            float diff = (length - spring.length) / length;

            float scale = 0.5f * diff; 
            pa[0] += scale * delta[0];
            pa[1] += scale * delta[1];
            pa[2] += scale * delta[2];
            pb[0] -= scale * delta[0];
            pb[1] -= scale * delta[1];
            pb[2] -= scale * delta[2];
        }
    }

    sim.x[0] = 0.0f;
    sim.x[1] = 0.0f;
    sim.x[2] = 0.0f;

    for (int i=0;i<sim.num_points;i++) {
        // float* pos = &sim.x[i*3];
        // debugf("[%d] (%f, %f, %f)\n", i,
        //     pos[0], pos[1], pos[2]
        // );
    }
}


void sim_render()
{
    glPushMatrix();
    glTranslatef(0, 8, 0);
    uint8_t colors[] = {
        255, 0, 0, 
        0, 255, 0, 
        0, 0, 255, 
        0, 255, 255, 
    };

    glBegin(GL_LINES);
    for (int i=0;i<sim.num_springs;i++) {
        struct Spring spring = sim.springs[i];
        float* pa = &sim.x[spring.from*3];
        float* pb = &sim.x[spring.to*3];
        glVertex3f(pa[0], pa[1], pa[2]);
        glVertex3f(pb[0], pb[1], pb[2]);
        glColor3f(i % 2, (i % 3)/2.0f, (i%4)/4.0f);
    }
    glEnd();

    /*
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < SIM_NUM_POINTS; i++) {
        float* pos = &sim.x[i*3];
        glVertex3f(pos[0], pos[1], pos[2]);
        //glColor3bv(&colors[(i%4)*3]);
        glColor3f(i % 2, (i % 3)/2.0f, (i%4)/4.0f);
    }
    glEnd();
    */
    glPopMatrix();
}

void render_wires()
{
    glPushMatrix();
    glTranslatef(0, 6, 0);
    glBegin(GL_LINE_STRIP);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 1.0f, 0.0f);
    glVertex3f(0.0f, 2.0f, 1.0f);
    glVertex3f(1.0f, 3.0f, 0.0f);
    glEnd();
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

    set_light_positions(rotation);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);
    
    render_plane();
    // render_decal();

    // glPushMatrix();
    // for (int i=0;i<3;i++) {
    //     glTranslatef(4.0f, 2.0f, 0.f);
    //     glRotatef(20.0f, 0.1f, 0.0f, 0.2f);
    //     glScalef(0.5f, 0.5f, 0.5f);
    //     render_cube(time_secs);
    // }
    // glPopMatrix();
    // render_skinned(&camera, animation);

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);
    render_sphere(rotation);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // render_primitives(rotation);

    sim_update();
    sim_render();
    // render_wires();

    gl_context_end();

    rdpq_detach_show();
}

static int audio_volume_level = 0;
static uint32_t audio_sample_time = 0;

void audio_poll(void) {	
	if (audio_can_write()) {    	
		short *buf = audio_write_begin();
        int bufsize = audio_get_buffer_length();
		mixer_poll(buf, bufsize);
        int maximum = 0;
        for (int i=0;i<bufsize;i++) {
            int val = buf[i*2];
            if (val <0) val=-val;
            if (val > maximum) { maximum = val; }
        }
        audio_volume_level = maximum;

        audio_sample_time += (uint32_t)bufsize;
		audio_write_end();
	}
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE_FETCH_ALWAYS);

    rdpq_init();
    gl_init();

    // glEnable(GL_MULTISAMPLE_ARB);

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    setup();

    controller_init();
    timer_init();

	audio_init(22050, 4);
	mixer_init(1);

    static wav64_t music_wav;
    if (music_enabled) {
        const char* songpath = "/break_it_down_again.wav64"; //TODO need to move to filesystem again
        wav64_open(&music_wav, songpath);
        wav64_play(&music_wav, 0);
    }

    sim_init();

#if !DEBUG_RDP
    while (1)
#endif
    {
        time_secs = TIMER_MICROS(timer_ticks()) / 1e6;
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

        if (down.c[0].L) {
            fog_enabled = !fog_enabled;
            if (fog_enabled) {
                glEnable(GL_FOG);
            } else {
                glDisable(GL_FOG);
            }
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
            camera.distance += y * 0.2f;
            camera.rotation = camera.rotation - x * 1.2f;
        }

        if (music_enabled) {
            audio_poll();
        }

        render();
        if (DEBUG_RDP)
            rspq_wait();
    }

}
