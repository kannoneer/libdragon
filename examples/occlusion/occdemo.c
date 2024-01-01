#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <GL/glu.h>
#include <libdragon.h>
#include <malloc.h>
#include <math.h>
#include <rspq_profile.h>

#include <model64.h>
#include "../../src/model64_internal.h"
#include "lib/microuiN64.h"

#include "camera.h"
#include "cube.h"
#include "decal.h"
#include "plane.h"
#include "prim_test.h"
#include "skinned.h"
#include "sphere.h"

#include "occult.h"
#include "bvh.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

#define HIRES_MODE 1
#if HIRES_MODE
#define SCREEN_WIDTH (640)
#define SCREEN_HEIGHT (480)
#define CULL_W (SCREEN_WIDTH / 16)
#define CULL_H (SCREEN_HEIGHT / 16)
#else
#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)
#define CULL_W (SCREEN_WIDTH / 8)
#define CULL_H (SCREEN_HEIGHT / 8)
#endif

#define GET_ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

static occ_culler_t *culler;
static occ_hull_t cube_hull;
static occ_hull_t unit_cube_hull;

enum camera_mode_enum {
    CAM_SPIN = 0,
    CAM_FPS = 1,
};

static uint32_t animation = 0;
static uint32_t texture_index = 0;
static camera_t camera;
static fps_camera_t fps_camera = {.pos={-1.147780f, 0.50000f, -8.575987f}, .angle=-3.231272f, .pitch=0.029522f};

int g_camera_mode = CAM_SPIN;
matrix_t g_view;
static surface_t zbuffer;
static surface_t sw_zbuffer_array[2];
static surface_t *sw_zbuffer;

static matrix_t g_projection;

static uint64_t g_num_frames = 0;
int g_show_node = -2;

static GLuint textures[4];

#define ENV_COLOR_LITERAL {0.85f, 0.85f, 1.0f, 1.f}
static const GLfloat environment_color[] = ENV_COLOR_LITERAL;

static bool config_menu_open = false;

static bool config_enable_culling = true;
static bool config_show_wireframe = false;
static int config_depth_view_mode = 1;
static bool config_top_down_view = false;
static float config_far_plane = 50.f;
static int config_show_bvh_boxes = 0;
static int config_show_last_box = 0;
static int config_cull_occluders = 1;
static int config_enable_fog_probes = 1;
static int config_test_occluders_after = 3;
static int config_max_occluders = 15;

static const GLfloat light_diffuse[8][4] = {
    {1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 0.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
};

typedef struct fog_probe_s {
    const char* name;
    float pos[3];
    float color[4];
} fog_probe_t;

static fog_probe_t fog_probes[] = {
    (fog_probe_t){"environment", {}, ENV_COLOR_LITERAL},
    (fog_probe_t){"probe torii", {}, {0.7f, 0.8f, 0.9f, 1.0f}},
    (fog_probe_t){"probe central", {}, {0.95f, 0.95f, 0.95f, 1.0f}},
    (fog_probe_t){"probe hallway", {}, {0.6f, 0.3f, 0.3f, 1.0f}},
    };

enum Fonts {
    FONT_SCIFI = 1
};

static const char *texture_path[4] = {
    "rom:/rock_tile.sprite",
    "rom:/diamond0.sprite",
    "rom:/pentagon0.sprite",
    "rom:/triangle0.sprite",
};

static sprite_t *sprites[4];

void wait_for_button() {
    debugf("Press A or B or Start to continue\n");
    while (true) {
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (pressed.a || pressed.b || pressed.start) break;
    }
}

void compute_camera_matrix(matrix_t *matrix, const camera_t *camera)
{
    matrix_t lookat;
    cpu_gluLookAt(&lookat,
                  0, 0.5f * -camera->distance, -camera->distance,
                  0, 4, 0,
                  0, 1, 0);
    matrix_t rotate = cpu_glRotatef(camera->rotation, 0, 1, 0);
    matrix_mult_full(matrix, &lookat, &rotate);
}

void compute_fps_camera_matrix(matrix_t *matrix, const fps_camera_t *camera)
{
    cpu_gluLookAt(matrix,
        camera->pos[0], camera->pos[1], camera->pos[2],
        camera->pos[0] + cos(camera->angle),
        camera->pos[1] + sin(camera->pitch),
        camera->pos[2] + sin(camera->angle) * cos(camera->pitch), //camera->pos[2] + sin(camera->angle),
        0, 1, 0);
}

void compute_top_down_camera_matrix(matrix_t *matrix, const fps_camera_t *camera)
{
    float h = config_far_plane * 0.8;
    cpu_gluLookAt(matrix,
        camera->pos[0], camera->pos[1] + h, camera->pos[2],
        camera->pos[0] + cos(camera->angle), camera->pos[1], camera->pos[2] + sin(camera->angle),
        0, 1, 0);
}


void setup()
{
    camera.distance=-10.0; camera.rotation=0.f;

    zbuffer = surface_alloc(FMT_RGBA16, display_get_width(), display_get_height());
    sw_zbuffer_array[0] = surface_alloc(FMT_RGBA16, CULL_W, CULL_H);
    sw_zbuffer_array[1] = surface_alloc(FMT_RGBA16, CULL_W, CULL_H);
    sw_zbuffer = &sw_zbuffer_array[0];

    for (uint32_t i = 0; i < 4; i++) {
        sprites[i] = sprite_load(texture_path[i]);
    }

    setup_sphere();
    make_sphere_mesh();

    setup_cube();

    setup_plane();
    make_plane_mesh();

    float aspect_ratio = (float)display_get_width() / (float)display_get_height();
    float near_plane = 1.0f;
    float far_plane = config_far_plane;

    glMatrixMode(GL_PROJECTION);
    computeProjectionMatrix(&g_projection, 80.f, aspect_ratio, near_plane, far_plane);
    glLoadIdentity();
    glMultMatrixf(&g_projection.m[0][0]);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, environment_color);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    float light_radius = 10.0f;

    for (uint32_t i = 0; i < 0; i++) {
        glEnable(GL_LIGHT0 + i);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, light_diffuse[i]);
        glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 2.0f / light_radius);
        glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 1.0f / (light_radius * light_radius));
    }

    GLfloat mat_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat_diffuse);

    glFogf(GL_FOG_START, far_plane); // inverted fog range for RDPQ_FOG_STANDARD
    glFogf(GL_FOG_END, 4.0f);
    glFogfv(GL_FOG_COLOR, environment_color);

    glEnable(GL_MULTISAMPLE_ARB);

    glGenTextures(4, textures);

