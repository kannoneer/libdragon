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

#define RAND_MAX (0xffffffffU)

static uint32_t rand_state = 1;
static uint32_t rand(void) {
	uint32_t x = rand_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return rand_state = x;
}

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

static const bool music_enabled = false;

static uint32_t animation = 3283;
static uint32_t texture_index = 0;
static camera_t camera;
static surface_t zbuffer;

static float time_secs = 0.0f;

#define NUM_TEXTURES (7)
#define TEX_CEILING (4)
#define TEX_FLARE (5)
#define TEX_ICON (6)

static GLuint textures[NUM_TEXTURES];

static GLenum shade_model = GL_SMOOTH;

static model64_t* model_gemstone = NULL;

static const GLfloat environment_color[] = { 0.1f, 0.03f, 0.2f, 1.f };

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

static const char *texture_path[NUM_TEXTURES] = {
    "rom:/circle0.rgba16.sprite",
    "rom:/diamond0.rgba16.sprite",
    "rom:/pentagon0.rgba16.sprite",
    "rom:/triangle0.rgba16.sprite",
    "rom:/ceiling2.ci4.sprite",
    "rom:/star.ia8.sprite",
    "rom:/icon.rgba16.sprite",
};

static sprite_t *sprites[NUM_TEXTURES];

void set_diffuse_material()
{
    GLfloat mat_diffuse[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);
}

void set_gemstone_material()
{
    GLfloat color[] = { 2.0f, 2.0f, 2.0f, 0.75f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, color);
}

#define MESH_MAX_VERTICES (36)
#define MESH_MAX_INDICES (60)

struct shadow_mesh {
    float verts[MESH_MAX_VERTICES][3];
    float verts_proj[MESH_MAX_VERTICES][3];
    uint16_t indices[MESH_MAX_INDICES];
    int num_vertices;
    int num_indices;

    mat4_t object_to_world;
};

static struct shadow_mesh smesh_gemstone;


static void shadow_mesh_init(struct shadow_mesh* mesh)
{
    memset(mesh, 0, sizeof(struct shadow_mesh));
}

static bool shadow_mesh_extract(struct shadow_mesh* mesh, model64_t* model)
{
    bool verbose = false;
    shadow_mesh_init(mesh);

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

    return true;
}

void shadow_mesh_draw(struct shadow_mesh* mesh) {
    if (mesh->num_indices == 0) {
        debugf("Warning: shadow mesh had an empty index buffer\n");
        return;
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);

    glVertexPointer(3, GL_FLOAT, sizeof(float) * 3, &mesh->verts_proj[0]);
    glDrawElements(GL_TRIANGLES, mesh->num_indices, GL_UNSIGNED_SHORT, &mesh->indices[0]);

    glDisableClientState(GL_VERTEX_ARRAY);
}

void setup()
{
    camera.distance = -10.0f;
    camera.rotation = 0.0f;

    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());

    for (uint32_t i = 0; i < NUM_TEXTURES; i++)
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
    glFogf(GL_FOG_END, 20);
    glFogfv(GL_FOG_COLOR, environment_color);

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

    bool spring_visible[MAX_SPRINGS];

    int num_springs;
    float root[3];
    int num_updates_done;

    struct {
        int u_inds[2]; // from-to points that make up the u vector
        int v_inds[2];
        int attach_top_index; // where the gem is attached
        int attach_tri_inds[3]; // gem centroid points

        float u[3];
        float v[3];
        float n[3];

        float origin[3];
        float ulength;
    } pose;

    struct {
        bool show_wires;
    } debug;
} sim;

