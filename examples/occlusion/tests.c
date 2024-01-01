// tests

#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <GL/glu.h>
#include <libdragon.h>
#include <malloc.h>
#include <math.h>
#include <rspq_profile.h>

#include <model64.h>
#include "../../src/model64_internal.h"

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

#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)
#define CULL_W (SCREEN_WIDTH / 8)
#define CULL_H (SCREEN_HEIGHT / 8)

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
// static fps_camera_t fps_camera = {.pos = {0.0, 0.000000f, 10.000000f}, .angle =-M_PI_2, .pitch=0.0f};
// static fps_camera_t fps_camera = {.pos = {0.0, 0.000000f, 10.000000f}, .angle =-0.673158, .pitch=0.0f};
static fps_camera_t fps_camera = {.pos={-6.866766f, 0.000000f, 6.455487f}, .angle=-0.134575, .pitch=0.000000};
int g_camera_mode = CAM_SPIN;
matrix_t g_view;
static surface_t zbuffer;
static surface_t sw_zbuffer_array[2];
static surface_t *sw_zbuffer;

static matrix_t g_projection;

static uint64_t g_num_frames = 0;
int g_show_node = -2;

static GLuint textures[4];

static const GLfloat environment_color[] = {0.85f, 0.85f, 1.0f, 1.f};

static bool config_enable_culling = true;
static bool config_show_wireframe = false;
static int config_depth_view_mode = 2;
static bool config_conservative = true;
static bool config_top_down_view = false;
static float config_far_plane = 50.f;

static const GLfloat light_pos[8][4] = {
    {1, 0, 0, 0},
    {-1, 0, 0, 0},
    {0, 0, 1, 0},
    {0, 0, -1, 0},
    {8, 3, 0, 1},
    {-8, 3, 0, 1},
    {0, 3, 8, 1},
    {0, 3, -8, 1},
};

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
    camera.distance=-1.0; camera.rotation=0.f;

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

    glFogf(GL_FOG_START, 1);
    glFogf(GL_FOG_END, far_plane);
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
        .num_vertices = sizeof(cube_cvertices) / sizeof(cube_cvertices[0]),
        .num_indices = sizeof(cube_indices) / sizeof(cube_indices[0]),
    };

    bool conversion_success = occ_hull_from_flat_mesh(&cube_mesh, &cube_hull);
    if (!conversion_success) {
        assert(false && "Couldn't convert cube mesh!");
    }

    occ_mesh_t unit_cube_mesh = {
        .vertices = (cvertex_t*)unit_cube_cvertices,
        .indices = (uint16_t*)unit_cube_indices,
        .num_vertices = sizeof(unit_cube_cvertices) / sizeof(unit_cube_cvertices[0]),
        .num_indices = sizeof(unit_cube_indices) / sizeof(unit_cube_indices[0]),
    };
    conversion_success = occ_hull_from_flat_mesh(&unit_cube_mesh, &unit_cube_hull);
    if (!conversion_success) {
        assert(false && "Couldn't convert unit cube mesh!");
    }
}

void set_light_positions(float rotation)
{
    glPushMatrix();
    glRotatef(rotation * 5.43f, 0, 1, 0);

    for (uint32_t i = 0; i < 8; i++) {
        glLightfv(GL_LIGHT0 + i, GL_POSITION, light_pos[i]);
    }
    glPopMatrix();
}

static occ_target_t cube_target = {};