#if 0
    GLenum min_filter = GL_LINEAR_MIPMAP_LINEAR;
#else
    GLenum min_filter = GL_LINEAR;
#endif

    for (uint32_t i = 0; i < 4; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], &(rdpq_texparms_t){.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE});
    }

    rdpq_text_register_font(FONT_SCIFI, rdpq_font_load("rom:/FerriteCoreDX.font64"));

    // Convert cube mesh to a 'hull' that's optimized for culling
    occ_mesh_t cube_mesh = {
        .vertices = (cvertex_t*)cube_cvertices,
        .indices = (uint16_t*)cube_indices,
        .num_vertices = GET_ARRAY_SIZE(cube_vertices),
        .num_indices = GET_ARRAY_SIZE(cube_indices),
    };

    bool conversion_success = occ_hull_from_flat_mesh(&cube_mesh, &cube_hull);
    if (!conversion_success) {
        assert(false && "Couldn't convert cube mesh!");
    }

    occ_mesh_t unit_cube_mesh = {
        .vertices = (cvertex_t*)unit_cube_cvertices,
        .indices = (uint16_t*)unit_cube_indices,
        .num_vertices = GET_ARRAY_SIZE(unit_cube_vertices),
        .num_indices = GET_ARRAY_SIZE(unit_cube_indices),
    };
    conversion_success = occ_hull_from_flat_mesh(&unit_cube_mesh, &unit_cube_hull);
    if (!conversion_success) {
        assert(false && "Couldn't convert unit cube mesh!");
    }
}

static void copy_to_matrix(const float* in, matrix_t* out) {
    // matrix_t is in column-major order with m[col][row]

    for (int i = 0; i < 16; ++i) {
        out->m[i / 4][i % 4] = in[i];
    }
}

#if 0
static void matrix_to_memory(const matrix_t* in, float* out) {
    // matrix_t is in column-major order with m[col][row]

    for (int i = 0; i < 16; ++i) {
        out[i] = in->m[i / 4][i % 4];
    }
}
#endif

#define SCENE_MAX_OCCLUDERS (30)
#define SCENE_MAX_NODES (50)
#define SCENE_MAX_BVH_SIZE (200)

struct scene_s
{
    occ_target_t targets[SCENE_MAX_NODES];
    struct {
        int num_drawn;
    } stats;

    model64_t *mdl_room;
    uint32_t num_nodes;
    uint32_t num_occluders;
    model64_node_t* nodes[SCENE_MAX_NODES];
    GLuint node_dplists[SCENE_MAX_NODES];
    matrix_t node_xforms[SCENE_MAX_NODES];
    matrix_t node_world_xforms[SCENE_MAX_NODES];
    bool node_should_test[SCENE_MAX_NODES];
    // bool node_has_xform[SCENE_MAX_NODES]; // node unit cubes need to be transformed anyway so we can't skip them
    const char* node_names[SCENE_MAX_NODES];

    model64_node_t* occluders[SCENE_MAX_OCCLUDERS];
    occ_mesh_t occ_meshes[SCENE_MAX_OCCLUDERS];
    occ_hull_t occ_hulls[SCENE_MAX_OCCLUDERS];
    matrix_t occluder_xforms[SCENE_MAX_OCCLUDERS];
    bool occluder_has_xform[SCENE_MAX_OCCLUDERS]; // true if xform is not identity
    float occ_origins[SCENE_MAX_OCCLUDERS*3];
    float occ_radiuses[SCENE_MAX_OCCLUDERS];
    aabb_t occ_aabbs[SCENE_MAX_OCCLUDERS];
    sphere_bvh_t occluder_bvh;

    sphere_bvh_t bvh;
    matrix_t bvh_xforms[SCENE_MAX_BVH_SIZE];

} scene = {};

struct {
    int num_drawn;
    int num_max;
    int num_occluders_drawn;
} scene_stats;

occ_raster_query_result_t last_visible_result = {};

void render_node(model64_t* mdl, model64_node_t* node)
{
    GLfloat mat_diffuse[] = 
    {
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
    };
    (void)mat_diffuse;

    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_RDPQ_MATERIAL_N64);

    rdpq_set_mode_standard();
    uint8_t env = 0;
    uint8_t prim = 255;
    rdpq_set_env_color((color_t){env, env, env, 255});
    rdpq_set_prim_color((color_t){prim, prim, prim, 255});
    rdpq_mode_combiner(RDPQ_COMBINER2(
        (TEX0, 0, SHADE, ENV), (0, 0, 0, 1),
        (COMBINED, ENV, PRIM, COMBINED), (0, 0, 0, COMBINED)));
    rdpq_mode_fog(RDPQ_FOG_STANDARD);

    model64_draw_node(mdl, node);
    glDisable(GL_RDPQ_MATERIAL_N64);
}

