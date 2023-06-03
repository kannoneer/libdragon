#include <libdragon.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <malloc.h>
#include <math.h>

#include "cube.h"
#include "sphere.h"
#include "plane.h"
#include "prim_test.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static float distance = -10.0f;
static float cam_rotate = 0.0f;
static surface_t zbuffer;

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

void load_texture(GLenum target, sprite_t *sprite)
{
    for (uint32_t i = 0; i < 7; i++)
    {
        surface_t surf = sprite_get_lod_pixels(sprite, i);
        if (!surf.buffer) break;

        glTexImageN64(target, i, &surf);
    }
}

struct Model {
    uint32_t magic; // "BINM"
    uint32_t version; // 20230603
    uint32_t vertex_count;
    uint32_t index_count;
    __attribute__((packed)) struct Vertex {
        float x, y, z;
        uint32_t color;
    }* verts;
    uint16_t* indices;
} model;

void load_model(const char* model_path)
{
    FILE* fp = fopen(model_path, "rb");
    if (!fp) {
        debugf("Failed to open file %s\n", model_path);
    }

    fread(&model, sizeof(struct Model), 1, fp);

    if (model.magic != 0x42494e4d) {
        debugf("Model magic didn't match, got: %lu instead\n", model.magic);
        return;
    }
    if (model.version != 20230603) {
        debugf("Model version didn't match, got: %lu instead\n", model.version);
        return;
    }

    debugf("vertex_count: %u\n", offsetof(struct Model, vertex_count));
    debugf("index_count: %u\n", offsetof(struct Model, index_count));
    debugf("verts: %u\n", offsetof(struct Model, verts));
    debugf("indices: %u\n", offsetof(struct Model, indices));
    debugf("size: %u\n", sizeof(struct Model));

    debugf("counts: verts, inds: %lu, %lu\n", model.vertex_count, model.index_count);
    debugf("start:  verts, inds: %lu, %lu\n", (uint32_t)model.verts, (uint32_t)model.indices);

    fseek(fp, (uint32_t)model.verts, SEEK_SET);
    uint32_t verts_byte_size = sizeof(struct Vertex) * model.vertex_count;
    model.verts = malloc(verts_byte_size);
    assert(model.verts);
    size_t numread = fread(model.verts, 1, verts_byte_size, fp);
    assert(numread == verts_byte_size);

    fseek(fp, (uint32_t)model.indices, SEEK_SET);
    uint32_t indices_byte_size = sizeof(model.indices[0]) * model.index_count;
    model.indices = malloc(indices_byte_size);
    assert(model.indices);
    numread = fread(model.indices, 1, indices_byte_size, fp);
    assert(numread == indices_byte_size);

    fclose(fp);
}

void draw_model()
{
    glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
    glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);

    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(struct Vertex), (void*)(0*sizeof(float) + (void*)model.verts));
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct Vertex), (void*)(3*sizeof(float) + (void*)model.verts));

    glDrawElements(GL_TRIANGLES, model.index_count, GL_UNSIGNED_SHORT, model.indices);
}

void setup()
{
    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());

    for (uint32_t i = 0; i < 4; i++)
    {
        sprites[i] = sprite_load(texture_path[i]);
    }

    load_model("rom://baked.binm");

    for (int i=0;i<3;i++) {
        struct Vertex* v = &model.verts[i];
        debugf("[%d] (%f, %f, %f), RGBA=%lu\n", i, v->x, v->y, v->z, v->color);
    }

    for (int i=0;i<6;i+=3) {
        debugf("(%d, %d, %d)\n", model.indices[i], model.indices[i+1], model.indices[i+2]);
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

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        load_texture(GL_TEXTURE_2D, sprites[i]);
    }
}

void draw_quad()
{
    glBegin(GL_TRIANGLE_STRIP);
        glNormal3f(0, 1, 0);
        glTexCoord2f(0, 0);
        glVertex3f(-0.5f, 0, -0.5f);
        glTexCoord2f(0, 1);
        glVertex3f(-0.5f, 0, 0.5f);
        glTexCoord2f(1, 0);
        glVertex3f(0.5f, 0, -0.5f);
        glTexCoord2f(1, 1);
        glVertex3f(0.5f, 0, 0.5f);
    glEnd();
}

void render()
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(
        0, -distance, -distance,
        0, 0, 0,
        0, 1, 0);
    glRotatef(cam_rotate, 0, 1, 0);

    float rotation = animation * 0.5f;

    glPushMatrix();

    glRotatef(rotation*5.43f, 0, 1, 0);

    for (uint32_t i = 0; i < 8; i++)
    {
        glLightfv(GL_LIGHT0 + i, GL_POSITION, light_pos[i]);
    }

    glPopMatrix();

    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    debugf("Model\n");
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_COLOR_MATERIAL);
    glPushMatrix();
    glTranslatef(-2.0f,2.f,-1.0f);
    glScalef(2.0f, 2.0f, 2.0f);
    draw_model();
    glPopMatrix();

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glEnable(GL_COLOR_MATERIAL);
    glPushMatrix();
    glColor3f(1, 1, 1);
    rdpq_debug_log_msg("Plane");
    draw_plane();
    // glTranslatef(0,-1.f,0);
    // rdpq_debug_log_msg("Cube");
    // draw_cube();
    glPopMatrix();

    // glPushMatrix();
    // glTranslatef(0, 0, 6);
    // glRotatef(35, 0, 1, 0);
    // glScalef(3, 3, 3);
    // glColor4f(1.0f, 0.4f, 0.2f, 0.5f);
    // glDepthFunc(GL_EQUAL);
    // glDepthMask(GL_FALSE);
    // rdpq_debug_log_msg("Decal");
    // draw_quad();
    // glDepthMask(GL_TRUE);
    // glDepthFunc(GL_LESS);
    // glPopMatrix();

    glDisable(GL_COLOR_MATERIAL);

    glPushMatrix();

    glRotatef(rotation*0.23f, 1, 0, 0);
    glRotatef(rotation*0.98f, 0, 0, 1);
    glRotatef(rotation*1.71f, 0, 1, 0);

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);

    glCullFace(GL_FRONT);
    rdpq_debug_log_msg("Sphere");
    draw_sphere();
    glCullFace(GL_BACK);

    glPopMatrix();

    glPushMatrix();

    glTranslatef(0, 6, 0);
    glRotatef(-rotation*2.46f, 0, 1, 0);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // rdpq_debug_log_msg("Primitives");
    // glColor4f(1, 1, 1, 0.4f);
    // prim_test();

    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glPopMatrix();
    gl_context_end();

    rdpq_detach_show();
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE_FETCH_ALWAYS);

    rdpq_init();
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
            distance += y * 0.2f;
            cam_rotate = cam_rotate - x * 1.2f;
        }

        render();
    }
}