void sim_init()
{
    memset(&sim, 0, sizeof(sim));

    memcpy(sim.oldx, sim.x, sizeof(sim.x));

    for (int i=0;i<MAX_SPRINGS;i++) {
        sim.spring_visible[i] = false;
    }

    const float rope_segment = 0.5f;
    const float side = 2.0f;

    int h=-1;
    for (int i=1;i<4;i++) {
        sim.spring_visible[sim.num_springs] = true;
        sim.springs[sim.num_springs++] = (struct Spring){i-1, i, rope_segment};
        h = i;
    }

    sim.springs[sim.num_springs++] = (struct Spring){h, h+1, side};
    sim.springs[sim.num_springs++] = (struct Spring){h, h+2, side};
    sim.springs[sim.num_springs++] = (struct Spring){h, h+3, side};
    sim.springs[sim.num_springs++] = (struct Spring){h+1, h+2, side};
    sim.springs[sim.num_springs++] = (struct Spring){h+2, h+3, side};
    sim.springs[sim.num_springs++] = (struct Spring){h+3, h+1, side};

    sim.springs[sim.num_springs++] = (struct Spring){h+1, h+4, side};
    sim.springs[sim.num_springs++] = (struct Spring){h+2, h+4, side};
    sim.springs[sim.num_springs++] = (struct Spring){h+3, h+4, side};


    sim.pose.u_inds[0] = h+1;
    sim.pose.u_inds[1] = h+2;
    sim.pose.v_inds[0] = h+1;
    sim.pose.v_inds[1] = h+3;

    sim.pose.attach_top_index = h;
    sim.pose.attach_tri_inds[0] = h+1;
    sim.pose.attach_tri_inds[1] = h+2;
    sim.pose.attach_tri_inds[2] = h+3;

    sim.num_points = h+5;

    for (int i=0;i<sim.num_points-1;i++) {
        int idx = i*3;
        sim.x[idx + 0] = i*0.5f;
        sim.x[idx + 1] = i*0.1f;
        sim.x[idx + 2] = i*0.15f;
    }

    sim.root[0] = 0.0f;
    sim.root[1] = 8.0f;
    sim.root[2] = 0.0f;

    sim.debug.show_wires = false;
}

void sim_update()
{
    const int num_iters = 3;
    const float gravity = -0.1f;

    // Verlet integration

    sim.x[0] = sim.root[0];
    sim.x[1] = sim.root[1];
    sim.x[2] = sim.root[2];

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

    sim.x[0] = sim.root[0];
    sim.x[1] = sim.root[1];
    sim.x[2] = sim.root[2];

    // Update object attachment

    // Compute the average position of the "attachment triangle" set in simulation pose struct.
    const float* a1 = &sim.x[3 * sim.pose.attach_tri_inds[0]];
    const float* a2 = &sim.x[3 * sim.pose.attach_tri_inds[1]];
    const float* a3 = &sim.x[3 * sim.pose.attach_tri_inds[2]];

    float centroid[3];
    vec3_copy(a1, centroid);
    vec3_add(a1, a2, centroid);
    vec3_add(centroid, a3, centroid);
    vec3_scale(0.33333f, centroid);

    sim.pose.origin[0] = centroid[0];
    sim.pose.origin[1] = centroid[1];
    sim.pose.origin[2] = centroid[2];

    const float* ufrom   = &sim.x[3 * sim.pose.u_inds[0]];
    const float* uto     = &sim.x[3 * sim.pose.u_inds[1]];
    //const float* vfrom   = &sim.x[3 * sim.pose.v_inds[0]];
    //const float* vto     = &sim.x[3 * sim.pose.v_inds[1]];
    const float* nfrom   = centroid;
    const float* nto     = &sim.x[3 * sim.pose.attach_top_index];

    float* uvec = sim.pose.u;
    float* vvec = sim.pose.v;
    float* nvec = sim.pose.n;

    vec3_sub(uto, ufrom, uvec);
    //vec3_sub(vto, vfrom, vvec);
    vec3_sub(nto, nfrom, nvec);

    sim.pose.ulength = vec3_length(uvec);

    vec3_normalize_(uvec);
    vec3_normalize_(nvec);

    //vec3_normalize_(vvec);
     vec3_cross(uvec, nvec, vvec);

    // Debug movement

    sim.num_updates_done++;

    if (sim.num_updates_done % 75 == 0) {
        sim.root[0] = ((float)rand()/RAND_MAX) * 6.0f - 3.0f;
        debugf("root[0] = %f\n", sim.root[0]);
    }

    for (int i=0;i<sim.num_points;i++) {
        // float* pos = &sim.x[i*3];
        // debugf("[%d] (%f, %f, %f)\n", i,
        //     pos[0], pos[1], pos[2]
        // );
    }
}

