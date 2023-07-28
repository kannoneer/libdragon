#define LIBDRAGON_PROFILE 0

#include <libdragon.h>
#include "../../src/model64_internal.h"
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/gl_integration.h>
#include <malloc.h>
#include <math.h>

#include "fmath.h"

#include "camera.h"
#include "cube.h"
#include "decal.h"
#include "sphere.h"
#include "plane.h"
#include "prim_test.h"
#include "skinned.h"

#include "mymath.h"
#include "sim.h"

#include "myprofile.h"

#define DEBUG_RDP 0

static const bool music_enabled = true;

static camera_t camera;
static surface_t zbuffer;
static surface_t tempbuffer;
static wav64_t music_wav;

static float time_secs = 0.0f;
static float time_secs_offset = 0.0f;
static int time_frames = 0;

static bool debug_freeze = false;

static bool tweak_double_sided_gems = true;
static bool tweak_dont_test_plane_depth = true; // 3% faster plane drawing when enabled
static bool tweak_enable_sphere_map = true;
static bool tweak_enable_sim_rendering = true;

static bool tweak_sync_before_shadows = true;
static bool tweak_sync_before_video = true;
static bool tweak_sync_after_video = true;
static bool tweak_sync_before_mixer_poll = false;
static bool tweak_sync_after_mixer_poll = false;
static bool tweak_wait_after_render_plane = true;
static bool tweak_wait_after_sim_render = true; // must be on
static bool tweak_sync_after_shadows = true; // must be on, set to false to trigger a crash

#define TEX_CEILING (4)
#define TEX_FLARE (5)
#define TEX_ICON (6)
#define TEX_DIAMOND (7)
#define TEX_TABLE (8)
#define TEX_CLOUDS (9)
#define TEX_CLOUDS_FAST (10)
#define TEX_CLOUDS_DARK (11)
#define NUM_TEXTURES (12)

static GLuint textures[NUM_TEXTURES];

static model64_t* model_gemstone = NULL;

//static const GLfloat environment_color[] = { 0.03f, 0.03f, 0.05f, 1.f };
static const GLfloat environment_color[] = { 0.03f, 0.03f, 0.03f, 1.f };

rdpq_font_t *font_subtitle = NULL;
rdpq_font_t *font_sign = NULL;
rdpq_font_t *font_mono = NULL;

static GLfloat light_pos[8][4] = {
    { 0.1f, 0, 0, 1 },
    { -1, 0, 0, 0 },
    { 0, 0, 1, 0 },
    { 0, 0, -1, 0 },
    { 8, 3, 0, 1 },
    { -8, 3, 0, 1 },
    { 0, 3, 8, 1 },
    { 0, 3, -8, 1 },
};

#define NUM_CAMERAS (3)

static struct Viewer {
    int active_camera;
    struct Camera {
        float eye[3];
        float target[3];
    } cams[NUM_CAMERAS];
} viewer;

#define CAM_CLOSEUP 0
#define CAM_OVERVIEW 1
#define CAM_ACTION 2

#define PART_GEMS 0
#define PART_VIDEO 1
#define PART_END 2
#define PART_INTRO 3
#define PART_SIGN1 4
#define PART_BLACK 5
#define PART_FLIGHT 6
#define NUM_PARTS (7)

static struct {
    bool interactive;
    bool moved; // did use arrows?
    int current_part;
    int overlay;
    float impacts;
    bool part_changed;
    float plane_brite;
    float glitch;
    float shake;
    float flight_speed;
    float flight_brite;
} demo;

void viewer_init()
{
    memset(&viewer, 0, sizeof(viewer));
    viewer.active_camera = 0;
}


#define NUM_SIMULATIONS (3)
static struct Simulation sims[NUM_SIMULATIONS];

mat4_t sim_object_to_worlds[NUM_SIMULATIONS];

static const GLfloat light_diffuse[8][4] = {
    { 1.0f, 1.0f, 1.0f, 1.0f }, //{ 1.0f, 0.1f, 0.0f, 1.0f },
    { 0.0f, 1.0f, 0.0f, 1.0f },
    { 0.0f, 0.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, 1.0f, 1.0f },
    { 0.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f, 1.0f },
};

static const char *texture_path[NUM_TEXTURES] = {
    "rom:/circle0.rgba16.sprite",
    "rom:/diamond0.rgba16.sprite",
    "rom:/pentagon0.rgba16.sprite",
    "rom:/triangle0.rgba16.sprite",
    "rom:/ceiling2.ci4.sprite",
    "rom:/star.ia8.sprite",
    "rom:/icon.rgba16.sprite",
    "rom:/squaremond.i4.sprite",
    "rom:/concrete.ci4.sprite",
    "rom:/clouds1.ci4.sprite",
    "rom:/clouds2.ci4.sprite",
    "rom:/clouds3.ci4.sprite",
};

static sprite_t *sprites[NUM_TEXTURES];
static sprite_t* spr_sign1 = NULL;
static sprite_t* spr_sign2 = NULL;
static sprite_t* spr_splash = NULL;

static void set_diffuse_material()
{
    GLfloat mat_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);
}