bool extract_node_bounds(const model64_node_t* node, const matrix_t* world_xform,
    float* out_origin, float* out_radius, aabb_t* out_aabb, matrix_t* out_node_xform)
{
        float obj_radius = 0.0f;
        float world_radius = 0.0f;

        aabb_t obj_aabb={};
        aabb_t world_aabb={};
        float world_center[3]={};
        bool bounds_ok = compute_mesh_bounds(node->mesh, world_xform, &obj_radius, &obj_aabb, &world_radius, &world_aabb, &world_center[0]);
        debugf("[node %p] OK: %d, obj_radius=%f, min=(%.3f, %.3f, %.3f), max=(%.3f, %.3f, %.3f)\n", node, bounds_ok, obj_radius,
            obj_aabb.lo[0], obj_aabb.lo[1], obj_aabb.lo[2],
            obj_aabb.hi[0], obj_aabb.hi[1], obj_aabb.hi[2]
        );
        debugf("[node %p] OK: %d, world_radius=%f, min=(%.3f, %.3f, %.3f), max=(%.3f, %.3f, %.3f), center=(%f, %f, %f)\n", node, bounds_ok, world_radius,
            world_aabb.lo[0], world_aabb.lo[1], world_aabb.lo[2],
            world_aabb.hi[0], world_aabb.hi[1], world_aabb.hi[2],
            world_center[0], world_center[1],world_center[2]
        );

        const float* minp = &obj_aabb.lo[0];
        const float* maxp = &obj_aabb.hi[0];
        // unit cube is [-1, 1]^3 so we need to scale only by 1/2 of the AABB axes to get that size
        float scale[3] = {0.5f * (maxp[0] - minp[0]), 0.5f * (maxp[1] - minp[1]), 0.5f * (maxp[2] - minp[2])};
        // compute midpoint
        float mid[3] = {0.5f * (maxp[0] + minp[0]), 0.5f * (maxp[1] + minp[1]), 0.5f * (maxp[2] + minp[2])};
        debugf("[node %p] scale: (%.3f, %.3f, %.3f), mid: (%.3f, %.3f, %.3f)\n",
            node,
            scale[0], scale[1], scale[2],
            mid[0], mid[1], mid[2]
        );

        // debugf("matrix %lu in\n", i);
        // print_matrix(&scene.node_xforms[i]);
        // float* orig = &origins[3*i];
        out_origin[0] = world_center[0];
        out_origin[1] = world_center[1];
        out_origin[2] = world_center[2];
        *out_radius = world_radius;
        *out_aabb = world_aabb;

        //matrix_t old = scene.node_world_xforms[i];

        matrix_t centerize = cpu_glTranslatef(mid[0], mid[1], mid[2]);
        matrix_t scale_obb = cpu_glScalef(scale[0], scale[1], scale[2]);
        matrix_t temp;
        matrix_mult_full(&temp, &centerize, &scale_obb);
        matrix_mult_full(out_node_xform, world_xform, &temp);

    return true;
}

bool matrix_is_identity(const matrix_t* mtx)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (mtx->m[i][j] != ((i == j) ? 1.0f : 0.0f)) return false;
        }
    }
    return true;
}