static float debug_z_shift = 0.0f;
static float debug_x_shift = 0.0f;

void sim_render()
{
    glDisable(GL_TEXTURE_2D);
    if (sim.debug.show_wires) {
        glDisable(GL_LIGHTING);
    } else {
        glEnable(GL_LIGHTING);
    }

    glPushMatrix();
    //glTranslatef(0, 8, 0);
    glBegin(GL_LINES);
    for (int i=0;i<sim.num_springs;i++) {
        struct Spring spring = sim.springs[i];
        if (sim.spring_visible[i] || sim.debug.show_wires) {
            float* pa = &sim.x[spring.from*3];
            float* pb = &sim.x[spring.to*3];
            glVertex3f(pa[0], pa[1], pa[2]);
            glVertex3f(pb[0], pb[1], pb[2]);
            //glColor3f(i % 2, (i % 3)/2.0f, (i%4)/4.0f);
            glColor3f(1.0f, 1.0f, 1.0f);
        }
    }
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    mat4_t basis;
    mat4_set_identity(basis);

    vec3_copy(sim.pose.u, &basis[2][0]);
    vec3_copy(sim.pose.n, &basis[1][0]);
    vec3_cross(sim.pose.u, sim.pose.n, &basis[0][0]);

    // translate
    vec3_copy(sim.pose.origin, &basis[3][0]);

    mat4_t shift;
    mat4_make_translation(debug_x_shift, -0.28f, 0.0f, shift);
    mat4_mul(basis, shift, basis);


    mat4_ucopy(basis, smesh_gemstone.object_to_world);

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

        debugf("u: (%f, %f, %f)\n", sim.pose.u[0], sim.pose.u[1], sim.pose.u[2]);
        debugf("v: (%f, %f, %f)\n", sim.pose.v[0], sim.pose.v[1], sim.pose.v[2]);
        debugf("n: (%f, %f, %f)\n", sim.pose.n[0], sim.pose.n[1], sim.pose.n[2]);

        debugf("basis:\n");
        print_mat4(basis);
    }

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
    model64_draw(model_gemstone);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_GEN_T);
    glDisable(GL_BLEND);
    
    glPopMatrix();
    glPopMatrix();
}

