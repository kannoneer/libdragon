#include <libdragon.h>
#include "../../src/model64_internal.h"
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

#include "mymath.h"
#include "sim.h"

#include "profile.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

static const bool music_enabled = false;

static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;
static surface_t tempbuffer;

static float time_secs = 0.0f;
static int time_frames = 0;

static bool debug_freeze = false;

bool tweak_double_sided_gems = true;
bool tweak_dont_test_plane_depth = true; // 3% faster plane drawing when enabled

#define NUM_TEXTURES (9)
#define TEX_CEILING (4)
#define TEX_FLARE (5)
#define TEX_ICON (6)
#define TEX_DIAMOND (7)
#define TEX_TABLE (8)

static GLuint textures[NUM_TEXTURES];

static model64_t* model_gemstone = NULL;

static const GLfloat environment_color[] = { 0.03f, 0.03f, 0.05f, 1.f };
//static const GLfloat environment_color[] = { 0.0f, 0.0f, 0.0f, 1.f };

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

static struct {
    bool interactive;
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
    { 1.0f, 0.1f, 0.0f, 1.0f },
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
};

static sprite_t *sprites[NUM_TEXTURES];

static void set_diffuse_material()
{
    GLfloat mat_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);
}

static void set_gemstone_material()
{
    GLfloat color[] = { 4.0f, 4.0f, 4.0f, 0.75f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
}

static void set_textured_shadow_material()
{
    const float shade = 0.0f;
    GLfloat color[] = { shade, shade, shade, 0.6f };
    //glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
    glColor4fv(color);
}

#define MESH_MAX_VERTICES (36)
#define MESH_MAX_INDICES (60)

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

static float debug_z_shift = 0.0f;
static float debug_x_shift = 0.0f;

void sim_render(struct Simulation* s, mat4_t out_basis)
{
    PROFILE_START(PS_RENDER_SIM_SETUP, 0);
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
        mat4_t inverse;
        mat4_t result;
        mat4_invert_rigid_xform(basis, inverse);
        mat4_mul(inverse, basis, result);

        debugf("basis:\n");
        print_mat4(basis);
        debugf("inverse:\n");
        print_mat4(inverse);
        debugf("result:\n");
        print_mat4(result);
    }

    if (false) {
        debugf("shift:\n");
        print_mat4(shift);

        debugf("u: (%f, %f, %f)\n", s->pose.u[0], s->pose.u[1], s->pose.u[2]);
        debugf("v: (%f, %f, %f)\n", s->pose.v[0], s->pose.v[1], s->pose.v[2]);
        debugf("n: (%f, %f, %f)\n", s->pose.n[0], s->pose.n[1], s->pose.n[2]);

        debugf("basis:\n");
        print_mat4(basis);
    }

    PROFILE_STOP(PS_RENDER_SIM_SETUP, 0);
    PROFILE_START(PS_RENDER_SIM_DRAWCALLS, 0);
    glEnable(GL_BLEND);
    set_gemstone_material();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glEnable(GL_LIGHTING);
    glBindTexture(GL_TEXTURE_2D, textures[TEX_CEILING]);

    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);

    glPushMatrix();
        glMultMatrixf(&basis[0][0]);

        glCullFace(GL_FRONT);
        model64_draw(model_gemstone);
        glCullFace(GL_BACK);
        if (tweak_double_sided_gems) {
            model64_draw(model_gemstone);
        }

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_TEXTURE_GEN_S);
        glDisable(GL_TEXTURE_GEN_T);
        glDisable(GL_BLEND);
    glPopMatrix();
    PROFILE_STOP(PS_RENDER_SIM_DRAWCALLS, 0);
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
    if (mesh->num_indices == 0) {
        debugf("Warning: shadow mesh had an empty index buffer\n");
        return;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(float) * 3, &mesh->verts_proj[0]);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 2, &mesh->texcoords[0]);
    glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, &mesh->indices[0]);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
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

    glFogf(GL_FOG_START, 5);
    glFogf(GL_FOG_END, 40);
    glFogfv(GL_FOG_COLOR, environment_color);
    glEnable(GL_FOG);

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

    if (!shadow_mesh_extract(&smesh_gemstone, model_gemstone)) {
        debugf("Shadow mesh generation failed\n");
    }
    
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


    for (int iter=0;iter<10;iter++) {
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

    PROFILE_START(PS_RENDER_SHADOWS_INVERT, 0);
    mat4_t world_to_object;
    mat4_invert_rigid_xform2(object_to_world, world_to_object);
    PROFILE_STOP(PS_RENDER_SHADOWS_INVERT, 0);
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glCullFace(GL_FRONT);
    shadow_mesh_draw(&smesh_gemstone);
    glCullFace(GL_BACK);
    shadow_mesh_draw(&smesh_gemstone);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    glPopMatrix();
}