void setup_scene()
{
    //bool verbose=false;
    struct scene_s* s = &scene;

    model64_t* model = model64_load("rom://room4.model64");
    if (!model) {
        debugf("Couldn't load model!\n");
        return;
    }
    assert(model);
    s->mdl_room = model;
    uint32_t mesh_count = model64_get_mesh_count(model);
    uint32_t node_count = model64_get_node_count(model);

    debugf("Mesh Count: %lu, Node Count: %lu\n", mesh_count, node_count);

    for (uint32_t i=0;i<node_count;i++){ 
        model64_node_t* node = model64_get_node(model, i);

        if (node->name && strstr(node->name, "probe")) {
            float* p = node->transform.pos;
            bool found=false;
            for (int probe_i = 0; probe_i < GET_ARRAY_SIZE(fog_probes); probe_i++) {
                if (strcmp(node->name, fog_probes[probe_i].name) == 0) {
                    fog_probes[probe_i].pos[0] = p[0];
                    fog_probes[probe_i].pos[1] = p[1];
                    fog_probes[probe_i].pos[2] = p[2];
                    debugf("Found '%s' at (%f, %f, %f)\n", node->name, p[0], p[1], p[2]);
                    found = true;
                    break;
                }
            }
            if (!found) {
                debugf("No entry found for '%s' (%f, %f, %f)\n", node->name, p[0], p[1], p[2]);
            }
        }

        if (node->name == NULL || node->mesh == NULL) {
            debugf("Skipping node %lu name=%s, mesh=%p\n", i, node->name, node->mesh);
            continue;
        }

        if (strstr(node->name, "cell") != NULL) {
            debugf("skipping cell '%s'\n", node->name);
            continue;
        }

        debugf("%lu node=%p, name=%s\n", i, node, node->name);

        if (strstr(node->name, "occluder") != NULL) {
            debugf("occluder found\n");
            if (s->num_occluders >= SCENE_MAX_OCCLUDERS) {
                debugf("Error: max occluders reached\n");
                break;
            }
            uint32_t idx = s->num_occluders;
            copy_to_matrix(&model->transforms[i].world_mtx[0], &scene.occluder_xforms[idx]);
            scene.occluder_has_xform[idx] = !matrix_is_identity(&scene.occluder_xforms[idx]);
            s->occluders[idx] = node;
            s->num_occluders++;
        } else {
            if (s->num_nodes >= SCENE_MAX_NODES) {
                debugf("Error: max nodes reached\n");
                break;
            }
            uint32_t idx = s->num_nodes;

            if (strstr(node->name, "occluder") == NULL) {
                scene.node_should_test[idx] = true;
            }

            copy_to_matrix(&model->transforms[i].world_mtx[0], &scene.node_world_xforms[idx]);
            // scene.node_has_xform[idx] = !matrix_is_identity(&scene.node_world_xforms[idx]);
            s->nodes[idx] = node;
            scene.node_names[idx] = node->name;
            s->num_nodes++;
        }
    }

    uint64_t start_ticks = get_ticks();

    float* origins = malloc(scene.num_nodes * sizeof(float) * 3);
    float* radiuses = malloc(scene.num_nodes * sizeof(float));
    aabb_t* aabbs = malloc(scene.num_nodes * sizeof(aabb_t));
    DEFER(free(origins));
    DEFER(free(radiuses));
    DEFER(free(aabbs));

    for (uint32_t i = 0; i < scene.num_nodes; i++) {
        GLuint list = glGenLists(1);
        glNewList(list, GL_COMPILE);
        model64_node_t* node = scene.nodes[i];
        render_node(scene.mdl_room, node);
        glEndList();
        scene.node_dplists[i] = list;

        bool success = extract_node_bounds(node, &scene.node_world_xforms[i], &origins[3*i], &radiuses[i], &aabbs[i], &scene.node_xforms[i]);
        assert(success);
    }

    debugf("num_nodes: %lu, num_occluders: %lu\n", s->num_nodes, s->num_occluders);

    for (uint32_t i=0;i<scene.num_occluders;i++) {
        bool success = model_to_occ_mesh(model, s->occluders[i]->mesh, &scene.occ_meshes[i]);
        if (!success) {
            debugf("conversion of occluder %lu failed\n", i);
            assert(success);
        }
        occ_hull_t* hullp = &scene.occ_hulls[i];
        success = occ_hull_from_flat_mesh(&scene.occ_meshes[i], hullp);
        if (!success) {
            debugf("conversion of hull %lu failed\n", i);
            assert(success);
        }
        matrix_t node_xform;
        success = extract_node_bounds(
            s->occluders[i],
            &scene.occluder_xforms[i],
            &scene.occ_origins[3 * i],
            &scene.occ_radiuses[i],
            &scene.occ_aabbs[i],
            &node_xform);

        debugf("occluder %lu origin=(%f, %f, %f), radius=%f\n", i, 
            scene.occ_origins[3*i+0], scene.occ_origins[3*i+1], scene.occ_origins[3*i+2], scene.occ_radiuses[i]);
    }

    if (!bvh_build(s->occ_origins, s->occ_radiuses, s->occ_aabbs, scene.num_occluders, &scene.occluder_bvh)) {
        debugf("Occluder BVH building failed\n");
        return;
    }

    debugf("input origins and rads:\n");
    for (uint32_t i = 0; i < scene.num_nodes; i++) {
        float r = radiuses[i];
        float* p = &origins[3*i];

        debugf("[%li] (%f, %f, %f), radius=%f\n", i, p[0], p[1], p[2], r);
    }

    uint64_t bvh_start_ticks = get_ticks();

    sphere_bvh_t bvh = {};
    if (!bvh_build(origins, radiuses, aabbs, scene.num_nodes, &bvh)) {
        debugf("Geometry BVH building failed\n");
        return;
    }

    uint64_t stop_ticks = get_ticks();
    debugf("init took %lu ms, bvh build %lu ms\n", TICKS_TO_MS(TICKS_DISTANCE(start_ticks, stop_ticks)), TICKS_TO_MS(TICKS_DISTANCE(bvh_start_ticks, stop_ticks)));
    debugf("bvh.num_nodes=%lu\n", bvh.num_nodes);
    bool ok = bvh_validate(&bvh);
    assert(bvh.num_nodes < SCENE_MAX_BVH_SIZE);
    scene.bvh = bvh;
    assert(ok);

    debugf("creating node matrices\n");
    for (uint32_t i = 0; i < bvh.num_nodes; i++) {
        bvh_node_t* n = &bvh.nodes[i];
        float r = sqrtf(n->radius_sqr);

        debugf("[%li] pos=(%f, %f, %f), r=%f\n", i, n->pos[0], n->pos[1], n->pos[2], r);

        matrix_t scale = cpu_glScalef(r, r, r);
        matrix_t translate = cpu_glTranslatef(n->pos[0], n->pos[1], n->pos[2]);
        matrix_mult_full(&scene.bvh_xforms[i], &translate, &scale);
    }

    // wait_for_button();
}

void render_posed_unit_cube(matrix_t* mtx) {
    glPushMatrix();
    glMultMatrixf(&mtx->m[0][0]);
    draw_unit_cube();
    glPopMatrix();
}

void render_scaled_unit_cube(matrix_t* mtx, float scale) {
    glPushMatrix();
    glScalef(scale, scale, scale);
    glMultMatrixf(&mtx->m[0][0]);
    draw_unit_cube();
    glPopMatrix();
}

void render_aabb(const aabb_t* box) {
    float center[3];
    float size[3];
    aabb_get_center(box, center);
    aabb_get_size(box, size);
    //aabb_print(box);

    glPushMatrix();
    glTranslatef(center[0], center[1], center[2]);
    glScalef(0.5f*size[0], 0.5*size[1], 0.5*size[2]);
    draw_unit_cube();
    glPopMatrix();
}


