
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <libdragon.h>

#include "occlusion.h"

/* hardware definitions */
// Pad buttons
#define A_BUTTON(a)     ((a) & 0x8000)
#define B_BUTTON(a)     ((a) & 0x4000)
#define Z_BUTTON(a)     ((a) & 0x2000)
#define START_BUTTON(a) ((a) & 0x1000)

// D-Pad
#define DU_BUTTON(a)    ((a) & 0x0800)
#define DD_BUTTON(a)    ((a) & 0x0400)
#define DL_BUTTON(a)    ((a) & 0x0200)
#define DR_BUTTON(a)    ((a) & 0x0100)

// Triggers
#define TL_BUTTON(a)    ((a) & 0x0020)
#define TR_BUTTON(a)    ((a) & 0x0010)

// Yellow C buttons
#define CU_BUTTON(a)    ((a) & 0x0008)
#define CD_BUTTON(a)    ((a) & 0x0004)
#define CL_BUTTON(a)    ((a) & 0x0002)
#define CR_BUTTON(a)    ((a) & 0x0001)

#define PAD_DEADZONE     5
#define PAD_ACCELERATION 10
#define PAD_CHECK_TIME   40


unsigned short gButtons = 0;
struct controller_data gKeys;
static int global_frame_num = 0;

volatile int gTicks;                    /* incremented every vblank */

static occ_culler_t* g_culler;

static struct {
	bool wait_for_press;
	bool verbose_setup;
} config;

/* input - do getButtons() first, then getAnalogX() and/or getAnalogY() */
unsigned short getButtons(int pad)
{
    // Read current controller status
    controller_scan();
    gKeys = get_keys_pressed();
    return (unsigned short)(gKeys.c[0].data >> 16);
}

unsigned char getAnalogX(int pad)
{
    return (unsigned char)gKeys.c[pad].x;
}

unsigned char getAnalogY(int pad)
{
    return (unsigned char)gKeys.c[pad].y;
}

display_context_t lockVideo(int wait)
{
    display_context_t dc;

    if (wait)
        dc = display_get();
    else
        dc = display_try_get();
    return dc;
}

void unlockVideo(display_context_t dc)
{
    if (dc)
        display_show(dc);
}

/* text functions */
void drawText(display_context_t dc, char *msg, int x, int y)
{
    if (dc)
        graphics_draw_text(dc, x, y, msg);
}

void printText(display_context_t dc, char *msg, int x, int y)
{
    if (dc)
        graphics_draw_text(dc, x*8, y*8, msg);
}

/* vblank callback */
void vblCallback(void)
{
    gTicks++;
}

void delay(int cnt)
{
    int then = gTicks + cnt;
    while (then > gTicks) ;
}

/* initialize console hardware */
void init_n64(void)
{
    /* Initialize peripherals */
    display_init( RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE );

    register_VI_handler(vblCallback);

    controller_init();
}


void draw_cube(surface_t* zbuffer)
{
	cpu_glViewport(0, 0, zbuffer->width, zbuffer->height, zbuffer->width, zbuffer->height);
	cpu_glDepthRange(0, 1);

    const float aspect_ratio = (float)zbuffer->width / (float)zbuffer->height;
    const float near_plane = 1.0f;
    const float far_plane = 20.0f;

	matrix_t proj = cpu_glFrustum(-near_plane*aspect_ratio, near_plane*aspect_ratio, -near_plane, near_plane, near_plane, far_plane);
	matrix_t translation = cpu_glTranslatef(0.0f, 0.0f, -3.0f);
	matrix_t rotation = cpu_glRotatef(45.f + 1.f * global_frame_num, 0.0f, 1.0f, 0.0f);
	matrix_t view;
	matrix_mult_full(&view, &translation, &rotation);
	matrix_t mvp = cpu_glLoadIdentity();
	matrix_mult_full(&mvp, &proj, &view);

	if (config.verbose_setup) {
		debugf("Projection matrix:\n");
		for (int i=0;i<4;i++) {
			for (int j=0;j<4;j++) {
				debugf("%f ", mvp.m[i][j]);
			}
			debugf("\n");
		}
	}

	occ_clear_zbuffer(zbuffer);
	occ_set_projection_matrix(g_culler, proj);
	occ_set_mvp_matrix(g_culler, mvp);

	occ_draw_indexed_mesh(g_culler,
		zbuffer,
		&cube_vertices[0],
		cube_indices, sizeof(cube_indices)/sizeof(cube_indices[0]));
}

int main(void)
{
    surface_t* screen;
	int res = 0;
	unsigned short buttons, previous = 0;

	config.wait_for_press = false;
	config.verbose_setup = false;

	debug_init_usblog();
	debug_init_isviewer();
    init_n64();
	rdpq_init();

	surface_t sw_zbuffer = surface_alloc(FMT_RGBA16, 160, 120);
	g_culler = occ_culler_alloc(0, 0, sw_zbuffer.width, sw_zbuffer.height);

    while (1)
    {
		draw_cube(&sw_zbuffer);

		int width[6] = { 320, 640, 256, 512, 512, 640 };
		int height[6] = { 240, 480, 240, 480, 240, 240 };
		unsigned int color;

        screen = lockVideo(1);
		color = graphics_make_color(0xCC, 0xCC, 0xCC, 0xFF);
		graphics_fill_screen(screen, color);

		color = graphics_make_color(0xFF, 0xFF, 0xFF, 0xFF);
		graphics_draw_line(screen, 0, 0, width[res]-1, 0, color);
		graphics_draw_line(screen, width[res]-1, 0, width[res]-1, height[res]-1, color);
		graphics_draw_line(screen, width[res]-1, height[res]-1, 0, height[res]-1, color);
		graphics_draw_line(screen, 0, height[res]-1, 0, 0, color);

		graphics_draw_line(screen, 0, 0, width[res]-1, height[res]-1, color);
		graphics_draw_line(screen, 0, height[res]-1, width[res]-1, 0, color);

		color = graphics_make_color(0x00, 0x00, 0x00, 0xFF);
		graphics_set_color(color, 0);

		rdpq_attach(screen, NULL);
		// rdpq_set_mode_copy(true);
		rdpq_set_mode_standard(); // Can't use copy mode if we need a 16-bit -> 32-bit conversion
		rdpq_tex_blit(&sw_zbuffer, 0, 0, NULL);
		rdpq_detach();

        unlockVideo(screen);


		if (config.wait_for_press) {
			while (1)
			{
				// wait for change in buttons
				buttons = getButtons(0);
				if (buttons ^ previous)
					break;
				delay(1);
			}
		}

		global_frame_num++;
        previous = buttons;
    }

    return 0;
}