void render_door_scene(surface_t* disp)
{
    long unsigned int anim_timer = g_num_frames;
    //debugf("g_num_frames: %llu\n", g_num_frames);
    float rotation = animation * 0.5f;

    set_light_positions(rotation);

    // Set some global render modes that we want to apply to all models
    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    // Draw occluders
    
    render_plane();

    // occ_mesh_t plane_mesh = {
    //      .vertices = plane_vertices,
    //      .indices = plane_indices,
    //      .num_indices = plane_index_count,
    //      .num_vertices = plane_vertex_count
    //  };

    // occ_draw_mesh(culler, sw_zbuffer, &plane_mesh, NULL);

    occ_mesh_t cube_mesh = {
        .vertices = (cvertex_t*)cube_cvertices,
        .indices = (uint16_t*)cube_indices,
        .num_vertices = sizeof(cube_cvertices) / sizeof(cube_cvertices[0]),
        .num_indices = sizeof(cube_indices) / sizeof(cube_indices[0]),
    };

    for (int i = 0; i < 3; i++) {
        glPushMatrix();
        matrix_t scale = cpu_glScalef(1.0f, 1.75f, 0.2f);
        matrix_t translate = cpu_glTranslatef((-1 + i) * 8.f + 2.0f * sin(i * 1.5f + 0.05f * anim_timer), 6.0f, sin(i * 2.f));
        matrix_t xform;
        matrix_mult_full(&xform, &translate, &scale);

        glMultMatrixf(&xform.m[0][0]);
        render_cube();
        occ_draw_mesh(culler, sw_zbuffer, &cube_mesh, &xform);
        glPopMatrix();
    }

    // We are interested in target cube's visiblity. Compute its model-to-world transform.

    matrix_t cube_rotate = cpu_glRotatef(2.f * anim_timer, sqrtf(3.f), 0.0f, sqrtf(3.f));
    //matrix_t cube_rotate = cpu_glRotatef(0.0f, 1.0f, 0.0f, 0.0f);
    matrix_t cube_translate = cpu_glTranslatef(0.0f + 6.0f * sin(anim_timer * 0.04f), 5.0f, 7.0f);
    matrix_t cube_xform;

    matrix_mult_full(&cube_xform, &cube_translate, &cube_rotate);

    // Occlusion culling

    //occ_result_box_t box = {};
    occ_raster_query_result_t raster_query = {};
    //bool cube_visible = occ_check_target_visible(culler, sw_zbuffer, &cube_mesh, &cube_xform, &cube_target, &raster_query);
    bool cube_visible=true;
    (void)raster_query.depth;
    (void)cube_target.last_visible_frame;
    assert(false && "need to update occ check to use hull");
    //box.hitX = raster_query.x;
    //box.hitY = raster_query.y;
    //box.udepth = raster_query.depth;
    // bool cube_visible = occ_check_mesh_visible_rough(culler, sw_zbuffer, &cube_mesh, &cube_xform, &box);
    //occ_draw_mesh(culler, sw_zbuffer, &cube_mesh, &cube_xform);

	// occ_draw_indexed_mesh_flags(culler, sw_zbuffer, &cube_xform, cube_mesh.vertices, cube_mesh.indices, cube_mesh.num_indices, NULL, (OCC_RASTER_FLAGS_QUERY & (~RASTER_FLAG_EARLY_OUT)) | RASTER_FLAG_WRITE_DEPTH, NULL);

    if (cube_visible || config_show_wireframe) {
        bool wireframe = !cube_visible;

        // Continue drawing other objects

        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_LIGHTING);
            glDisable(GL_TEXTURE_2D);
        }
        glPushMatrix();
        glMultMatrixf(&cube_xform.m[0][0]);
        render_cube();
        glPopMatrix();
        if (wireframe) {
            glEnable(GL_TEXTURE_2D);
            glEnable(GL_LIGHTING);
            glEnable(GL_DEPTH_TEST);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    }

    // render_decal();
    // render_skinned(&camera, animation);

    glBindTexture(GL_TEXTURE_2D, textures[(texture_index + 1) % 4]);
    // render_sphere(rotation);

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    // render_primitives(rotation);
}

#define BIG_SCENE_NUM_CUBES (100)

struct big_scene_s
{
    occ_target_t targets[BIG_SCENE_NUM_CUBES];
} big_scene = {};

struct {
    int num_drawn;
    int num_max;
} scene_stats;


void setup_big_scene()
{
}