void render_flare()
{
    glPushMatrix();

    glTranslatef(light_pos[0][0], light_pos[0][1], light_pos[0][2]);

    float to_cam[3];
    vec3_sub(camera.computed_eye, light_pos[0],to_cam);
    float dist = vec3_length(to_cam);
    
    set_diffuse_material();
    glDisable(GL_LIGHTING);

    glBindTexture(GL_TEXTURE_2D, textures[TEX_FLARE]);
    glEnable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glDepthMask(GL_FALSE);
    
    //glEnable(GL_BLEND);

    glPointSize(400.0f / (dist+1.0f));
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

void render_shadows()
{
    glPushMatrix();
    glTranslatef(0.0f, 4.0f, 0.0f);
    set_diffuse_material();
    struct shadow_mesh* mesh = &smesh_gemstone;

    // transform light position to object space
    //      - will need inverse of the model matrix of the gemstone model
    // go over each vertex and compute light-to-vertex direction
    // trace a ray from vertex towards the floor plane (also transformed to object space)
    // if hit, set vertex to hit position
    // if missed, set vertex 1000 units away (TODO)
    // 
    memcpy(&mesh->verts_proj[0], &mesh->verts[0], sizeof(float) * 3 * mesh->num_vertices);

    mat4_t world_to_object;
    mat4_invert_rigid_xform(mesh->object_to_world, world_to_object);
    float light_world[4] = {light_pos[0][0], light_pos[0][1], light_pos[0][2], 1.0f};
    float light_obj[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    mat4_mul_vec4(world_to_object, light_world, light_obj);

    debugf("world_to_object: ");
    print_mat4(world_to_object);
    debugf("light_world: ");
    print_vec4(light_world);
    debugf("light_obj: ");
    print_vec4(light_obj);

    // PROBLEM: raytrace is done in object space, ground plane is world space?
    //  - transform every vertex to world space?
    //  - transform plane to object space?
    //      - TODO how to raytrace any plane and ray?
    // TODO transform shadow mesh when rendering
    float plane_normal[3] = {0.f, -1.0f, 0.0f};
    float plane_origin[3] = {0.0f, -2.0f, 0.0f};

    for (int i=0; i<mesh->num_vertices; i++) {
        float* vert = &mesh->verts[i][0];
        float light_to_vert[3];
        vec3_sub(vert, light_obj, light_to_vert);
        vec3_normalize_(light_to_vert);

        debugf("light_to_vert: ");
        print_vec3(light_to_vert);

        float t=0.0f;
        // TODO light_to_vert.y < 0 so denom < 0?
        bool hit = intersect_plane(plane_normal, plane_origin, light_obj, light_to_vert, &t);

        debugf("[%d], hit=%s, t=%f\n", i, hit ? "true" : "false", t);

        if (!hit) {
            t = 1000.0f;
        }

        // float dy = light_to_vert[1];
        // float t;
        // if (dy > -0.01f) {
        //     t = 1000.0f; // HACK avoid divide by zero and sky
        // } else {
        //     t = -vert[1] / dy;
        // }

        // debugf("[%d] dy=%f, t=%f\n", i, dy, t);

        // light_to_vert will be a ray offset from vertex to floor
        float result[3]; // projected position. TODO could reuse light_to_vert here?
        vec3_copy(vert, result);
        vec3_scale(t, light_to_vert);
        vec3_add(result, light_to_vert, result);
        // add floor offset to vertex position. we copied positions with a memcpy already above
        vec3_add(&mesh->verts_proj[i][0], result, &mesh->verts_proj[i][0]);

    }

    //TODO apply this world_to_object to light
    //TODO compute light to vertex for each vert

    shadow_mesh_draw(&smesh_gemstone);
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

    camera.computed_eye[0] = cos(camera.rotation) * camera.distance;
    camera.computed_eye[1] = 6.0f;
    camera.computed_eye[2] = -sin(camera.rotation) * camera.distance;
    
    glLoadIdentity();
    static float cam_target[3];
    float cam_target_blend = 0.1f;
    float inv_cam_target_blend = 1.0f - cam_target_blend;

    cam_target[0] = inv_cam_target_blend * cam_target[0] + cam_target_blend * sim.pose.origin[0];
    cam_target[1] = inv_cam_target_blend * cam_target[1] + cam_target_blend * sim.pose.origin[1];
    cam_target[2] = inv_cam_target_blend * cam_target[2] + cam_target_blend * sim.pose.origin[2];

    gluLookAt(
        camera.computed_eye[0], camera.computed_eye[1], camera.computed_eye[2],
        cam_target[0], cam_target[1], cam_target[2],
        0, 1, 0);

    // camera_transform(&camera);

    float rotation = animation * 0.5f;

    float dist = 4.0f;
    float langle = time_secs * 0.4f;

    light_pos[0][0] = dist * cos(langle);
    light_pos[0][1] = 8.0f + sin(time_secs*0.8f);
    light_pos[0][2] = dist * sin(langle);

    glLightfv(GL_LIGHT0, GL_POSITION, light_pos[0]);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);
    
    // render_plane();

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1)%4]);
    render_sphere(rotation);

    sim_update();
    sim_render();

    set_diffuse_material();

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // render_primitives(rotation);

    render_shadows();

    render_flare();

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
            debugf("show wires\n");
            sim.debug.show_wires = !sim.debug.show_wires;
        }

        const float nudge=0.01f;
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
        float mag = x*x + y*y;

        if (fabsf(mag) > 0.01f) {
            camera.distance += y * 0.2f;
            camera.rotation = camera.rotation - x * 0.05f;
        }

        if (music_enabled) {
            audio_poll();
        }

        render();
        if (DEBUG_RDP)
            rspq_wait();
    }

}