void render_scene(surface_t* disp)
{
    long unsigned int anim_timer = g_num_frames;
    (void)anim_timer;

    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    // Draw occluders
    scene_stats.num_occluders_drawn = 0;

    if (config_enable_culling) {
        prof_begin(REGION_DRAW_OCCLUDERS);

        if (config_cull_occluders) {
            // debugf("HACK no occluders\n");
            prof_begin(REGION_CULL_OCCLUDERS);
            static cull_result_t occluder_cull_results[SCENE_MAX_OCCLUDERS];
            uint32_t max_occluders = min(GET_ARRAY_SIZE(occluder_cull_results), config_max_occluders);
            uint32_t num_visible = bvh_find_visible(&scene.occluder_bvh, culler->camera_pos, culler->clip_planes, occluder_cull_results, max_occluders);
            prof_end(REGION_CULL_OCCLUDERS);

            // for (uint32_t i = 0; i < scene.num_occluders; i++) {
            for (uint32_t traverse_idx = 0; traverse_idx < num_visible; traverse_idx++) {
                // debugf("testing %lu (%f, %f, %f), rad_sqr=%f\n",
                //         i,
                //        scene.occ_origins[3 * i],
                //        scene.occ_origins[3 * i + 1],
                //        scene.occ_origins[3 * i + 2],
                //        scene.occ_radiuses_sqr[i]);

                // bool visible = true;
                // if (config_cull_occluders) {
                //     uint8_t inflags = 0x00;
                //     float radius = scene.occ_radiuses[i];
                //     plane_side_t side = is_sphere_inside_frustum(culler->clip_planes, &scene.occ_origins[3 * i], radius*radius, &inflags);
                //     visible = side != SIDE_OUT;
                // }

                uint16_t idx = occluder_cull_results[traverse_idx].idx;
                occ_raster_query_result_t result = {};
                occ_occluder_flags_t flags = OCCLUDER_TWO_SIDED;
                if (traverse_idx >= config_test_occluders_after) {
                    flags |= OCCLUDER_TEST_FIRST;
                }
                matrix_t* model_xform_ptr = NULL;
                if (scene.occluder_has_xform[idx]) {
                    model_xform_ptr = &scene.occluder_xforms[idx];
                }
                occ_draw_hull(culler, sw_zbuffer, &scene.occ_hulls[idx], model_xform_ptr, &result, flags);
                // debugf("occluder %lu visible: %d, inflags: 0x%x\n", i, visible, inflags);
                scene_stats.num_occluders_drawn++;
            }
        } else {
            for (uint32_t i = 0; i < scene.num_occluders; i++) {
                occ_raster_query_result_t result = {};
                occ_draw_hull(culler, sw_zbuffer, &scene.occ_hulls[i], &scene.occluder_xforms[i], &result, OCCLUDER_TWO_SIDED);
                scene_stats.num_occluders_drawn++;
            }
        }
        prof_end(REGION_DRAW_OCCLUDERS);
    }

    // Draw occludees (AKA objects, targets)

    GLfloat mat_diffuse[] = 
    {
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
    };

    scene_stats.num_drawn = 0;
    scene_stats.num_max = 0;

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    //glEnable(GL_BLEND);
    //glBlendFunc(GL_ONE, GL_ONE);

    prof_begin(REGION_FRUSTUM_CULL);
    static cull_result_t cull_results[SCENE_MAX_NODES]; // references Node* elements
    uint32_t num_visible = bvh_find_visible(&scene.bvh, culler->camera_pos, culler->clip_planes, cull_results, sizeof(cull_results) / sizeof(cull_results[0]));
    bool actually_visible[SCENE_MAX_NODES] = {};
    prof_end(REGION_FRUSTUM_CULL);

    scene_stats.num_max = scene.num_nodes;

    for (uint32_t traverse_idx = 0; traverse_idx < num_visible; traverse_idx++) {
        cull_result_t res = cull_results[traverse_idx];
        uint16_t idx = res.idx;
        // if (res.idx != 0) continue;
        // debugf("[%u] .idx=%d, .flags=0x%x\n", idx, res.idx, res.flags);

        bool visible = true;

        if (config_enable_culling && !(res.flags & VISIBLE_CAMERA_INSIDE) && scene.node_should_test[idx]) {
            occ_raster_query_result_t result = {};
            // debugf("occlusion testing node '%s'\n", scene.node_names[idx]);
            // occ_draw_hull(culler, sw_zbuffer, &unit_cube_hull, &scene.node_xforms[idx], &result, 0);
            visible = occ_check_target_visible(culler, sw_zbuffer, &unit_cube_hull, &scene.node_xforms[idx], &scene.targets[idx], &result);
            if (visible) last_visible_result = result;
        }

        if (visible) {
            actually_visible[idx] = true;

            glCallList(scene.node_dplists[idx]);

            (void)mat_diffuse;
            scene_stats.num_drawn++;
        }
    }

    if (config_show_wireframe) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        GLfloat white[4] = {0.2f, 0.2f, 0.2f, 0.2f};

        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &white[0]);

        //for (uint32_t i = 0; i < scene.num_nodes; i++) {
        for (uint32_t i = 0; i < SCENE_MAX_NODES; i++) {
        if (!actually_visible[i]) {
            render_posed_unit_cube(&scene.node_xforms[i]);
            }
            // glPushMatrix();
            // glMultMatrixf(&scene.node_xforms[i].m[0][0]);
            // draw_unit_cube();
            // glPopMatrix();
        }

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        GLfloat red[4] = {0.4f, 0.1f, 0.1f, 0.2f};
        GLfloat blu[4] = {0.1f, 0.1f, 0.4f, 0.2f};
        GLfloat green[4] = {0.1f, 0.4f, 0.1f, 0.2f};

        if (mu_begin_window_ex(&mu_ctx, "BVH", mu_rect(220, 40, 300, 340), MU_OPT_NOCLOSE)) {
            static int show_leaf_boxes;
            static int show_inner_boxes;
            static int show_obb;
            mu_checkbox(&mu_ctx, "Show leaf boxes", &show_leaf_boxes);
            mu_checkbox(&mu_ctx, "Leaf OBB instead", &show_obb);
            mu_checkbox(&mu_ctx, "Show inner boxes", &show_inner_boxes);
            mu_layout_row(&mu_ctx, 1, (int[]){-1}, 0);

            void traverse(uint32_t i)
            {
                bvh_node_t *n = &scene.bvh.nodes[i];
                bool is_leaf = bvh_node_is_leaf(n);
                char msg[100];
                const char *title = "";

                if (is_leaf) {
                    title = scene.node_names[n->idx];
                }

                snprintf(msg, sizeof(msg), "[%lu] %s", i, title);

                if (mu_begin_treenode(&mu_ctx, msg)) {
                    snprintf(msg, sizeof(msg), "radius^^2=%.3f", n->radius_sqr);
                    mu_text(&mu_ctx, msg);

                    if (is_leaf && show_leaf_boxes) {
                        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &red[0]);
                        if (show_obb) {
                            uint16_t model_idx = bvh_node_get_idx(n);
                            render_posed_unit_cube(&scene.node_xforms[model_idx]);
                        } else {
                            render_aabb(&scene.bvh.node_aabbs[i]);
                        }
                    } else if (show_inner_boxes) {
                        render_aabb(&scene.bvh.node_aabbs[i]);
                        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &blu[0]);
                    }

                    if (is_leaf) {
                        snprintf(msg, sizeof(msg), "go to idx=%u", n->idx);
                        if (mu_button(&mu_ctx, msg)) {
                            fps_camera.pos[0] = n->pos[0];
                            // fps_camera.pos[1] = n->pos[1];
                            fps_camera.pos[2] = n->pos[2];
                            fps_camera.pos[2] -= 2.0f * sqrtf(n->radius_sqr);
                            fps_camera.angle = M_PI_2;
                        }
                    } else {
                        if (show_inner_boxes) {
                            aabb_t planebox = scene.bvh.node_aabbs[i];
                            uint32_t ax = bvh_node_get_axis(n);
                            planebox.lo[ax] = n->pos[ax] - 1e-3;
                            planebox.hi[ax] = n->pos[ax] + 1e-3;

                            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &green[0]);
                            render_aabb(&planebox);
                        }

                        snprintf(msg, sizeof(msg), "flags=0x%02x", n->flags);
                        mu_text(&mu_ctx, msg);
                        if (n->flags & BVH_FLAG_LEFT_CHILD) {
                            traverse(i + 1);
                        }
                        if (n->flags & BVH_FLAG_RIGHT_CHILD) {
                            traverse(n->idx);
                        }
                    }
                    mu_end_treenode(&mu_ctx);
                }
            }

            traverse(0);
        }

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);

        mu_end_window(&mu_ctx);
    }

    bool show_bvh = g_show_node >= -1;

    if (show_bvh || config_show_bvh_boxes)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        GLfloat red[4] = {0.4f, 0.1f, 0.1f, 0.2f};
        GLfloat blu[4] = {0.1f, 0.1f, 0.4f, 0.2f};
        GLfloat green[4] = {0.1f, 0.4f, 0.1f, 0.2f};

        for (uint32_t i=0;i<scene.bvh.num_nodes;i++) {
            bvh_node_t* n = &scene.bvh.nodes[i];
            bool is_leaf = bvh_node_is_leaf(n);

            if (g_show_node >= 0 && i != g_show_node) {
                continue;
            }

            //if (!in_frustum) {
            //    debugf("[bvh node=%lu] frustum culling node\n", i);
            //}

            // debugf("[bvh node=%lu] is_leaf=%d, in_frustum=%d\n", i, is_leaf, in_frustum);
            // float diff[3]={
            //     fps_camera.pos[0] - n->pos[0],
            //     fps_camera.pos[1] - n->pos[1],
            //     fps_camera.pos[2] - n->pos[2],
            // };
            // float d = diff[0]*diff[0] + diff[1]*diff[1] + diff[2]*diff[2];
            // //render_posed_unit_cube(&scene.bvh_xforms[i]);
            // float dist = d - n->radius_sqr;
            // bool clips = dist < 1.0f;
            //bool near = !inside && dist < 2.0f*2.0f;
            if (is_leaf) {
                glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &red[0]);
            } else { 
                glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &blu[0]);
            }

            if (!config_show_bvh_boxes) {
                // if (is_leaf) continue;
                glPushMatrix();
                glMultMatrixf(&scene.bvh_xforms[i].m[0][0]);
                // if (clips) {
                // glCullFace(GL_FRONT);
                // } else {
                // glCullFace(GL_BACK);
                // }
                draw_sphere();
                glPopMatrix();
            }

            if (!config_show_bvh_boxes) {
                render_aabb(&scene.bvh.node_aabbs[i]);
            }
            if (!is_leaf) {
                aabb_t planebox = scene.bvh.node_aabbs[i];
                uint32_t ax = bvh_node_get_axis(n);
                planebox.lo[ax] = n->pos[ax] - 1e-3;
                planebox.hi[ax] = n->pos[ax] + 1e-3;

                glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &green[0]);
                render_aabb(&planebox);
            }
            glCullFace(GL_BACK);
        }

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
}