static void set_plane_material(float brite)
{
    GLfloat color[] = { brite, brite, brite, 1.0f };
    glColor4fv(color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
}

static void set_gemstone_material(float brite)
{
    GLfloat color[] = { brite, brite, brite, 1.0f };
    //GLfloat color[] = { 1.0f, 1.0f, 1.0f, 0.1 };
    //GLfloat color[] = { 0.2f, 0.2f, 0.3f, 0.1f };
    glColor4fv(color);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
}

static void set_textured_shadow_material()
{
    const float shade = 0.1f;
    GLfloat color[] = { shade, shade, shade, 0.6f };
    //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
    glColor4fv(color);
}

#define MESH_MAX_VERTICES (36)
#define MESH_MAX_INDICES (60)

// The end result after extracting a model64 mesh
struct basic_mesh {
    float verts[MESH_MAX_VERTICES][3];
    float normals[MESH_MAX_VERTICES][3];
    float texcoords[MESH_MAX_VERTICES][2];
    uint16_t indices[MESH_MAX_INDICES];
    int num_vertices;
    int num_indices;
};


// A mesh with special projected shadow vertices
struct shadow_mesh {
    float verts[MESH_MAX_VERTICES][3];
    float verts_proj[MESH_MAX_VERTICES][3];
    float verts_proj_old[MESH_MAX_VERTICES][3];
    float texcoords[MESH_MAX_VERTICES][2];
    uint16_t indices[MESH_MAX_INDICES];
    int num_vertices;
    int num_indices;
};

static struct shadow_mesh smesh_gemstone;
static struct basic_mesh bmesh_gemstone;

static float debug_z_shift = 0.0f;
static float debug_x_shift = 0.0f;

void apply_sphere_mapping()
{
    // apply sphere mapping texgen in world space
    //TODO extract regular positions + normals mesh from model64

    // case GL_SPHERE_MAP:
    //     GLfloat norm_eye_pos[3];
    //     gl_normalize(norm_eye_pos, eye_pos);
    //     GLfloat d2 = 2.0f * dot_product3(norm_eye_pos, eye_normal);
    //     GLfloat r[3] = {
    //         norm_eye_pos[0] - eye_normal[0] * d2,
    //         norm_eye_pos[1] - eye_normal[1] * d2,
    //         norm_eye_pos[2] - eye_normal[2] * d2 + 1.0f,
    //     };
    //     GLfloat m = 1.0f / (2.0f * sqrtf(dot_product3(r, r)));
    //     dest[coord_index] = r[coord_index] * m + 0.5f;
    //     break;
}

void sim_prepare_render(struct Simulation* s, mat4_t out_basis)
{
    MY_PROFILE_START(PS_RENDER_SIM_SETUP, 0);

    mat4_t basis;
    mat4_set_identity(basis);

    vec3_copy(s->pose.u, &basis[2][0]);
    vec3_copy(s->pose.n, &basis[1][0]);
    vec3_cross(s->pose.u, s->pose.n, &basis[0][0]);

    // translate
    vec3_copy(s->pose.origin, &basis[3][0]);

    mat4_t shift;
    mat4_make_translation(debug_x_shift, -0.28f, 0.0f, shift);
    mat4_mul(basis, shift, basis);

    mat4_ucopy(basis, out_basis);
    if (false) {
        debugf("%p basis:\n", s);
        print_mat4(basis);
    }
    MY_PROFILE_STOP(PS_RENDER_SIM_SETUP, 0);
}

void sim_render(struct Simulation* s, mat4_t basis)
{
    MY_PROFILE_START(PS_RENDER_SIM_DRAWCALLS, 0);

    glDisable(GL_TEXTURE_2D);
    if (s->debug.show_wires) {
        glDisable(GL_LIGHTING);
    } else {
        glEnable(GL_LIGHTING);
    }

    glBegin(GL_LINES);
    glColor3f(0.05f, 0.05f, 0.05f);
    for (int i=0;i<s->num_springs;i++) {
        struct Spring spring = s->springs[i];
        if (s->spring_visible[i] || s->debug.show_wires) {
            float* pa = &s->x[spring.from*3];
            float* pb = &s->x[spring.to*3];
            glVertex3f(pa[0], pa[1], pa[2]);
            glVertex3f(pb[0], pb[1], pb[2]);
            //glColor3f(i % 2, (i % 3)/2.0f, (i%4)/4.0f);
        }
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);

    //glEnable(GL_COLOR_MATERIAL);
    //glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glBindTexture(GL_TEXTURE_2D, textures[TEX_CEILING]);

    if (tweak_enable_sphere_map) {
        glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
        glEnable(GL_TEXTURE_GEN_S);
        glEnable(GL_TEXTURE_GEN_T);
    }

    if (true) {

        glPushMatrix();
            glMultMatrixf(&basis[0][0]);
            glDisable(GL_DEPTH_TEST);

            if (tweak_double_sided_gems) {
                set_gemstone_material(0.1f);
                glDepthMask(GL_FALSE);
                glCullFace(GL_BACK);
                glDepthMask(GL_TRUE);
                model64_draw(model_gemstone);

            }
            glCullFace(GL_FRONT); // wtf dude
            set_gemstone_material(0.5f);
            model64_draw(model_gemstone);
            rspq_wait();
            glEnable(GL_DEPTH_TEST);
            glCullFace(GL_BACK);

            glDisable(GL_TEXTURE_2D);
            if (tweak_enable_sphere_map) {
                glDisable(GL_TEXTURE_GEN_S);
                glDisable(GL_TEXTURE_GEN_T);
            }
            glDisable(GL_BLEND);
        glPopMatrix();
    }
    MY_PROFILE_STOP(PS_RENDER_SIM_DRAWCALLS, 0);
}

static void shadow_mesh_clear(struct shadow_mesh* mesh)
{
    memset(mesh, 0, sizeof(struct shadow_mesh));
}

static bool shadow_mesh_extract(struct shadow_mesh* mesh, model64_t* model)
{
    bool verbose = false;
    shadow_mesh_clear(mesh);

    primitive_t* prim = &model_gemstone->meshes[0].primitives[0];
    attribute_t* attr = &prim->position;

    if (verbose) {
        debugf("Num primitives: %lu\n", model_gemstone->meshes[0].num_primitives);
        debugf("Num vertices: %lu\n", prim->num_vertices);
        debugf("Num indices: %lu\n", prim->num_indices);

        debugf("Primitive 0 pos attribute:\nsize=%lu, type=%lu, stride=%lu, pointer=%p\n",
               attr->size,
               attr->type,
               attr->stride,
               attr->pointer);
    }

    int old_to_new[MESH_MAX_VERTICES] = {-1};

    assert(prim->num_vertices <= MESH_MAX_VERTICES); // not strictly needed tue to deduplication
    assert(prim->num_indices <= MESH_MAX_INDICES);
    assert(prim->position.type == GL_HALF_FIXED_N64);

    int bits = prim->vertex_precision;
    float scale = 1.0f / (1 << bits);
    if (verbose) debugf("position bits: %d, scale: %f\n", bits, scale);

    attribute_t* position = &prim->position;
    assert(position->size == 3);

    typedef int16_t u_int16_t __attribute__((aligned(1)));

    for (uint32_t vertex_id=0; vertex_id < prim->num_vertices; vertex_id++) {
        u_int16_t* pos = (u_int16_t*)(position->pointer + position->stride * vertex_id);
        float f[3] = {scale * pos[0], scale * pos[1], scale * pos[2]};
        // debugf("[%lu] (%d, %d, %d) -> (%f, %f, %f)\n", vertex_id, pos[0], pos[1], pos[2], f[0], f[1], f[2]);

        int new_idx = -1;
        for (int slot=0; slot < mesh->num_vertices; slot++) {
            if (mesh->verts[slot][0] == f[0] && mesh->verts[slot][1] == f[1] && mesh->verts[slot][2] == f[2]) {
                // debugf(" same as slot %d = (%f, %f, %f)\n", slot, f[0], f[1], f[2]);
                new_idx = slot;
                break;
            }
        }

        if (new_idx == -1) {
            new_idx = mesh->num_vertices++;
            // debugf(" setting slot %d\n", new_idx);
            mesh->verts[new_idx][0] = f[0];
            mesh->verts[new_idx][1] = f[1];
            mesh->verts[new_idx][2] = f[2];
        }

        old_to_new[vertex_id] = new_idx;
    }

    if (verbose) {
        for (int i = 0; i < prim->num_vertices; i++) {
            debugf("old_to_new[%d] = %d\n", i, old_to_new[i]);
        }
    }

    uint16_t* prim_indices = (uint16_t*)prim->indices;
    for (uint32_t i=0; i < prim->num_indices; i++) {
        mesh->indices[mesh->num_indices++] = old_to_new[prim_indices[i]];
    }

    if (verbose) {
        debugf("index buffer:\n");
        for (int i = 0; i < mesh->num_indices; i++) {
            debugf("%d, ", mesh->indices[i]);
        }
        debugf("\n");
    }

    for (int i = 0; i < mesh->num_vertices; i++)
    {
        // map object space positions to uvs
        float* p = &mesh->verts[i][0];
        float u = 0.8f * atan2f(p[2], p[0]) / (2.0f * M_PI);
        float v = 0.5f * p[1];
        mesh->texcoords[i][0] = u;
        mesh->texcoords[i][1] = v;
    }

    return true;
}

void shadow_mesh_draw(struct shadow_mesh* mesh) {
    const bool verbose = false;
    if (mesh->num_indices == 0) {
        debugf("Warning: shadow mesh had an empty index buffer\n");
        return;
    }

    assert(mesh->num_vertices <= MESH_MAX_VERTICES);
    assert(mesh->num_indices <= MESH_MAX_INDICES);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    if (verbose) {
    for (int i=0;i<mesh->num_vertices;i++) {
        debugf("[%s] verts_proj[%d] = (%f, %f, %f), texcoords[%d] = (%f, %f)\n", __FUNCTION__,
            i,
            mesh->verts_proj[i][0],
            mesh->verts_proj[i][1],
            mesh->verts_proj[i][2],
            i,
            mesh->texcoords[i][0],
            mesh->texcoords[i][1]
        );
    }
    }

    if (verbose) {
    debugf("[%s] mesh->num_indices=%d\n", __FUNCTION__, mesh->num_indices);
    debugf("mesh->indices[] = {");
    for (int i=0;i<mesh->num_indices;i++) {
        debugf("%d, ", mesh->indices[i]);
    }
    debugf("}\n");
    }

    glVertexPointer(3, GL_FLOAT, sizeof(float) * 3, &mesh->verts_proj[0]);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 2, &mesh->texcoords[0]);
    glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, &mesh->indices[0]);
    //HACK Draw only a part
    //glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, &mesh->indices[0]);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void basic_mesh_extract(struct basic_mesh* mesh, model64_t* model)
{
    bool verbose = true;
    if (verbose) debugf("%s(%p, %p)\n", __FUNCTION__, mesh, model);

    memset(mesh, 0, sizeof(struct basic_mesh));

    primitive_t* prim = &model->meshes[0].primitives[0];
    attribute_t* attr = &prim->position;

    if (verbose) {
        debugf("Num primitives: %lu\n", model->meshes[0].num_primitives);
        debugf("Num vertices: %lu\n", prim->num_vertices);
        debugf("Num indices: %lu\n", prim->num_indices);

        debugf("Primitive 0 pos attribute:\nsize=%lu, type=%lu, stride=%lu, pointer=%p\n",
               attr->size,
               attr->type,
               attr->stride,
               attr->pointer);
    }

    assert(prim->num_vertices <= MESH_MAX_VERTICES);
    assert(prim->num_indices <= MESH_MAX_INDICES);
    assert(prim->position.type == GL_HALF_FIXED_N64);

    int bits = prim->vertex_precision;
    float scale = 1.0f / (1 << bits);
    if (verbose) debugf("position bits: %d, scale: %f\n", bits, scale);

    attribute_t* position = &prim->position;
    assert(position->size == 3);

    typedef int16_t u_int16_t __attribute__((aligned(1)));

    for (uint32_t vertex_id=0; vertex_id < prim->num_vertices; vertex_id++) {
        u_int16_t* pos = (u_int16_t*)(position->pointer + position->stride * vertex_id);
        float f[3] = {scale * pos[0], scale * pos[1], scale * pos[2]};
        // debugf("[%lu] (%d, %d, %d) -> (%f, %f, %f)\n", vertex_id, pos[0], pos[1], pos[2], f[0], f[1], f[2]);

        // debugf(" setting slot %d\n", new_idx);
        mesh->verts[vertex_id][0] = f[0];
        mesh->verts[vertex_id][1] = f[1];
        mesh->verts[vertex_id][2] = f[2];
    }
}

