#include <GL/gl.h>
#include <GL/gl_integration.h>
#include <GL/glu.h>
#include <libdragon.h>
#include <malloc.h>
#include <math.h>
#include <rspq_profile.h>

#include "camera.h"
#include "cube.h"
#include "decal.h"
#include "plane.h"
#include "prim_test.h"
#include "skinned.h"
#include "sphere.h"

#include "occlusion.h"

// Set this to 1 to enable rdpq debug output.
// The demo will only run for a single frame and stop.
#define DEBUG_RDP 0

#define SCREEN_WIDTH (320)
#define SCREEN_HEIGHT (240)
#define CULL_W (SCREEN_WIDTH / 8)
#define CULL_H (SCREEN_HEIGHT / 8)

static occ_culler_t *culler;
static occ_hull_t cube_hull;

static uint32_t animation = 0;
static uint32_t texture_index = 0;
static camera_t camera;
matrix_t g_view;
static surface_t zbuffer;
static surface_t sw_zbuffer_array[2];
static surface_t *sw_zbuffer;

static matrix_t g_projection;
// static matrix_t g_cube_xform;

static uint64_t g_num_frames = 0;

static GLuint textures[4];

static const GLfloat environment_color[] = {0.1f, 0.5f, 0.5f, 1.f};

static bool config_show_visible_point = true;
static bool config_show_wireframe = false;
static bool config_enable_culling = true;
static int config_depth_view_mode = 2;
static bool config_conservative = true;

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
    {1.0f, 0.0f, 0.0f, 1.0f},
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
    "rom:/circle0.sprite",
    "rom:/diamond0.sprite",
    "rom:/pentagon0.sprite",
    "rom:/triangle0.sprite",
};