static float point3_distance_sqr(const float* a, const float* b) {
    float diff[3] = {
        b[0] - a[0],
        b[1] - a[1],
        b[2] - a[2],
    };
    return diff[0]*diff[0] + diff[1]*diff[1] + diff[2]*diff[2];
}


void render(double delta)
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    // Evaluate fog probes

    if (config_enable_fog_probes) {
        prof_begin(REGION_PROBES);
        const float env_weight = 1e-2; // assign some weight to env color that's used when not near any probe
        float fog_color[4] = {env_weight * fog_probes[0].color[0], env_weight * fog_probes[0].color[1], env_weight * fog_probes[0].color[2], env_weight};
        for (int i = 1; i < GET_ARRAY_SIZE(fog_probes); i++) {
            float dist = point3_distance_sqr(&fps_camera.pos[0], &fog_probes[i].pos[0]);
            float w = 1.0f / (dist + 4.0f);
            fog_color[0] += w * fog_probes[i].color[0];
            fog_color[1] += w * fog_probes[i].color[1];
            fog_color[2] += w * fog_probes[i].color[2];
            fog_color[3] += w;
        }
        fog_color[0] /= fog_color[3];
        fog_color[1] /= fog_color[3];
        fog_color[2] /= fog_color[3];
        glFogfv(GL_FOG_COLOR, fog_color);
        glClearColor(fog_color[0], fog_color[1], fog_color[2], 1.0f);
        prof_end(REGION_PROBES);
    } else {
        glFogfv(GL_FOG_COLOR, environment_color);
        glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    occ_next_frame(culler);
    occ_clear_zbuffer(sw_zbuffer);
    
    if (config_top_down_view || config_show_wireframe) {
        glDisable(GL_FOG);
    } else {
        glEnable(GL_FOG);
    }

    glMatrixMode(GL_MODELVIEW);

    if (g_camera_mode == CAM_SPIN) {
        compute_camera_matrix(&g_view, &camera);
    } else if (g_camera_mode == CAM_FPS) {
        compute_fps_camera_matrix(&g_view, &fps_camera);
    }
    else {
        assert(!"unknown camera mode");
    }

    // matrix_t mvp;
    // matrix_mult_full(&mvp, &g_projection, &g_view);

    occ_set_view_and_projection(culler, &g_view, &g_projection);
    glLoadMatrixf(&g_view.m[0][0]);

    // glLightfv(GL_LIGHT0, GL_POSITION, &fps_camera.pos[0]);

    if (config_top_down_view) {
        matrix_t new_view;
        compute_top_down_camera_matrix(&new_view, &fps_camera);
        glLoadMatrixf(&new_view.m[0][0]);
    }

    //render_door_scene(disp);
    //render_big_scene(disp);
    render_scene(disp);
    //render_2d_scene(disp);
    //render_single_cube_scene(disp);
    g_camera_mode = CAM_FPS;
    gl_context_end();


    if (true) {
        uint16_t minz = 0xffff;
        for (int y = 0; y < sw_zbuffer->height; y++) {
            for (int x = 0; x < sw_zbuffer->width; x++) {
                uint16_t z = ((uint16_t *)(sw_zbuffer->buffer + sw_zbuffer->stride * y))[x];
                if (z < minz) minz = z;
            }
        }
        // rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 20, "minZ: %u", minz);
    }

    // Show the software zbuffer

    rdpq_blitparms_t params={};
    // rdpq_attach(disp, NULL);
    if (config_depth_view_mode > 0) {
        rdpq_set_mode_standard(); // Can't use copy mode if we need a 16-bit -> 32-bit conversion
        if (config_depth_view_mode == 2) {
            rdpq_set_env_color((color_t){0, 0, 0, 160});
            rdpq_mode_combiner(RDPQ_COMBINER1((0, 0, 0, TEX0), (0, 0, 0, ENV)));
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            params.scale_x = SCREEN_WIDTH / CULL_W;
            params.scale_y = SCREEN_HEIGHT / CULL_H;
        }

        rdpq_tex_blit(sw_zbuffer, 0, 0, &params);
    }

    if (config_show_last_box && (g_num_frames % 2 ==0)) {
        rdpq_set_mode_fill((color_t){255,0,0,255});
        const occ_result_box_t* box = &last_visible_result.box;
        int scalex = SCREEN_WIDTH / CULL_W;
        int scaley = SCREEN_HEIGHT / CULL_H;
        rdpq_fill_rectangle(scalex*box->minX, scaley*box->minY, scalex*box->maxX, scaley*box->maxY);
        // debugf("filled (%d, %d, %d, %d)\n", box->minX, box->minY, box->maxX, box->maxY);
    }

    rspq_flush();

    rdpq_text_print(NULL, FONT_SCIFI, CULL_W + 8, 20, config_enable_culling ? "occlusion culling: ON" : "occlusion culling: OFF");
    rdpq_text_print(NULL, FONT_SCIFI, CULL_W + 8, 30, config_show_wireframe ? "show culled: ON" : "show culled: OFF");
    rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 40, "visible: %d/%d objects, %d occluders", scene_stats.num_drawn, scene_stats.num_max, scene_stats.num_occluders_drawn);
    rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 50, "delta: % 6.3f ms, %.1f fps", delta*1000, 1.0/delta);

    mu64_end_frame();
    if (config_menu_open) {
        mu64_draw();
    }
    
    rdpq_detach_show();

    rspq_profile_next_frame();

    if (((g_num_frames) % 60) == 0) {
        rspq_profile_dump();
        rspq_profile_reset();
    }

    if (sw_zbuffer == &sw_zbuffer_array[0]) {
        sw_zbuffer = &sw_zbuffer_array[1];
    }
    else {
        sw_zbuffer = &sw_zbuffer_array[0];
    }
}