void set_frustum(float fov_factor)
{
    float aspect_ratio = (float)display_get_width() / (float)display_get_height();
    float near_plane = 1.0f;
    float far_plane = 50.0f;

    glLoadIdentity();
    glFrustum(-near_plane*aspect_ratio*fov_factor, near_plane*aspect_ratio*fov_factor, -near_plane*fov_factor, near_plane*fov_factor, near_plane, far_plane);
}

void setup()
{
    camera.distance = 7.0f;
    camera.rotation = 0.6f;

    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
    tempbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());

    for (uint32_t i = 0; i < NUM_TEXTURES; i++)
    {
        sprites[i] = sprite_load(texture_path[i]);
    }

    font_subtitle = rdpq_font_load("rom:/AlteHaasGroteskBold.font64");
    assert(font_subtitle);
    font_sign = rdpq_font_load("rom:/1942.font64");
    assert(font_sign);
    font_mono = rdpq_font_load("rom:/OSD.font64");
    assert(font_mono);

    //spr_sign1 = sprite_load("rom:/esp.rgba16.sprite");
    spr_sign2 = sprite_load("rom:/esp2.rgba16.sprite");
    spr_splash = sprite_load("rom:/splash.ci4.sprite");

    setup_sphere();
    make_sphere_mesh();

    setup_cube();

    setup_plane();
    make_plane_mesh();

    glMatrixMode(GL_PROJECTION);
    set_frustum(1.0f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, environment_color);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    float light_radius = 40.0f;

    for (uint32_t i = 0; i < 1; i++)
    {
        glEnable(GL_LIGHT0 + i);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, light_diffuse[i]);
        glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 2.0f/light_radius);
        glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 1.0f/(light_radius*light_radius));
    }

    set_diffuse_material();

    glGenTextures(NUM_TEXTURES, textures);

    #if 0
    GLenum min_filter = GL_LINEAR_MIPMAP_LINEAR;
    #else
    GLenum min_filter = GL_LINEAR;
    #endif

    for (uint32_t i = 0; i < NUM_TEXTURES; i++)
    {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        if (i == TEX_FLARE) {
            glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], &(rdpq_texparms_t){.s.repeats = 1, .t.repeats = 1});
        } else {
            glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], &(rdpq_texparms_t){.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE});
        }
    }

    model_gemstone = model64_load("rom:/gemstone.model64");
    assert(model_gemstone);

    shadow_mesh_clear(&smesh_gemstone);
    if (!shadow_mesh_extract(&smesh_gemstone, model_gemstone)) {
        debugf("Shadow mesh generation failed\n");
    }

    basic_mesh_extract(&bmesh_gemstone, model_gemstone);
    
    // Setup simulations

    const float height = 5.0f;
    if (NUM_SIMULATIONS >= 1) sim_init(&sims[0], (struct SimConfig){
        .root = {0.0f, height+8.0f, 3.0f}, .root_is_static = true}
        );
    if (NUM_SIMULATIONS >= 2) sim_init(&sims[1], (struct SimConfig){
        .root = {3.0f, height+7.0f, -3.0f}, .root_is_static = true}
        );
    if (NUM_SIMULATIONS >= 3) sim_init(&sims[2], (struct SimConfig){
        .root = {-3.0f, height+5.0f, -3.0f}, .root_is_static = true}
        );


    const int warmup_frames = 30;
    for (int i = 0; i < NUM_SIMULATIONS; i++) {
        sims[i].config.warmup_frames = warmup_frames;
    }

    for (int iter=0;iter<warmup_frames;iter++) {
        for (int i = 0; i < NUM_SIMULATIONS; i++) {
            sim_update(&sims[i]);
        }
    }
}

void render_flare()
{
    set_diffuse_material();
    glDisable(GL_LIGHTING);

    glBindTexture(GL_TEXTURE_2D, textures[TEX_FLARE]);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    
    //glEnable(GL_BLEND);

    float to_cam[3];
    vec3_sub(camera.computed_eye, &light_pos[0][0], to_cam);
    float dist = vec3_length(to_cam);
    glPointSize(400.0f / (dist+1.0f));

    glPushMatrix();
    glTranslatef(light_pos[0][0], light_pos[0][1], light_pos[0][2]);
    glBegin(GL_POINTS);
    //render_unit_cube_points();
    glColor3f(1.0f, 1.0f, 1.0f);
    glTexCoord2f(0.0f, 0.0f);

    glVertex3f(0.f, 0.f, 0.f);
    glEnd();
    glPopMatrix();

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    // debugf("flare basis:\n");
    // for (int i = 0; i < 4; i++) {
    //     debugf("[ %f %f %f %f ]\n", basis[i], basis[i + 4], basis[i + 8], basis[i + 12]);
    // }
}