void render_big_scene(surface_t* disp)
{
    long unsigned int anim_timer = 0; //g_num_frames;

    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    const float wave = anim_timer * 0.2f;

    const int num_cubes = BIG_SCENE_NUM_CUBES;
    matrix_t cube_xforms[num_cubes];
    float cube_z_dists[num_cubes];
    int cube_order[num_cubes];

    const float height = 5.0f;
    for (int i = 0; i < num_cubes; i++) {
        float theta = i*2.83f;
        // float phi = i*3.1f;
        const float r = 15.0f; //10.0f * (0.5f + .5f*sin(i));
        vec3f pos = (vec3f){
            r * cos(theta),
            height + (i / (float)num_cubes - 0.5f) * 1.5f * r,
            r * sin(theta)};

        // compute viewspace z for front-to-back drawing
        float world[4] = {pos.x, pos.y, pos.z, 1.0f};
        float view[4] = {};

        matrix_mult(&view[0], &g_view, &world[0]);
        cube_z_dists[i] = -view[2];

        matrix_t translate = cpu_glTranslatef(pos.x, pos.y, pos.z);
        matrix_t rotate = cpu_glRotatef(i*45.f+wave, sqrtf(2)/2.f, 0.0f, sqrtf(2)/2.f);
        matrix_t temp;
        matrix_t scale = cpu_glScalef(1.0f, 2.0f, 1.0f);
        matrix_mult_full(&temp, &rotate, &scale);
        matrix_mult_full(&cube_xforms[i], &translate, &temp);
    }

    if (config_enable_culling) {
        // sort cubes by viewspace z

        int compare(const void *a, const void *b)
        {
            float fa = cube_z_dists[*(int *)a];
            float fb = cube_z_dists[*(int *)b];
            // debugf("%f < %f\n", fa, fb);
            if (fa < fb) {
                return -1;
            }
            else if (fa > fb) {
                return 1;
            }
            else {
                return 0;
            }
        }

        for (int i = 0; i < num_cubes; i++) {
            cube_order[i] = i;
        }

        qsort(cube_order, num_cubes, sizeof(cube_order[0]), compare);

        // draw from front to back

        scene_stats.num_drawn = 0;
        scene_stats.num_max = BIG_SCENE_NUM_CUBES;
        for (int order_i = 0; order_i < num_cubes; order_i++) {
            int idx = cube_order[order_i];
            // debugf("order_i =%d, idx= %d, dist = %f\n", order_i, idx, cube_z_dists[idx]);
            // query for visibility
            // debugf("i=%d\n", i);
            matrix_t *xform = &cube_xforms[idx];
            bool visible = occ_check_target_visible(culler, sw_zbuffer, &cube_hull, xform, &big_scene.targets[idx], NULL);

            if (visible || config_show_wireframe) {
                glPushMatrix();
                glMultMatrixf(&xform->m[0][0]);
                if (visible) {
                    render_cube();
                }
                else {
                    // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                    glDisable(GL_DEPTH_TEST);
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_ONE, GL_ONE);
                    glDisable(GL_LIGHTING);
                    glDisable(GL_TEXTURE_2D);
                    render_cube();
                    glEnable(GL_TEXTURE_2D);
                    glEnable(GL_LIGHTING);
                    glDisable(GL_BLEND);
                    glEnable(GL_DEPTH_TEST);
                    // glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                }
                glPopMatrix();
            }

            if (visible) {
                // also draw to software zbuffer
                occ_draw_hull(culler, sw_zbuffer, &cube_hull, xform, NULL, 0);
            }
            if (visible) {
                scene_stats.num_drawn++;
            }
        }
    } else {
        scene_stats.num_drawn = 0;
        for (int idx = 0; idx < num_cubes; idx++) {
            glPushMatrix();
            glMultMatrixf(&cube_xforms[idx].m[0][0]);
            render_cube();
            glPopMatrix();
            scene_stats.num_drawn++;
        }
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
}

static void copy_to_matrix(const float* in, matrix_t* out) {
    // matrix_t is in column-major order with m[col][row]

    for (int i = 0; i < 16; ++i) {
        out->m[i / 4][i % 4] = in[i];
    }
}

#define CITY_SCENE_MAX_OCCLUDERS (10)
#define CITY_SCENE_MAX_NODES (50)
#define CITY_SCENE_MAX_BVH_SIZE (200)

struct city_scene_s
{
    occ_target_t targets[CITY_SCENE_MAX_NODES];
    struct {
        int num_drawn;
    } stats;

    model64_t *mdl_room;
    uint32_t num_nodes;
    uint32_t num_occluders;
    model64_node_t* nodes[CITY_SCENE_MAX_NODES];
    GLuint node_dplists[CITY_SCENE_MAX_NODES];
    matrix_t node_xforms[CITY_SCENE_MAX_NODES];
    bool node_should_test[CITY_SCENE_MAX_NODES];
    const char* node_names[CITY_SCENE_MAX_NODES];

    model64_node_t* occluders[CITY_SCENE_MAX_OCCLUDERS];
    occ_mesh_t occ_meshes[CITY_SCENE_MAX_OCCLUDERS];
    occ_hull_t occ_hulls[CITY_SCENE_MAX_OCCLUDERS];
    matrix_t occluder_xforms[CITY_SCENE_MAX_OCCLUDERS];

    sphere_bvh_t bvh;
    matrix_t bvh_xforms[CITY_SCENE_MAX_BVH_SIZE];
} scene = {};

void setup_city_scene()
{
    //bool verbose=false;
    struct city_scene_s* s = &scene;

    model64_t* model = model64_load("rom://room1.model64");
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
            if (s->num_occluders >= CITY_SCENE_MAX_OCCLUDERS) {
                debugf("Error: max occluders reached\n");
                break;
            }
            copy_to_matrix(&model->transforms[i].world_mtx[0], &scene.occluder_xforms[s->num_occluders]);

            s->occluders[s->num_occluders] = node;
            s->num_occluders++;
        } else {
            if (s->num_nodes >= CITY_SCENE_MAX_NODES) {
                debugf("Error: max nodes reached\n");
                break;
            }

            if (strstr(node->name, "detail") != NULL) {
                scene.node_should_test[s->num_nodes] = true;
            }
            copy_to_matrix(&model->transforms[i].world_mtx[0], &scene.node_xforms[s->num_nodes]);
            s->nodes[s->num_nodes] = node;
            scene.node_names[s->num_nodes] = node->name;
            s->num_nodes++;
            debugf("wrote node node=%p", s->nodes[s->num_nodes - 1]);
        }
    }

    GLfloat mat_diffuse[] = 
    {
        1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
    };

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
        scene.node_dplists[i] = list;
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mat_diffuse[4*(i%4)]);
        model64_node_t* node = scene.nodes[i];
        model64_draw_node(scene.mdl_room, node);
        glEndList();

        float obj_radius = 0.0f;
        float world_radius = 0.0f;

        aabb_t obj_aabb={};
        aabb_t world_aabb={};
        float world_center[3]={};
        bool bounds_ok = compute_mesh_bounds(node->mesh, &scene.node_xforms[i], &obj_radius, &obj_aabb, &world_radius, &world_aabb, &world_center[0]);
        debugf("[node %lu] OK: %d, obj_radius=%f, min=(%.3f, %.3f, %.3f), max=(%.3f, %.3f, %.3f)\n", i, bounds_ok, obj_radius,
            obj_aabb.lo[0], obj_aabb.lo[1], obj_aabb.lo[2],
            obj_aabb.hi[0], obj_aabb.hi[1], obj_aabb.hi[2]
        );
        debugf("[node %lu] OK: %d, world_radius=%f, min=(%.3f, %.3f, %.3f), max=(%.3f, %.3f, %.3f), center=(%f, %f, %f)\n", i, bounds_ok, world_radius,
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
        debugf("[node %lu] scale: (%.3f, %.3f, %.3f), mid: (%.3f, %.3f, %.3f)\n",
            i,
            scale[0], scale[1], scale[2],
            mid[0], mid[1], mid[2]
        );

        debugf("matrix %lu\n", i);
        print_matrix(&scene.node_xforms[i]);
        float* orig = &origins[3*i];
        orig[0] = world_center[0];
        orig[1] = world_center[1];
        orig[2] = world_center[2];
        radiuses[i] = world_radius;
        aabbs[i] = world_aabb;

        matrix_t temp = cpu_glScalef(scale[0], scale[1], scale[2]);
        matrix_t old = scene.node_xforms[i];
        matrix_mult_full(&scene.node_xforms[i], &old, &temp);
        old = scene.node_xforms[i];
        temp = cpu_glTranslatef(mid[0], mid[1], mid[2]);
        matrix_mult_full(&scene.node_xforms[i], &old, &temp);
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
    }

    debugf("input origins and rads:\n");
    for (uint32_t i = 0; i < scene.num_nodes; i++) {
        float r = radiuses[i];
        float* p = &origins[3*i];

        debugf("[%li] (%f, %f, %f), radius=%f\n", i, p[0], p[1], p[2], r);
    }

    uint64_t bvh_start_ticks = get_ticks();

    sphere_bvh_t bvh = {};
    bool success = bvh_build(origins, radiuses, aabbs, scene.num_nodes, &bvh);
    if (!success) {
        debugf("BVH building failed\n");
        return;
    }

    uint64_t stop_ticks = get_ticks();
    debugf("init took %lu ms, bvh build %lu ms\n", TICKS_TO_MS(TICKS_DISTANCE(start_ticks, stop_ticks)), TICKS_TO_MS(TICKS_DISTANCE(bvh_start_ticks, stop_ticks)));
    debugf("bvh.num_nodes=%lu\n", bvh.num_nodes);
    bool ok = bvh_validate(&bvh);
    assert(bvh.num_nodes < CITY_SCENE_MAX_BVH_SIZE);
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