void apply_postproc(surface_t *disp)
{

    /*
    rdpq_set_mode_standard();
    //rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    //rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    //rdpq_set_mode_copy(true);
    // rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,0),       (0,0,0,PRIM)));
    const int lvl = 8;
    rdpq_set_prim_color(RGBA32(lvl, lvl, lvl, 255));
    rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,TEX0), (0,0,0,TEX0)));
    rdpq_tex_blit(disp, 0, 0, NULL);
    */

    const int lvl = 255;
    color_t colors[2] = {
        RGBA32(0, lvl, lvl, 255),
        RGBA32(lvl, 0, 0, 255),
    };

    struct {
        int x;
        int y;
    } offsets[2] = {{2,0}, {-2, 0}};

    // surface_t* sources[2] = {disp, &tempbuffer};
    // surface_t* targets[2] = {&tempbuffer, disp};
    surface_t* sources[2] = {disp, &tempbuffer};
    surface_t* targets[2] = {&tempbuffer, disp}; // FIXME doesnhas no effect?

    rdpq_mode_push();
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
        rdpq_detach();
    }
    rdpq_mode_pop();

    /*
    rdpq_set_mode_standard();
    rdpq_set_prim_color(RGBA32(0,0,0, 128));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_fill_rectangle(0, 0, 320, 240);

    // Additive noise
    rdpq_set_prim_color(RGBA32(255,255,255, 128));
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);

    rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,0),       (0,0,0,PRIM)));
    */

}

void run_animation()
{
    // 0 - 8  verse
    // 8 - 16 verse with synth
    // 23 - 40 chorus, 31 midpoint
    // 40 - 50 end verse
    
    viewer.active_camera = CAM_CLOSEUP;


    if (time_secs > 8) {
        viewer.active_camera = CAM_OVERVIEW;
    }
    if (time_secs > 16) {
        viewer.active_camera = CAM_CLOSEUP;
    }
    if (time_secs > 23) {
        viewer.active_camera = CAM_ACTION;
    }
    if (time_secs > 31) {
        viewer.active_camera = ((int)(time_secs / 3) % NUM_CAMERAS);
    }

    float drop_start = 40.0f;
    if (time_secs > drop_start) {
        for (int i=0;i<NUM_SIMULATIONS;i++) {
            if (time_secs > drop_start + i) {
            sims[i].config.root_is_static = false;
            }
        }
    }
}