void render_shadows(mat4_t object_to_world)
{
    const bool verbose = false;
    set_diffuse_material();
    glDisable(GL_LIGHTING);

    struct shadow_mesh* mesh = &smesh_gemstone;

    // transform light position to object space
    //      - will need inverse of the model matrix of the gemstone model
    // go over each vertex and compute light-to-vertex direction
    // trace a ray from vertex towards the floor plane (also transformed to object space)
    // if hit, set vertex to hit position
    // if missed, set vertex 1000 units away (TODO)
    // 

    MY_PROFILE_START(PS_RENDER_SHADOWS_INVERT, 0);
    mat4_t world_to_object;
    mat4_invert_rigid_xform2(object_to_world, world_to_object);
    MY_PROFILE_STOP(PS_RENDER_SHADOWS_INVERT, 0);
    float light_world[4] = {light_pos[0][0], light_pos[0][1], light_pos[0][2], 1.0f};
    float light_obj[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    mat4_mul_vec4(world_to_object, light_world, light_obj);

    if (verbose) {
        debugf("world_to_object: ");
        print_mat4(world_to_object);
        debugf("light_world: ");
        print_vec4(light_world);
        debugf("light_obj: ");
        print_vec4(light_obj);
    }

    float plane_normal[4] = {0.f, -1.0f, 0.0f, 0.0f};
    float plane_origin[4] = {0.0f, 0.1f, 0.0f, 1.0f};

    mat4_mul_vec4(world_to_object, plane_normal, plane_normal);
    mat4_mul_vec4(world_to_object, plane_origin, plane_origin);

    vec3_normalize_(plane_normal); // in case transformation matrix has scaling
    glPushMatrix();
    glMultMatrixf(&object_to_world[0][0]);

    if (false) {
        glPushMatrix();
        glBegin(GL_LINES);
        glColor3b(255, 0, 255);
        // glVertex3fv(&gem_origin_obj[0]);
        glVertex3f(0.0f, 0.0f, 0.0f);
        glColor3b(255, 0, 255);
        glVertex3fv(&light_obj[0]);
        glEnd();
        glPopMatrix();
    }

    bool missed_rays = false;

    for (int i=0; i<mesh->num_vertices; i++) {
        float* vert = &mesh->verts[i][0];
        float light_to_vert[3];
        vec3_sub(vert, light_obj, light_to_vert);
        vec3_normalize_(light_to_vert);

        if (verbose) debugf("light_to_vert: ");
        if (verbose) print_vec3(light_to_vert);

        float t=0.0f;
        // TODO light_to_vert.y < 0 so denom < 0?
        bool hit = intersect_plane(plane_normal, plane_origin, light_obj, light_to_vert, &t);

        if (verbose) debugf("[%d], hit=%s, t=%f\n", i, hit ? "true" : "false", t);

        if (!hit) {
            t=20.f;
            missed_rays=true;
        }

        vec3_scale(t, light_to_vert);
        vec3_add(light_obj, light_to_vert, &mesh->verts_proj[i][0]);

        if (!hit) {
            //debugf("ray missed at (%f, %f, %f) from (%f, %f, %f)\n", vert[0], vert[1], vert[2], light_obj[0], light_obj[1], light_obj[2]);
            // debugf("ray missed, dir (%f, %f, %f),\tlight=(%f, %f, %f)\n",
            //     light_to_vert[0], light_to_vert[1], light_to_vert[2],
            //     light_obj[0], light_obj[1], light_obj[2]);
            //if (time_secs > 5.0f) debug_freeze = true;
        }

        if (false) {
            glBegin(GL_LINES);
            glColor3b(255, 0, 255);
            glVertex3fv(vert);
            glColor3b(255, 0, 255);
            glVertex3fv(&mesh->verts_proj[i][0]);
            glEnd();
        }
    }


    // HACK: If any of the rays missed the plane, use last frames mesh.
    if (missed_rays) {
        memcpy(mesh->verts_proj, mesh->verts_proj_old, sizeof(mesh->verts_proj));
    } else {
        memcpy(mesh->verts_proj_old, mesh->verts_proj, sizeof(mesh->verts_proj));
    }

    glBindTexture(GL_TEXTURE_2D, textures[TEX_DIAMOND]);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    set_textured_shadow_material();

    glDepthMask(GL_FALSE);
    glDepthFunc(GL_LESS_INTERPENETRATING_N64);
    //glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!missed_rays) {
    glCullFace(GL_FRONT);
    shadow_mesh_draw(&smesh_gemstone);
    glCullFace(GL_BACK);
    shadow_mesh_draw(&smesh_gemstone);
    }

    //glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glPopMatrix();
}

void apply_postproc(surface_t *disp)
{

    const int lvl = 255;
    color_t colors[2] = {
        RGBA32(0, lvl, lvl, 255),
        RGBA32(lvl, 0, 0, 255),
    };

    struct {
        int x;
        int y;
    } offsets[2] = {{2,0}, {-2, 0}};

    surface_t* sources[2] = {disp, &tempbuffer};
    surface_t* targets[2] = {&tempbuffer, disp}; 

    for (int pass=0;pass<2;pass++) {
        rdpq_attach(targets[pass], NULL);

        rdpq_set_prim_color(colors[pass]);
        rdpq_set_fog_color(RGBA32(0, 0, 0, 128));
        rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
        //rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);
        rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (0,0,0,PRIM)));
        rdpq_blitparms_t blit={0};
        //blit.scale_x = 1.0f + zoom;
        //blit.scale_y = 1.0f + zoom;
        //blit.cx = 160*zoom;
        //blit.cy = 120*zoom;
        blit.filtering = false;
        blit.cx = offsets[pass].x;
        blit.cy = offsets[pass].y;
        rdpq_tex_blit(sources[pass], 0, 0, &blit);

        rdpq_fence();
        if (pass == 0) {
            rdpq_detach();
        } else {
            //rdpq_detach_show(); // done in main loop
        }
    }
}

// Parts
const float music_start = 2.0f;
const float smooth_start = 21.4f;
const float drop_start = 52.3f;
const float c_start = 60.f + 23.2f;
const float overlay_start = 30.5914f;