//wait_for_button();
    // while(true){}
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

void render_city_scene(surface_t* disp)
{
    long unsigned int anim_timer = 0; //g_num_frames;
    (void)anim_timer;

    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    // Draw occluders

    if (config_enable_culling) {
        for (uint32_t i = 0; i < scene.num_occluders; i++) {
            debugf("drawing occluder %lu\n", i);
            occ_raster_query_result_t result = {};
            occ_draw_hull(culler, sw_zbuffer, &scene.occ_hulls[i], &scene.occluder_xforms[i], &result, OCCLUDER_TWO_SIDED);
        }
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

    static cull_result_t cull_results[CITY_SCENE_MAX_NODES]; // references Node* elements
    uint32_t num_visible = bvh_find_visible(&scene.bvh, culler->camera_pos, culler->clip_planes, cull_results, sizeof(cull_results) / sizeof(cull_results[0]));

    for (uint32_t res_idx = 0; res_idx < num_visible; res_idx++) {
        cull_result_t res = cull_results[res_idx];
        uint16_t idx = res.idx;
        debugf("[%u] .idx=%d, .flags=0x%x\n", idx, res.idx, res.flags);

        bool visible = true;

        if (config_enable_culling && !(res.flags & VISIBLE_CAMERA_INSIDE) && scene.node_should_test[idx]) {
            debugf("occlusion testing node '%s'\n", scene.node_names[idx]);
            occ_raster_query_result_t result = {};
            //occ_draw_hull(culler, sw_zbuffer, &unit_cube_hull, &city_scene.node_xforms[i], &result, 0);
            visible = occ_check_target_visible(culler, sw_zbuffer, &unit_cube_hull, &scene.node_xforms[idx], &scene.targets[idx], &result);
        }

        if (visible) {
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mat_diffuse[4*(idx%4)]);
            glCallList(scene.node_dplists[idx]);
            scene_stats.num_drawn++;
        }
    }

        //glDisable(GL_BLEND);
    //glEnable(GL_DEPTH_TEST);

    #if 0
    for (uint32_t i = 0; i < city_scene.num_nodes; i++) {
        //if (strcmp(city_scene.node_names[i], "room1 detail")) continue; // HACK skip other nodes

        bool visible = true;

        if (config_enable_culling && city_scene.node_should_test[i]) {
            debugf("occlusion testing node '%s'\n", city_scene.node_names[i]);
            occ_raster_query_result_t result = {};
            //occ_draw_hull(culler, sw_zbuffer, &unit_cube_hull, &city_scene.node_xforms[i], &result, 0);
            visible = occ_check_target_visible(culler, sw_zbuffer, &unit_cube_hull, &city_scene.node_xforms[i], &city_scene.targets[i], &result);
        }

        debugf("%lu visible=%d\n", i, visible);

        if (visible) {
            debugf("drawing %lu, mdl=%p, node=%p\n", i, city_scene.mdl_room, city_scene.nodes[i]);
            glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mat_diffuse[4*(i%4)]);
            glCallList(city_scene.node_dplists[i]);
            scene_stats.num_drawn++;
        } else if (config_show_wireframe) {
            //glPushMatrix();
            //glMultMatrixf(&city_scene.node_xforms[i].m[0][0]);
            //draw_unit_cube();
            //glPopMatrix();
            // glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &mat_diffuse[4*(i%4)]);
            // glCallList(city_scene.node_dplists[i]);
        }
        scene_stats.num_max++;
    }
    #endif

    if (config_show_wireframe) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        GLfloat white[4] = {0.2f, 0.2f, 0.2f, 0.2f};

        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &white[0]);

        for (uint32_t i = 0; i < scene.num_nodes; i++) {
            if (scene.targets[i].last_visible_frame == culler->frame) {
                render_posed_unit_cube(&scene.node_xforms[i]);
                // glPushMatrix();
                // glMultMatrixf(&city_scene.node_xforms[i].m[0][0]);
                // draw_unit_cube();
                // glPopMatrix();
            }
        }

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    bool show_bvh = g_show_node > 0;

    if (show_bvh)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);

        //GLfloat red[4] = {0.2f, 0.1f, 0.1f, 0.2f};
        //GLfloat blu[4] = {0.1f, 0.1f, 0.2f, 0.2f};
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
            // //render_posed_unit_cube(&city_scene.bvh_xforms[i]);
            // float dist = d - n->radius_sqr;
            // bool clips = dist < 1.0f;
            //bool near = !inside && dist < 2.0f*2.0f;
            if (is_leaf) {
                glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &red[0]);
            } else { 
                glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, &blu[0]);
            }

            //if (is_leaf) continue;
            glPushMatrix();
            glMultMatrixf(&scene.bvh_xforms[i].m[0][0]);
            // if (clips) {
            // glCullFace(GL_FRONT);
            // } else {
            // glCullFace(GL_BACK);
            // }

            // render_scaled_unit_cube(&city_scene.bvh_xforms[i], float scale) {
            draw_sphere();
            glPopMatrix();

            // render_aabb(&city_scene.bvh.node_aabbs[i]);
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

