#include "libdragon.h"
#include <malloc.h>
#include <math.h>

static float rot_angle = 0.0f;

void render()
{
    surface_t *disp = display_lock();
    if (!disp)
    {
        return;
    }

    uint32_t black = graphics_make_color(0, 0, 0, 255);
    uint32_t red = graphics_make_color(255, 0, 0, 255);

    graphics_fill_screen(disp, black);

    float w = display_get_width();
    float h = display_get_height();
    float centerx = w/2.0f;
    float centery = h/2.0f;

    float size = 50 + 50.0 * sin(rot_angle * 2);

    float corners[4][2];
    for (int i = 0; i < 4; i++)
    {
        float my_angle = rot_angle + (M_PI / 2.0) * i;
        corners[i][0] = centerx + cos(my_angle) * size;
        corners[i][1] = centery + sin(my_angle) * size;
    }
    
    rdp_attach(disp);
    rdp_enable_blend_fill();
    rdp_set_blend_color(red);

    // Draw a quad made of triangles triangles 0-1-2 and 0-2-3
    rdp_draw_filled_triangle(corners[0][0], corners[0][1], corners[1][0], corners[1][1], corners[2][0], corners[2][1]);
    rdp_draw_filled_triangle(corners[0][0], corners[0][1], corners[2][0], corners[2][1], corners[3][0], corners[3][1]);

    rspq_wait();
    
    rdp_auto_show_display(disp);

    rot_angle += 0.01f;
}

int main()
{
    display_init(RESOLUTION_320x240, DEPTH_32_BPP, 3, GAMMA_NONE, ANTIALIAS_RESAMPLE);

    debug_init_isviewer();
    debug_init_usblog();

    controller_init();
    timer_init();

    dfs_init(DFS_DEFAULT_LOCATION);

    rdp_init();
    
    while (1)
    {
        render();

        controller_scan();
        struct controller_data ckeys = get_keys_down();
        (void)ckeys;
    }
}