struct {
    uint32_t ticks[3];
    int next;
    uint32_t mean;
} avg_delta = {};

void delta_time_init() {
    avg_delta.next = 0;
    avg_delta.mean = 0;
    for (int i = 0; i < GET_ARRAY_SIZE(avg_delta.ticks); i++) {
        avg_delta.ticks[i] = (uint32_t)(1./30.0 * TICKS_PER_SECOND);
        avg_delta.mean += avg_delta.ticks[i];
    }
}

float get_smoothed_delta(uint32_t tickdelta) {
    float old = avg_delta.ticks[avg_delta.next];
    avg_delta.ticks[avg_delta.next++] = tickdelta;
    if (avg_delta.next == GET_ARRAY_SIZE(avg_delta.ticks)) {
        avg_delta.next = 0;
    }
    avg_delta.mean -= old;
    avg_delta.mean += tickdelta;
    float div = 1.0f / GET_ARRAY_SIZE(avg_delta.ticks);
    return avg_delta.mean * div / (float)TICKS_PER_SECOND;
}

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    dfs_init(DFS_DEFAULT_LOCATION);

    const bool hires = HIRES_MODE;
    if (hires) {
        display_init(RESOLUTION_640x480, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER /* FILTERS_DISABLED */);
    } else {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
        *(volatile uint32_t*)0xA4400000 |= 0x300; //disable filtering on PAL
    }

    joypad_init();
    rdpq_init();
    gl_init();
    delta_time_init();

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    glDepthRange(0, 1); // This is the default but set here to draw attention it since it's also the culler's convention
    culler = occ_alloc();
    occ_set_viewport(culler, 0, 0, CULL_W, CULL_H);

    rdpq_font_t *font = rdpq_font_load("rom:/KidsDraw.font64");
    uint8_t font_id = 2;
    rdpq_text_register_font(font_id, font);
    mu64_init(JOYPAD_PORT_2, font_id);


    setup();
    setup_scene();

    rspq_profile_start();

    double delta=1/30.0f; // last frame's delta time
    double smoothed_delta=1/30.0f;
    uint32_t last_ticks = get_ticks();