void run_animation()
{
    // 3.7515
    // 9.4984
    // 17.6798
    // 21.5510
    // 30.0517
    // 38.9914
    // 43.8603
    // 52.0417
    // 56.9107
    // 59.2653
    // 63.1365
    // 64.2141
    // 68.9633
    // 71.3977
    // 77.2644
    // 79.1002
    // 83.4902
    // 89.0376
    // 90.6739
    // 92.9088
    // 96.4608
    // 99.4141
    // 102.8463
    // 105.6798
    // 110.5887
    // 115.0585
    int part = PART_GEMS;
    int cam = ((int)(time_secs / 3) % NUM_CAMERAS);
    demo.overlay = 0;
    demo.impacts = 0.0f;
    demo.plane_brite = 0.7f;
    demo.glitch = 0.0f; // hack
    demo.shake = 0.0f;
    demo.flight_speed = 3.0f;
    demo.flight_brite = 1.0f;

    if (time_secs > 30.f) {
        demo.plane_brite = 0.5f;
    }
    if (time_secs > 57.f) {
        demo.plane_brite = 0.1f;
    }
 
    if (time_secs < 0.0) {
        part = PART_BLACK;
    } else if (time_secs < 3.7515f) {
        part = PART_INTRO;
        demo.glitch = 2.0f;
        if (time_secs > 1.8f) demo.glitch = 10.0f;
    } else if (time_secs < 9.4984f) {
        demo.glitch = 4.f;
        if (time_secs > 5.f) demo.glitch = 2.f;
        part = PART_VIDEO;
    } else if (time_secs < 17.6798f) { 
        part = PART_VIDEO;
        demo.glitch = 1.f;
    } else if (time_secs < 19.6798f) { 
        part = PART_VIDEO;
        demo.glitch = 0.f;
    } else if (time_secs < 21.5510f) { // smooth start
        float tl = time_secs - 21.5510f;
        part = PART_GEMS;
        cam = ((int)(tl / 3) % NUM_CAMERAS);
        //cam = CAM_OVERVIEW;
    } else if (time_secs < 29.9999f) {
        part = PART_GEMS;
    } else if (time_secs < 35.009) { // smooth hats
        part = PART_SIGN1;
        demo.glitch = 1.f;
    } else if (time_secs < 38.5) { 
        part = PART_FLIGHT;
        cam = CAM_OVERVIEW;

        if (time_secs > 37.f) {
        demo.overlay = 1;
        }
    } else if (time_secs < 52.8603f) {
        part = PART_FLIGHT;
        demo.overlay = 1;
    //} else if (time_secs < 52.0417f) { // drop
    } else if (time_secs < 56.f /*56.9107f*/) {
        part = PART_VIDEO;
        demo.shake = 1.0f;
        demo.glitch = 5.0f;
    } else if (time_secs < 59.2653f) {
        demo.impacts = 0.03f;
        demo.shake = 1.0f;
        demo.glitch = 2.0f;
    } else if (time_secs < 63.1365f) {
        part = PART_VIDEO;
        demo.glitch = 3.0f;
    } else if (time_secs < 64.2141f) {
        demo.shake = 1.0f;
        demo.glitch = 2.0f;
    } else if (time_secs < 68.9633f) { // drop, 2nd half
        part = PART_VIDEO;
        demo.glitch = 3.0f;
    } else if (time_secs < 71.3977f) {
        part = PART_FLIGHT;
        demo.shake = 1.0f;
        demo.glitch = 0.0f;
        demo.flight_speed = 12.0f;
        demo.flight_brite = 0.8f;
    } else if (time_secs < 77.2644f) {
        part = PART_VIDEO;
        demo.glitch = 1.0f;
    } else if (time_secs < 79.1002f) {
        demo.glitch = 0.0f;
        demo.shake = 3.0f;
    } else if (time_secs < 83.4902f) { // C part arpeggio
        demo.glitch = 8.0f;
        part = PART_VIDEO;
    } else if (time_secs < 89.0376f) {
        part = PART_VIDEO;
    } else if (time_secs < 90.6739f) {
        part = PART_VIDEO;
    } else if (time_secs < 92.9088f) {
        part = PART_FLIGHT;
        demo.flight_speed = -1.0f;
        demo.flight_brite = 0.5f;
    } else if (time_secs < 96.4608f) {
        part = PART_VIDEO;
    } else if (time_secs < 99.4141f) { // ending verse
    } else if (time_secs < 102.8463f) {
    } else if (time_secs < 114.0000f) {
        cam = CAM_OVERVIEW;
    } else if (time_secs < 121.0000f) {
        part = PART_END;
        if (time_secs > 117.f) demo.glitch = 2.f;
    } else {
        part = PART_BLACK;
    }

    demo.current_part = part;
    viewer.active_camera = cam;

    // if (time_secs < 1.0f) {
    //     demo.current_part = PART_INTRO;
    // } else if (time_secs < smooth_start) {
    //     demo.current_part = PART_VIDEO;
    // } else if (time_secs > smooth_start + 30.f && time_secs < smooth_start + 60.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    // } else if (time_secs > 16.0f && time_secs < 18.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    //     // chorus
    // } else if(time_secs > 23.0f && time_secs < 27.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    // } else if (time_secs > 29.0f && time_secs < 31.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    // } else if (time_secs > 32.0f && time_secs < 33.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    // } else if (time_secs > 35.0f && time_secs < 40.0f) {
    //     // debugf("anim %s at %d\n", __FUNCTION__, __LINE__);
    //     demo.current_part = PART_VIDEO;
    // } else {
    //     demo.current_part = PART_GEMS;
    // }

    demo.part_changed = false;

    static int last_part;
    if (demo.current_part != last_part) {
        debugf("Changed part %d -> %d\n", last_part, demo.current_part);
        last_part = demo.current_part;
        demo.part_changed = true;
    }

    float slump_start = 60+37.0f;
    if (time_secs > slump_start) {
        for (int i = 0; i < NUM_SIMULATIONS; i++) {
            if (time_secs > slump_start + i) {
                sims[i].config.root_is_static = false;
            }
        }
    }

    //demo.current_part = PART_VIDEO; // HACK: use only video

    // Cameras
    
    // viewer.active_camera = CAM_CLOSEUP;

    // if (time_secs > 8) {
    //     viewer.active_camera = CAM_OVERVIEW;
    // }
    // if (time_secs > 16) {
    //     viewer.active_camera = CAM_CLOSEUP;
    // }
    // if (time_secs > 23) {
    //     viewer.active_camera = CAM_ACTION;
    // }
    // if (time_secs > 31) {
    //     viewer.active_camera = ((int)(time_secs / 3) % NUM_CAMERAS);
    // }

}