void render_2d_scene(surface_t*)
{
    vec2f a = (vec2f){10, 15};
    vec2f b = (vec2f){15, 25};
    vec2f c = (vec2f){35, 20};

    occ_raster_query_result_t result={};
    uint32_t flags = (RASTER_FLAG_BACKFACE_CULL | RASTER_FLAG_WRITE_DEPTH |RASTER_FLAG_ROUND_DEPTH_UP | RASTER_FLAG_DISCARD_FAR);

    if (config_conservative) {
        flags |= RASTER_FLAG_SHRINK_EDGE_01;
        flags |= RASTER_FLAG_SHRINK_EDGE_12;
        flags |= RASTER_FLAG_SHRINK_EDGE_20;
        // flags |= RASTER_FLAG_EXPAND_EDGE_01;
        // flags |= RASTER_FLAG_EXPAND_EDGE_12;
        // flags |= RASTER_FLAG_EXPAND_EDGE_20;
    }

    draw_tri(
        a, b, c,
        0.5f,
        0.5f,
        0.5f,
        flags,
        &result,
        sw_zbuffer);
}

static occ_target_t target_single_cube = {0};

void render_single_cube_scene(surface_t*)
{
    long unsigned int anim_timer = 10; //g_num_frames;

    glEnable(GL_LIGHTING);
    glEnable(GL_NORMALIZE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textures[texture_index]);

    matrix_t xform;

    vec3f pos = (vec3f){0.f, 2.f, 0.f};

    matrix_t translate = cpu_glTranslatef(pos.x, pos.y, pos.z);
    matrix_t rotate = cpu_glRotatef(45.f + anim_timer*0.5f, sqrtf(2)/2.f, 0.0f, sqrtf(2)/2.f);
    matrix_mult_full(&xform, &translate, &rotate);

    // bool visible = occ_check_target_visible(culler, sw_zbuffer, &cube_hull.mesh, &xform, &cube_target, NULL);
    // (void)visible;

    glPushMatrix();
    glMultMatrixf(&xform.m[0][0]);
    render_cube();
    glPopMatrix();

    //occ_draw_mesh(culler, sw_zbuffer, &cube_hull.mesh, &xform);
    occ_draw_hull(culler, sw_zbuffer, &cube_hull, &xform, NULL, 0);
    (void)target_single_cube;
    //occ_check_target_visible(culler, sw_zbuffer, &cube_hull, &xform, &target_single_cube, NULL);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
}