static sprite_t *sprites[4];

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
    float far_plane = 50.0f;

    glMatrixMode(GL_PROJECTION);
    computeProjectionMatrix(&g_projection, 80.f, aspect_ratio, near_plane, far_plane);
    glLoadIdentity();
    glMultMatrixf(&g_projection.m[0][0]);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, environment_color);
    glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

    float light_radius = 10.0f;

    for (uint32_t i = 0; i < 8; i++) {
        glEnable(GL_LIGHT0 + i);
        glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, light_diffuse[i]);
        glLightf(GL_LIGHT0 + i, GL_LINEAR_ATTENUATION, 2.0f / light_radius);
        glLightf(GL_LIGHT0 + i, GL_QUADRATIC_ATTENUATION, 1.0f / (light_radius * light_radius));
    }

    GLfloat mat_diffuse[] = {1.0f, 1.0f, 1.0f, 1.0f};
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

    for (uint32_t i = 0; i < 4; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);

        glSpriteTextureN64(GL_TEXTURE_2D, sprites[i], &(rdpq_texparms_t){.s.repeats = REPEAT_INFINITE, .t.repeats = REPEAT_INFINITE});
    }

    rdpq_text_register_font(FONT_SCIFI, rdpq_font_load("rom:/FerriteCoreDX.font64"));

    // Convert cube mesh to a 'hull' that's optimized for culling
    occ_mesh_t cube_mesh = {
        .vertices = (vertex_t*)cube_vertices,
        .indices = (uint16_t*)cube_indices,
        .num_vertices = sizeof(cube_vertices) / sizeof(cube_vertices[0]),
        .num_indices = sizeof(cube_indices) / sizeof(cube_indices[0]),
    };

    bool conversion_success = occ_hull_from_flat_mesh(&cube_mesh, &cube_hull);
    if (!conversion_success) {
        assert(false && "Couldn't convert cube mesh!");
        while (true) {}
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
        .vertices = (vertex_t*)cube_vertices,
        .indices = (uint16_t*)cube_indices,
        .num_vertices = sizeof(cube_vertices) / sizeof(cube_vertices[0]),
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
    bool cube_visible = occ_check_target_visible(culler, sw_zbuffer, &cube_mesh, &cube_xform, &cube_target, &raster_query);
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

#define BIG_SCENE_NUM_CUBES (40)

struct
{
    occ_target_t targets[BIG_SCENE_NUM_CUBES];
    struct {
        int num_drawn;
    } stats;
} big_scene = {};

void setup_big_scene()
{
    // memset(&big_scene, 0, sizeof(big_scene));
}

void render_big_scene(surface_t* disp)
{
    long unsigned int anim_timer = 0; // g_num_frames; // HACK

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
        matrix_mult_full(&cube_xforms[i], &translate, &rotate);
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

        big_scene.stats.num_drawn = 0;
        for (int order_i = 0; order_i < num_cubes; order_i++) {
            int idx = cube_order[order_i];
            // debugf("order_i =%d, idx= %d, dist = %f\n", order_i, idx, cube_z_dists[idx]);
            // query for visibility
            // debugf("i=%d\n", i);
            matrix_t *xform = &cube_xforms[idx];
            bool visible = occ_check_target_visible(culler, sw_zbuffer, &cube_hull.mesh, xform, &big_scene.targets[idx], NULL);

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

            // HACK: always draw occluders to SW Z-buffer even if they didn't pass the rough check
            if (true || visible) {
                // also draw to software zbuffer
                //occ_draw_mesh(culler, sw_zbuffer, &cube_hull.mesh, xform);
                occ_draw_hull(culler, sw_zbuffer, &cube_hull, xform);
            }
            if (visible) {
                big_scene.stats.num_drawn++;
            }
        }
    } else {
        big_scene.stats.num_drawn = 0;
        for (int idx = 0; idx < num_cubes; idx++) {
            glPushMatrix();
            glMultMatrixf(&cube_xforms[idx].m[0][0]);
            render_cube();
            glPopMatrix();
            big_scene.stats.num_drawn++;
        }
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

void render_single_cube_scene(surface_t*)
{
    long unsigned int anim_timer = g_num_frames;

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

    bool visible = occ_check_target_visible(culler, sw_zbuffer, &cube_hull.mesh, &xform, &cube_target, NULL);
    (void)visible;

    glPushMatrix();
    glMultMatrixf(&xform.m[0][0]);
    render_cube();
    glPopMatrix();

    //occ_draw_mesh(culler, sw_zbuffer, &cube_hull.mesh, &xform);
    occ_draw_hull(culler, sw_zbuffer, &cube_hull, &xform);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
}

void render()
{
    surface_t *disp = display_get();

    rdpq_attach(disp, &zbuffer);

    gl_context_begin();

    glClearColor(environment_color[0], environment_color[1], environment_color[2], environment_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    occ_next_frame(culler);
    occ_clear_zbuffer(sw_zbuffer);

    glMatrixMode(GL_MODELVIEW);
    compute_camera_matrix(&g_view, &camera);
    // matrix_t mvp;
    // matrix_mult_full(&mvp, &g_projection, &g_view);

    glLoadMatrixf(&g_view.m[0][0]);
    occ_set_view_and_projection(culler, &g_view, &g_projection);

    //render_door_scene(disp);
    render_big_scene(disp);
    //render_2d_scene(disp);
    // render_single_cube_scene(disp);

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
    rdpq_text_printf(NULL, FONT_SCIFI, CULL_W + 8, 40, "visible: %d/%d cubes", big_scene.stats.num_drawn, BIG_SCENE_NUM_CUBES);
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

    joypad_init();

    rspq_profile_start();

#if !DEBUG_RDP
    while (1)
#endif
    {
        uint32_t ticks_start = get_ticks();
        joypad_poll();
        joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
        prof_next_frame();

        if (pressed.a) {
            animation++;
        }

        if (pressed.b) {
            animation--;
        }

        if (pressed.start) {
            debugf("%ld\n", animation);
        }

        if (pressed.r) {
            config_show_wireframe = !config_show_wireframe;
        }

        if (pressed.l) {
            config_show_visible_point = !config_show_visible_point;
        }

        if (pressed.c_up) {
            config_enable_culling = !config_enable_culling;
            make_sphere_mesh();
        }

        if (pressed.c_down) {
            config_depth_view_mode = (config_depth_view_mode + 1) % 3;
        }

        if (pressed.c_right) {
            config_conservative = !config_conservative;
            debugf("conservative rasterization: %s\n", config_conservative ? "ON" : "OFF");
        }

        float y = inputs.stick_y / 128.f;
        float x = inputs.stick_x / 128.f;
        float mag = x * x + y * y;

        if (fabsf(mag) > 0.01f) {
            camera.distance += y * 0.2f;
            camera.rotation = camera.rotation - x * 1.2f;
        }

        render();
        if (DEBUG_RDP)
            rspq_wait();
        
        rspq_flush();
        uint32_t ticks_end = get_ticks();
        if (true) {
            double delta = (ticks_end - ticks_start) / (double)TICKS_PER_SECOND;
            debugf("deltatime: %f ms\n", delta * 1000.0);
            prof_print_stats();
        }
        prof_reset_stats();

        g_num_frames++;
        // debugf("camera.distance=%f; camera.rotation=%f;\n", camera.distance, camera.rotation);
    }
}