void render(surface_t *disp)
{
    rdpq_attach(disp, &zbuffer);

    rdpq_mode_dithering(DITHER_NOISE_SQUARE);
    gl_context_begin();

    glFogf(GL_FOG_START, 5);
    glFogf(GL_FOG_END, 40);
    glFogfv(GL_FOG_COLOR, environment_color);
    glEnable(GL_FOG);

    const float fudge = 0.01f;
    glClearColor(environment_color[0] + fudge, environment_color[1] + fudge, environment_color[2] + fudge, environment_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera.computed_eye[0] = cos(camera.rotation) * camera.distance;
    camera.computed_eye[1] = 4.0f;
    camera.computed_eye[2] = -sin(camera.rotation) * camera.distance;
    
    glLoadIdentity();

    if (viewer.active_camera == CAM_CLOSEUP) {
        glMatrixMode(GL_PROJECTION);
        set_frustum(0.6f);

        static float cam_target[3];
        float cam_target_blend = 0.1f;
        float inv_cam_target_blend = 1.0f - cam_target_blend;

        cam_target[0] = inv_cam_target_blend * cam_target[0] + cam_target_blend * sims[0].pose.origin[0];
        cam_target[1] = inv_cam_target_blend * cam_target[1] + cam_target_blend * sims[0].pose.origin[1];
        cam_target[2] = inv_cam_target_blend * cam_target[2] + cam_target_blend * sims[0].pose.origin[2];

        glMatrixMode(GL_MODELVIEW);
        gluLookAt(
            camera.computed_eye[0], camera.computed_eye[1], camera.computed_eye[2],
            cam_target[0], cam_target[1], cam_target[2],
            0, 1, 0);
        
    }
    else if (viewer.active_camera == CAM_OVERVIEW) {
        glMatrixMode(GL_PROJECTION);
        set_frustum(1.1f);

        float yshake = 0.05f * sin(0.5f*time_secs);
        float target_xshake = 0.0f + 0.025f * cos(time_secs*0.3f);
        float eye_yshake = 1.0f + 0.015f* sin(time_secs*1.3f);
        glMatrixMode(GL_MODELVIEW);
        gluLookAt(
            2.0f, 10.0f * eye_yshake, 1.0f,
            1.f * target_xshake, 1.0f, 0.0f,
            0 + yshake, 1 - yshake, 0);
    } else if (viewer.active_camera == CAM_ACTION) {
        glMatrixMode(GL_PROJECTION);
        set_frustum(1.0f);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        float *pos = &sims[0].pose.origin[0];
        float angle=time_secs*0.7f;
        gluLookAt(
            pos[0] + 3.0f*cos(angle), pos[1] + 2.0f, pos[2] + 3.0f*sin(angle),
            pos[0], pos[1], pos[2],
            0, 1, 0);
    }

    // float gshake = demo.shake * 0.5f * sinf(15.1f*time_secs);
    // glRotatef(gshake, 0.1f, 0.5f, 0.5f);

    glMatrixMode(GL_MODELVIEW);
    // camera_transform(&camera);

    float dist = 4.0f;
    float langle = time_secs * 0.3f;

    if (!debug_freeze) {
        light_pos[0][0] = dist * cos(langle);
        light_pos[0][1] = 10.0f + 0.5 * sin(time_secs*0.8f);
        light_pos[0][2] = dist * sin(langle);
    }

    glLightfv(GL_LIGHT0, GL_POSITION, light_pos[0]);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[TEX_TABLE]);
    
    MY_PROFILE_START(PS_RENDER_BG, 0);
    if (tweak_dont_test_plane_depth) {
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
    }

    set_plane_material(demo.plane_brite);
    render_plane();
    if (tweak_wait_after_render_plane) {
        rspq_wait();
    }

    if (tweak_dont_test_plane_depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }
    MY_PROFILE_STOP(PS_RENDER_BG, 0);

    for (int i = 0; i < NUM_SIMULATIONS; i++) {
        sim_prepare_render(&sims[i], sim_object_to_worlds[i]);
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    MY_PROFILE_START(PS_RENDER_SHADOWS, 0);
    for (int i = 0; i < NUM_SIMULATIONS; i++) {
        render_shadows(sim_object_to_worlds[i]);
        if (tweak_sync_after_shadows) {
            rspq_wait();
        }
    }
    MY_PROFILE_STOP(PS_RENDER_SHADOWS, 0);

    MY_PROFILE_START(PS_RENDER_SIM, 0);
    for (int i = 0; i < NUM_SIMULATIONS; i++) {
        if (tweak_enable_sim_rendering) {
            sim_render(&sims[i], sim_object_to_worlds[i]);
            if (tweak_wait_after_sim_render) {
                rspq_wait();
            }
        } else {
            mat4_set_identity(sim_object_to_worlds[i]);
        }
    }
    MY_PROFILE_STOP(PS_RENDER_SIM, 0);

    gl_context_end();

    if (tweak_sync_before_shadows) {
        rspq_wait();
    }

    gl_context_begin();



    if (false) {
        MY_PROFILE_START(PS_RENDER_FLARE, 0);
        render_flare();
        MY_PROFILE_STOP(PS_RENDER_FLARE, 0);
    }

    gl_context_end();

    if (false) {
        MY_PROFILE_START(PS_RENDER_POSTPROC, 0);
        rdpq_detach();
        apply_postproc(disp);
        MY_PROFILE_STOP(PS_RENDER_POSTPROC, 0);
    }
    else {
        rdpq_detach();
    }
}

void render_flight(surface_t *disp)
{
    rdpq_attach(disp, &zbuffer);
    gl_context_begin();

    float white[] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float fudge = 0.01f;
    glClearColor(white[0], white[1], white[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float plane_tile = PLANE_SIZE / PLANE_SEGMENTS;

    glFogf(GL_FOG_START, 5);
    glFogf(GL_FOG_END, 40);
    glFogfv(GL_FOG_COLOR, white);
    glEnable(GL_FOG);

    glMatrixMode(GL_PROJECTION);
    set_frustum(0.6f);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    gluLookAt(
        0.0f, -4.0f + 0.5 * sin(time_secs*0.5f), 4.5f,
        0.0f, 0.0f, 0.0f,
        0, 1, 0);

    float zmove = fmodf(time_secs*demo.flight_speed, plane_tile);

    glTranslatef(0.0f, 0.0f, zmove);
    glRotatef(sin(time_secs*0.3f) * 10.0f, 0.0f, 0.0f, -1.0f);
    //glRotatef(sin(time_secs*0.6f+1.0f) * 5.0f, 0.5f, 0.5f, 0.0f);
    // glRotatef(45.f + time_secs * 10.0f, 1.0f, 0.0f, 0.0f);
    
    glDisable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);

    set_plane_material(demo.flight_brite);
    GLuint tex = textures[TEX_CLOUDS];
    if (demo.flight_speed > 4.0f) tex = textures[TEX_CLOUDS_FAST];
    if (demo.flight_speed < 0.0f) tex = textures[TEX_CLOUDS_DARK];
    glBindTexture(GL_TEXTURE_2D, tex);
    glPushMatrix();
        glRotatef(180.f, 0.0f, 0.0f, 1.0f);
        render_plane();
    glPopMatrix();

    gl_context_end();
    rdpq_detach();
}

static uint32_t audio_sample_time = 0;

void audio_poll(void) {	
	if (audio_can_write()) {    	
		short *buf = audio_write_begin();
        int bufsize = audio_get_buffer_length();

        if (tweak_sync_before_mixer_poll) {
            rspq_wait();
        }

		mixer_poll(buf, bufsize);

        if (tweak_sync_after_mixer_poll) {
            rspq_wait();
        }

        audio_sample_time += (uint32_t)bufsize;
		audio_write_end();
	}
}
        
void render_noise(surface_t* disp) {
    rdpq_set_mode_standard();
    const int lvl = 14;
    rdpq_set_prim_color(RGBA32(lvl, lvl, lvl, 255));
    rdpq_mode_combiner(RDPQ_COMBINER1((NOISE, 0, PRIM, TEX0), (0, 0, 0, TEX0)));
    rdpq_tex_blit(disp, 0, 0, NULL);
}

void render_video(mpeg2_t* mp2, surface_t* disp)
{
    int ret = throttle_wait();
    if (ret < 0) {
        debugf("videoplayer: frame too slow (%d Kcycles)\n", -ret);
    }

    if (music_enabled) {
        audio_poll();
    }

    if (mpeg2_next_frame(mp2)) {
        rdpq_attach(disp, &zbuffer);

        if (tweak_sync_before_video) {
            rspq_wait();
        }

        mpeg2_draw_frame(mp2, disp);

        if (tweak_sync_after_video) {
            rspq_wait();
        }

        render_noise(disp);

        color_t black = RGBA32(0, 0, 0, 0xFF);
        color_t col = RGBA32(248, 227, 106 , 255);

        //color_t white = RGBA32(0xFF, 0xFF, 0xFF, 0xFF);

        struct {
            color_t color;
            float start;
            const char* msg1;
            const char* msg2;
            int x1;
            int x2;
        } messages[] = {
            {col, c_start + 0.5f, "       our longing is our pledge,", "    and blessed are the homesick,", 40, 40},
            {col, c_start + 3.5f, "", "       for they shall come home.", 50, 50},
        };

        // "and thereby attaining a \"perfecting unity\" with the Great Being, which would bring about mankind’s \"ultimate regeneration\""

        const float duration = 3.0f;

        for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
            if (time_secs > messages[i].start && time_secs < messages[i].start + duration) {
                rdpq_font_begin(black);
                rdpq_font_position(messages[i].x1, 240 - 55+2);
                rdpq_font_print(font_subtitle, messages[i].msg1);
                rdpq_font_position(messages[i].x2, 240 - 35+2);
                rdpq_font_print(font_subtitle, messages[i].msg2);
                rdpq_font_end();
                rdpq_font_begin(messages[i].color);
                rdpq_font_position(messages[i].x1, 240 - 55);
                rdpq_font_print(font_subtitle, messages[i].msg1);
                rdpq_font_position(messages[i].x2, 240 - 35);
                rdpq_font_print(font_subtitle, messages[i].msg2);
                rdpq_font_end();
                break;
            }
        }

        rdpq_detach();
    }
    else {
        //debugf("Video ended\n");
    }
}

const int sign_text_y = 40;
const int sign_text_margin = 30;

void render_ending(surface_t* disp)
{
    rdpq_attach(disp, &zbuffer);

    rdpq_set_mode_fill(RGBA32(0, 0, 64, 255));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());

    char* msg = "Salvation unlikely.";

    rdpq_font_begin(RGBA32(242,242,245, 0xFF));

    rdpq_font_position(20, sign_text_y);
    rdpq_font_print(font_sign, "CONCLUSION");
    rdpq_font_position(20, sign_text_y + sign_text_margin);
    rdpq_font_print(font_sign, msg);
    rdpq_font_end();

    render_noise(disp);

    rdpq_detach();
    rspq_wait();
}