void render(double delta)
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    occ_next_frame(culler);
    occ_clear_zbuffer(sw_zbuffer);
    
    if (config_top_down_view || config_show_wireframe) {
        //glDisable(GL_FOG);
    } else {
        //glEnable(GL_FOG);
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

    glLightfv(GL_LIGHT0, GL_POSITION, &fps_camera.pos[0]);

    if (config_top_down_view) {
        matrix_t new_view;
        compute_top_down_camera_matrix(&new_view, &fps_camera);
        glLoadMatrixf(&new_view.m[0][0]);
    }

    //render_door_scene(disp);
    //render_big_scene(disp);
    //render_city_scene(disp);
    //render_2d_scene(disp);
    render_single_cube_scene(disp);
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

    // if (true || (g_num_frames / 2) % 2 == 0) {
    //     rdpq_set_mode_fill(cube_visible ? (color_t){0, 255, 0, 64} : (color_t){255, 0, 0, 64});
    //     rdpq_fill_rectangle(box.minX, box.minY, box.maxX, box.maxY);
    // }

    // debugf("octagon ratio: %f \n", g_num_checked / ((float)(box.maxX - box.minX) * (float)(box.maxY - box.minY)));
    // for (int i=0;i<g_num_checked;i++) {
    //     vec2 p = g_checked_pixels[i];
    //     graphics_draw_pixel(disp, p.x, p.y, 0x0fff);
    // }

    rspq_flush();

    // float xscale = disp->width / (float)sw_zbuffer->width;
    // float yscale = disp->height / (float)sw_zbuffer->height;
    // float xvisible = xscale * box.hitX;
    // float yvisible = yscale * box.hitY;

    // if (cube_visible) {
    //     // Draw the visible pixel to both the mini-image and the full rendering
    //     rdpq_set_mode_fill((color_t){0, 0, 255, 255});
    //     rdpq_fill_rectangle(box.hitX, box.hitY + 1, box.hitX, box.hitY + 1);
    //     if (config_show_visible_point) {
    //         rdpq_set_mode_fill((color_t){255, 255, 255, 255});
    //         rdpq_fill_rectangle(xvisible - 1, yvisible - 1, xvisible + 2, yvisible + 2);
    //     }
    // }

    // rdpq_text_print(NULL, FONT_SCIFI, xscale*(box.minX+box.maxX)*0.5f, yscale*(box.minY+box.maxY)*0.5f, cube_visible ? "seen" : "hidden");
    // rdpq_text_print(NULL, FONT_SCIFI, CULL_W + 8, 10, cube_visible ? "cube visible" : "cube hidden");
    rdpq_text_print(NULL, FONT_SCIFI, CULL_W + 8, 20, config_enable_culling ? "occlusion culling: ON" : "occlusion culling: OFF");
    rdpq_text_print(NULL, FONT_SCIFI, CULL_W + 8, 30, config_show_wireframe ? "show culled: ON" : "show culled: OFF");
    rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 40, "visible: %d/%d objects", scene_stats.num_drawn, scene_stats.num_max);
    rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 50, "delta: %.3f ms", delta*1000);
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

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    dfs_init(DFS_DEFAULT_LOCATION);

    display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    *(volatile uint32_t*)0xA4400000 |= 0x300; //disable filtering on PAL

    joypad_init();
    rdpq_init();
    gl_init();