#if !DEBUG_RDP
    while (1)
#endif
    {
        mu64_start_frame();
        mu64_set_mouse_speed(0.10f * (float)delta); // keep cursor speed constant

        if (config_menu_open) {
            if (mu_begin_window_ex(&mu_ctx, "Settings", mu_rect(10, 40, 140, 120), MU_OPT_NOCLOSE)) {
                mu_layout_row(&mu_ctx, 1, (int[]){-1}, 0);
                mu_label(&mu_ctx, "Background");
                mu_checkbox(&mu_ctx, "Show BVH boxes", &config_show_bvh_boxes);
                mu_checkbox(&mu_ctx, "Rough test only", &config_force_rough_test_only);
                mu_checkbox(&mu_ctx, "Show last rough box", &config_show_last_box);
                mu_checkbox(&mu_ctx, "Cull occluders", &config_cull_occluders);
                mu_checkbox(&mu_ctx, "Enable fog probes", &config_enable_fog_probes);
                float value = config_test_occluders_after;
                mu_slider(&mu_ctx, &value, 0, 50);
                config_test_occluders_after = value;
                value = config_max_occluders;
                mu_slider(&mu_ctx, &value, 0, 50);
                config_max_occluders = value;

                mu_end_window(&mu_ctx);
            }

        }

        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_inputs_t mouse_inputs = joypad_get_inputs(JOYPAD_PORT_2);
        joypad_buttons_t mouse_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_2);
        prof_next_frame();

        if (!config_menu_open) {
            if (mouse_pressed.a) {
                g_show_node++;
                debugf("g_show_node: %d\n", g_show_node);
            }
            if (mouse_pressed.b) {
                g_show_node--;
                debugf("g_show_node: %d\n", g_show_node);
            }
        }

        if (pressed.a) {
            animation++;
        }

        if (pressed.b) {
            animation--;
        }

        if (pressed.start) {
            config_menu_open = !config_menu_open;
        }

        if (pressed.l) {
            config_top_down_view = !config_top_down_view;
            debugf("top down view: %d\n", config_top_down_view);
        }

        if (pressed.r) {
            config_show_wireframe = !config_show_wireframe;
        }


        if (pressed.c_up) {
            config_enable_culling = !config_enable_culling;
            make_sphere_mesh();
        }

        if (pressed.c_down) {
            config_depth_view_mode = (config_depth_view_mode + 1) % 3;
        }

        float y = inputs.stick_y / 128.f;
        float x = inputs.stick_x / 128.f;
        float mag = x * x + y * y;

        if (g_camera_mode == CAM_SPIN) {
            if (fabsf(mag) > 0.01f) {
                camera.distance += y * 0.2f;
                camera.rotation = camera.rotation - x * 1.2f;
            }
        }
        else if (g_camera_mode == CAM_FPS) {
            if (!config_menu_open) {
                float adelta = 0.075f;
                float mdelta = 0.20f;
                if (fabsf(mag) > 0.01f) {
                    fps_camera.pos[0] += 60.0f * smoothed_delta * mdelta * y * cos(fps_camera.angle);
                    fps_camera.pos[2] += 60.0f * smoothed_delta * mdelta * y * sin(fps_camera.angle);
                    fps_camera.pos[0] += 60.0f * smoothed_delta * mdelta * x * cos(fps_camera.angle + M_PI_2);
                    fps_camera.pos[2] += 60.0f * smoothed_delta * mdelta * x * sin(fps_camera.angle + M_PI_2);
                }

                if (fabsf(inputs.cstick_x) > 0.01f) {
                    fps_camera.angle = fmodf(fps_camera.angle + 60.0f * smoothed_delta * adelta * inputs.cstick_x / 127.f, 2 * M_PI);
                }

                const float mouse_sens = 2.0f;
                fps_camera.angle = fmodf(fps_camera.angle + mouse_sens * adelta * mouse_inputs.stick_x / 127.f, 2 * M_PI);
                fps_camera.pitch = fmodf(fps_camera.pitch + mouse_sens * adelta * mouse_inputs.stick_y / 127.f, 2 * M_PI);
            }
        }

        debugf("fps_camera: {.pos={%ff, %ff, %ff}, .angle=%ff, .pitch=%ff}\n",
            fps_camera.pos[0], fps_camera.pos[1],fps_camera.pos[2],
            fps_camera.angle, fps_camera.pitch);


        render(smoothed_delta);
        if (DEBUG_RDP)
            rspq_wait();
        
        rspq_wait();
        rspq_flush();

        uint32_t ticks_end = get_ticks();
        if (true) {
            delta = (ticks_end - last_ticks) / (double)TICKS_PER_SECOND;
            smoothed_delta = get_smoothed_delta(ticks_end - last_ticks);
            //debugf("deltatime: % 6f ms\n", delta * 1000.0);
            prof_print_stats();
        }
        last_ticks = ticks_end;
        prof_reset_stats();

        g_num_frames++;
    }
}