void render_black(surface_t* disp)
{
    rdpq_attach(disp, &zbuffer);

    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
    rdpq_detach();
    rspq_wait();
}

void render_intro(surface_t* disp)
{
    rdpq_attach(disp, &zbuffer);

    rdpq_set_mode_fill(RGBA32(0, 0, 64, 255));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());

    char* msg = "\"Bringing You Back\"";

    rdpq_font_begin(RGBA32(242,242,245, 0xFF));

    rdpq_font_position(20, sign_text_y);
    rdpq_font_print(font_sign, "Supersensory Investigation");
    rdpq_font_position(20, sign_text_y + sign_text_margin);
    rdpq_font_print(font_sign, msg);
    rdpq_font_position(20, 240 - 4*20);
    rdpq_font_print(font_sign, "");
    rdpq_font_position(20, 240 - 3*20);
    rdpq_font_print(font_sign, "Codes by cce AKA tykkiman");
    rdpq_font_position(20, 240 - 2*20);
    rdpq_font_print(font_sign, "Audio by miika");
    rdpq_font_end();

    render_noise(disp);

    rdpq_detach();
    rspq_wait();
}

void render_sign(surface_t* disp)
{
    rdpq_attach(disp, &zbuffer);

    rdpq_set_mode_fill(RGBA32(0, 0, 64, 255));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());

    // rdpq_set_mode_fill(RGBA32(77,77,81, 255));
    // rdpq_fill_rectangle(17, 128, 196, 21);

    rdpq_font_begin(RGBA32(242,242,245, 0xFF));
    rdpq_font_position(20, sign_text_y);
    rdpq_font_print(font_sign, "EXPERIMENT");
    rdpq_font_position(20, sign_text_y + sign_text_margin);
    rdpq_font_print(font_sign, "Extra Scholastic Perception");
    rdpq_font_position(230, 20);
    //rdpq_font_printf(font_mono, "%.3f", time_secs);
    rdpq_font_end();

    render_noise(disp);

    rdpq_detach();
    rspq_wait();
}

void render_overlay(surface_t* disp)
{
    rspq_wait();
    rdpq_attach(disp, NULL);

    surface_t bkgsurf = sprite_get_pixels(spr_sign2);
    //rdpq_set_mode_copy(true);
    rdpq_set_mode_standard();
    //rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY_CONST);

    float local_time = time_secs - overlay_start ;
    if (local_time < 0) local_time = 0.0f;

    float scale = 1.0f; // + 1.0f/(0.1f+local_time*2); //sin(time_secs * 0.9) * .1f;

    uint32_t mask = rand();
    uint32_t mask2 = rand_func((uint32_t)(time_secs*2));
    int size = spr_sign2->width / 5;

    int step = time_secs*5;
    bool animated = true;

    int xstart = 160 - (spr_sign2->width/2) * scale;
    for (int i=0;i<5;i++) {
        int idx = rand_func(step+i) % 5;
        rdpq_blitparms_t blit={0};
        blit.s0 = idx*size;
        blit.width = size;
        //blit.scale_y = 1.0f + 0.1f * ;
        blit.scale_x = scale;
        blit.scale_y = scale;
        blit.filtering = true;
        int noise = ((mask & (1<<idx)) > 0);
        int shown = ((mask2 & (1<<idx)) > 0);
        if (!animated) shown = true;
        if (shown) {
        rdpq_set_fog_color(RGBA32(128, 128, 128, 192 + noise * 16));
        float ofs = 0.0f; // 10 * cos(time_secs + i) * scale;
        int x = xstart + i*size*scale;
        rdpq_tex_blit(&bkgsurf, x, 80 + ofs, &blit);
        rspq_wait();
        }
    }

    rdpq_detach();
    rspq_wait();
}

void render_glitch(surface_t* disp)
{
    if (demo.glitch <= 0.f) return;
    if (randf()*5.f > demo.glitch) {
        return;
    }

    rspq_wait();
    rdpq_attach(disp, NULL);

        //rdpq_set_prim_color(colors[pass]);
        //rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
        //rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (0,0,0,PRIM)));
        bool colors = false;
        if (colors) {
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,0,PRIM,0), (0,0,0,PRIM)));
        } else {
            rdpq_set_mode_copy(false);
        }
        //rdpq_set_fog_color(RGBA32(255, 0, 0, 255));

        int n = rand() % (int)demo.glitch;

        for (int iter=0; iter<n; iter++) {
            rdpq_blitparms_t blit={0};
            //blit.scale_x = 1.0f + zoom;
            //blit.scale_y = 1.0f + zoom;
            //blit.cx = 160*zoom;
            //blit.cy = 120*zoom;

            if (colors) {
            if ((rand()&0xF) == 0) {
            rdpq_set_prim_color(RGBA32(255, 0, 0, 255));
            } else {
            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            }
            }

            uint32_t x = 0;
            uint32_t y =rand() % 240; 
            uint32_t w = 320;
            uint32_t h = rand() % 25;
            blit.scale_y = 1.5f;
            if (x+w > 320) w = 320-x;
            if (y+h > 240) h = 240-y;
            if (y+h*blit.scale_y > 240) {
                h/=2;
            }
            if (h <= 0) h = 1;
            //debugf("%lu, %lu, wh: %lu, %lu\n", x, y, w, h);

            blit.filtering = false;
            blit.s0 = x;
            blit.t0 = y;
            blit.width = w;
            blit.height = h;
            rdpq_tex_blit(disp, x, y, &blit);
        }

    //surface_t bkgsurf = sprite_get_pixels(spr_sign2);
    // for (int y=0;y<disp->height;y++) {
    //     uint32_t* line = (uint32_t*)&((uint8_t*)(&disp->buffer))[y*disp->stride];
    //     line[100] = line[50];
    // }

    rdpq_detach();
    rspq_wait();
}

void render_splash(surface_t* disp) 
{
    rdpq_attach(disp, NULL);
    rdpq_set_mode_copy(false);
    rdpq_sprite_blit(spr_splash, 0.0f, 0.0f, NULL);
    rdpq_detach();
}


static void seek_to(float secs) {
    float time = secs * 44100.f;
    if (time < 0.f) time =0.0f;
    if (time > music_wav.wave.len) time = music_wav.wave.len-1;
    mixer_ch_set_pos(0, time);
}

int main()
{
	debug_init_isviewer();
	debug_init_usblog();

    debugf("Built at %s\n", __TIMESTAMP__);
    
    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE_FETCH_NEEDED);
    
    rdpq_init();
    gl_init();
    //rdpq_debug_start();

    my_profile_init();

    demo.interactive = false;
    demo.current_part = PART_VIDEO;

    glEnable(GL_MULTISAMPLE_ARB);

#if DEBUG_RDP
    rdpq_debug_start();
    //rdpq_debug_log(true);