#if DEBUG_RDP
    rdpq_debug_start();
    rdpq_debug_log(true);
#endif

    glDepthRange(0, 1); // This is the default but set here to draw attention it since it's also the culler's convention
    culler = occ_alloc();
    occ_set_viewport(culler, 0, 0, CULL_W, CULL_H);

    setup();
    setup_big_scene();

    rspq_profile_start();

    double delta=1/30.0f; // last frame's delta time
    uint32_t last_ticks = get_ticks();

#if !DEBUG_RDP
    while (1)
#endif
    {
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        joypad_inputs_t mouse_inputs = joypad_get_inputs(JOYPAD_PORT_2);
        joypad_buttons_t mouse_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_2);
        prof_next_frame();

        if (mouse_pressed.a) {
            g_show_node++;
            debugf("g_show_node: %d\n", g_show_node);
        }
        if (mouse_pressed.b) {
            g_show_node--;
            debugf("g_show_node: %d\n", g_show_node);
        }

        if (pressed.a) {
            animation++;
        }

        if (pressed.b) {
            animation--;
        }

        if (pressed.start) {
            debugf("%ld\n", animation);
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
            float adelta = 0.075f;
            float mdelta = 0.20f;
            if (fabsf(mag) > 0.01f) {
                fps_camera.pos[0] += mdelta * y * cos(fps_camera.angle);
                fps_camera.pos[2] += mdelta * y * sin(fps_camera.angle);
                fps_camera.pos[0] += mdelta * x * cos(fps_camera.angle + M_PI_2);
                fps_camera.pos[2] += mdelta * x * sin(fps_camera.angle + M_PI_2);
            }

            if (fabsf(inputs.cstick_x) > 0.01f) {
                fps_camera.angle = fmodf(fps_camera.angle + adelta * inputs.cstick_x/127.f, 2 * M_PI);
            }

            const float mouse_sens = 2.0f;
            fps_camera.angle = fmodf(fps_camera.angle + mouse_sens * adelta * mouse_inputs.stick_x / 127.f, 2 * M_PI);
            fps_camera.pitch = fmodf(fps_camera.pitch + mouse_sens * adelta * mouse_inputs.stick_y / 127.f, 2 * M_PI);
        }

        debugf("fps_camera: {.pos={%ff, %ff, %ff}, .angle=%f, .pitch=%f}\n",
            fps_camera.pos[0], fps_camera.pos[1],fps_camera.pos[2],
            fps_camera.angle, fps_camera.pitch);

        render(delta);
        if (DEBUG_RDP)
            rspq_wait();
        
        rspq_wait();
        rspq_flush();
        uint32_t ticks_end = get_ticks();
        if (true) {
            delta = (ticks_end - last_ticks) / (double)TICKS_PER_SECOND;
            debugf("deltatime: %f ms\n", delta * 1000.0);
            prof_print_stats();
        }
        last_ticks = ticks_end;
        prof_reset_stats();

        g_num_frames++;
    }
}