void render()
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    const float fudge = 0.01f;
    glClearColor(environment_color[0] + fudge, environment_color[1] + fudge, environment_color[2] + fudge, environment_color[3] + fudge);
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
        float target_xshake = 1.0f + 0.025f * cos(time_secs*0.3f);
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
    
    PROFILE_START(PS_RENDER_BG, 0);
    if (tweak_dont_test_plane_depth) {
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
    }
    render_plane();
    if (tweak_dont_test_plane_depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }
    PROFILE_STOP(PS_RENDER_BG, 0);

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);
    // render_sphere(rotation);

    PROFILE_START(PS_RENDER_SIM, 0);
    for (int i=0;i<NUM_SIMULATIONS;i++) {
        sim_render(&sims[i], sim_object_to_worlds[i]);
    }
    PROFILE_STOP(PS_RENDER_SIM, 0);

    //set_diffuse_material();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    PROFILE_START(PS_RENDER_SHADOWS, 0);
    for (int i=0;i<NUM_SIMULATIONS;i++) {
        render_shadows(sim_object_to_worlds[i]);
    }
    PROFILE_STOP(PS_RENDER_SHADOWS, 0);

    PROFILE_START(PS_RENDER_FLARE, 0);
    render_flare();
    PROFILE_STOP(PS_RENDER_FLARE, 0);

    gl_context_end();

    rdpq_detach();
    PROFILE_START(PS_RENDER_POSTPROC, 0);
    if (false) apply_postproc(disp);
    PROFILE_STOP(PS_RENDER_POSTPROC, 0);
    rdpq_attach(disp, NULL);
    rdpq_detach_show(); // FIXME does attach + detach without calls make sense?
}

static uint32_t audio_sample_time = 0;

void audio_poll(void) {	
	if (audio_can_write()) {    	
		short *buf = audio_write_begin();
        int bufsize = audio_get_buffer_length();
		mixer_poll(buf, bufsize);
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

    profile_init();

    demo.interactive = false;

    // glEnable(GL_MULTISAMPLE_ARB);

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    setup();

    controller_init();
    timer_init();

	audio_init(44100, 4);
	mixer_init(2);

    static wav64_t music_wav;
    if (music_enabled) {
        const char* songpath = "/music1.wav64"; //TODO need to move to filesystem again
        wav64_open(&music_wav, songpath);
        wav64_play(&music_wav, 0);
    }

#if !DEBUG_RDP
    while (1)
#endif
    {
        time_secs = TIMER_MICROS(timer_ticks()) / 1e6;

        controller_scan();

        struct controller_data pressed = get_keys_pressed();
        struct controller_data down = get_keys_down();

        if (down.c[0].start) {
            demo.interactive = !demo.interactive;
            debugf("%s mode\n", demo.interactive ? "Interactive" : "Playback");
        }

        if (demo.interactive) {
            if (down.c[0].L) {
                viewer.active_camera--;
                if (viewer.active_camera < 0) viewer.active_camera = NUM_CAMERAS - 1;

                debugf("Active camera: %d\n", viewer.active_camera);
            }

            if (down.c[0].R) {
                viewer.active_camera++;
                if (viewer.active_camera >= NUM_CAMERAS) viewer.active_camera = 0;

                debugf("Active camera: %d\n", viewer.active_camera);
            }

            if (down.c[0].A) {
                debugf("show wires\n");
                for (int i = 0; i < NUM_SIMULATIONS; i++) {
                    sims[i].debug.show_wires = !sims[i].debug.show_wires;
                }
            }

            const float nudge = 0.01f;
            bool c_pressed = false;

            if (down.c[0].C_up) {
                debug_z_shift += nudge;
                c_pressed = true;
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

        PROFILE_START(PS_MUSIC, 0);
        if (music_enabled) {
            audio_poll();
        }
        PROFILE_STOP(PS_MUSIC, 0);

        PROFILE_START(PS_UPDATE, 0);
        if (true || time_frames < 40) {
            for (int i = 0; i < NUM_SIMULATIONS; i++) {
                sim_update(&sims[i]);
            }
        }
        PROFILE_STOP(PS_UPDATE, 0);

        if (!demo.interactive) {
            run_animation();
        }

        PROFILE_START(PS_RENDER, 0);
        render();
        PROFILE_STOP(PS_RENDER, 0);
        if (DEBUG_RDP)
            rspq_wait();
        
        profile_next_frame();
        time_frames++;

        #if LIBDRAGON_PROFILE
        if (time_frames == 128) {
            profile_dump();
            profile_init();
        }
        #endif
    }

}