#endif

    setup();

    controller_init();
    timer_init();

	audio_init(44100, 4);
	mixer_init(2);

	mpeg2_t mp2;
	mpeg2_open(&mp2, "rom:/supercut.m1v");
	float video_fps = mpeg2_get_framerate(&mp2);
	throttle_init(video_fps, 0, 8);

    if (music_enabled) {
        const char* songpath = "/20230728_6_neo_occult_wave_blessed1.wav64";
        wav64_open(&music_wav, songpath);
    }

    // debugf("DEBUG HACK: only play video\n");
    // while (true) {
    //      if (music_enabled) {
    //          audio_poll();
    //      }
    //     render_video(&mp2);
    // }

    #ifdef NDEBUG
    surface_t *disp = display_get();
    render_splash(disp);
    rdpq_attach(disp, NULL);
    rdpq_detach_show();
    wait_ms(4000);
    rdpq_attach(display_get(), NULL);
    rdpq_set_mode_fill(RGBA32(0, 0, 0, 255));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
    rdpq_detach_show();
    wait_ms(1000);
    #endif
    

    if (music_enabled) {
        wav64_play(&music_wav, 0);
    }

    while (1)
    {
        if (music_enabled) {
            time_secs = mixer_ch_get_pos(0) / 44100.f;
        } else {
            time_secs = TIMER_MICROS(timer_ticks()) / 1e6 + time_secs_offset;
            if (time_secs < 0) time_secs = 0.f;
        }

        debugf("[%.4f] frame %d, part: %d\n", time_secs, time_frames, demo.current_part);

        controller_scan();

        struct controller_data pressed = get_keys_pressed();
        struct controller_data down = get_keys_down();

        if (down.c[0].start) {
            demo.interactive = !demo.interactive;
            debugf("%s mode\n", demo.interactive ? "Interactive" : "Playback");
            if (demo.interactive == false) demo.moved = false;
        }
        surface_t *disp = display_get();

        if (demo.interactive) {
            if (pressed.c[0].L) {
                seek_to(time_secs - 1.0f);
                time_secs_offset -= 1.0f;
                //viewer.active_camera--;
                //if (viewer.active_camera < 0) viewer.active_camera = NUM_CAMERAS - 1;
                //debugf("Active camera: %d\n", viewer.active_camera);
            }

            if (pressed.c[0].R) {
                seek_to(time_secs + 1.0f);
                time_secs_offset += 1.0f;
                // viewer.active_camera++;
                // if (viewer.active_camera >= NUM_CAMERAS) viewer.active_camera = 0;

                // debugf("Active camera: %d\n", viewer.active_camera);
            }

            if (down.c[0].A) {
                debugf("show wires\n");
                for (int i = 0; i < NUM_SIMULATIONS; i++) {
                    sims[i].debug.show_wires = !sims[i].debug.show_wires;
                }
            }

            if (down.c[0].B) {
                // debugf("[%.4f] frame %d\n", time_secs, time_frames);
                debugf("%.4f\n", time_secs);
                // float vel[3] = {randf()-0.5f, randf()-0.5f, randf()-0.5f};
                // debugf("impact (%f, %f, %f)\n", vel[0], vel[1], vel[2]);
                // sim_apply_impact(&sims[0], vel);
            }

            const float nudge = 0.02f;
            bool c_pressed = false;

            if (down.c[0].C_up) {
                tweak_double_sided_gems = !tweak_double_sided_gems;
                debugf("double sided: %s\n", tweak_double_sided_gems ? "ON" : "OFF");

            }

            if (down.c[0].C_down) {
                debug_z_shift -= nudge;
                c_pressed = true;
            }

            if (down.c[0].C_left) {
                debug_x_shift -= nudge;
                c_pressed = true;
            }

            if (down.c[0].C_right) {
                debug_x_shift += nudge;
                c_pressed = true;
            }

            if (down.c[0].left) {
                demo.current_part--;
                if (demo.current_part < 0) demo.current_part = NUM_PARTS-1;
                debugf("Part: %d\n", demo.current_part);
                demo.moved = true;
            }

            if (down.c[0].right) {
                demo.current_part++;
                if (demo.current_part >= NUM_PARTS) demo.current_part = 0;
                debugf("Part: %d\n", demo.current_part);
                demo.moved = true;
            }

            if (c_pressed) {
                debugf("xz shift: (%f, %f)\n", debug_x_shift, debug_z_shift);
            }

            float y = pressed.c[0].y / 128.f;
            float x = pressed.c[0].x / 128.f;
            float mag = x * x + y * y;

            if (fabsf(mag) > 0.01f) {
                camera.distance += y * 0.2f;
                camera.rotation = camera.rotation - x * 0.05f;
            }
        }

        MY_PROFILE_START(PS_MUSIC, 0);
        if (music_enabled) {
            audio_poll();
        }
        MY_PROFILE_STOP(PS_MUSIC, 0);

        if (!(demo.interactive && demo.moved)) {
            run_animation();
        }

        if (demo.current_part == PART_GEMS) {
            if (demo.impacts > 0.0f) {
                for (int i=0;i<NUM_SIMULATIONS;i++) {
                    if (randf() < demo.impacts) {
                        const float scale=0.1f;
                        float vel[3] = {scale*(randf()-0.5f), scale*(randf()), scale*(randf()-0.5f)};
                        sim_apply_impact(&sims[i], vel);
                    }
                }
            }

            MY_PROFILE_START(PS_UPDATE, 0);
            if (true || time_frames < 40) {
                for (int i = 0; i < NUM_SIMULATIONS; i++) {
                    sim_update(&sims[i]);
                }
            }
            MY_PROFILE_STOP(PS_UPDATE, 0);

            MY_PROFILE_START(PS_RENDER, 0);
            render(disp);
            MY_PROFILE_STOP(PS_RENDER, 0);

            my_profile_next_frame();

            #if LIBDRAGON_MY_PROFILE
            if (time_frames == 128) {
                // my_profile_dump();
                // my_profile_init();
            }
            #endif
        } else if (demo.current_part == PART_VIDEO) {

            if (music_enabled) {
                audio_poll();
            }

            if (tweak_sync_before_video) {
                rspq_wait();
            }
            render_video(&mp2, disp);
            if (tweak_sync_after_video) {
                rspq_wait();
            }
        }
        else if (demo.current_part == PART_END) {
            render_ending(disp);
        }
        else if (demo.current_part == PART_INTRO) {
            render_intro(disp);
        }
        else if (demo.current_part == PART_SIGN1) {
            render_sign(disp);
            rspq_wait();
        }
        else if (demo.current_part == PART_BLACK) {
            render_black(disp);
            rspq_wait();
        } else if (demo.current_part == PART_FLIGHT) {
            render_flight(disp);
            rspq_wait();
        } else {
            debugf("Invalid demo part %d\n", demo.current_part);
        }

        if (music_enabled) {
            audio_poll();
        }

        if (demo.overlay == 1) {
            render_overlay(disp);
        }

        if (demo.glitch > 0) {
            render_glitch(disp);
        }

        rdpq_attach(disp, &zbuffer);

        if (demo.interactive) {
            rdpq_font_begin(RGBA32(0xFF, 0xFF, 0xFF, 0xFF));
            rdpq_font_position(10, 20);
            rdpq_font_printf(font_subtitle, "Edit %02d:%02d (%.3f)\n", (int)(time_secs/60), ((int)time_secs)%60, time_secs);
            rdpq_font_end();
        }

        rdpq_detach_show();

        // if (DEBUG_RDP)
        //     rspq_wait();
        time_frames++;
    }

}
