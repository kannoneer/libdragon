
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <libdragon.h>

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

volatile int gTicks;                    /* incremented every vblank */

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
    display_init( RESOLUTION_320x240, DEPTH_32_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE );

    register_VI_handler(vblCallback);

    controller_init();
}

// statement expressions: https://stackoverflow.com/a/58532788
#define max(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a > _b ? _a : _b;       \
})

#define min(a,b)             \
({                           \
    __typeof__ (a) _a = (a); \
    __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b;       \
})


typedef struct vec2
{
	int x, y;
} vec2_t;

vec2_t vec2_add(vec2_t a, vec2_t b) {
	vec2_t c = {a.x+b.x, a.y+b.y};
	return c;
}

vec2_t vec2_sub(vec2_t a, vec2_t b) {
	vec2_t c = {a.x-b.x, a.y-b.y};
	return c;
}

vec2_t vec2_mul(vec2_t a, vec2_t b) {
	vec2_t c = {a.x*b.x, a.y*b.y};
	return c;
}

vec2_t vec2_muls(vec2_t a, float s) {
	vec2_t c = {a.x*s, a.y * s};
	return c;
}

vec2_t vec2_div(vec2_t a, vec2_t b) {
	vec2_t c = {a.x/b.x, a.y/b.y};
	return c;
}

vec2_t vec2_divs(vec2_t a, int s) {
	vec2_t c = {a.x/s, a.y/s};
	return c;
}

static int cross2d(vec2_t a, vec2_t b) {
	return a.x * b.y - a.y * b.x;
}

/* The determinant of a 2D matrix
 * [ a b ]
 * [ c d ]
 **/
static int det2d(int a, int b, int c, int d)
{
	return a*d - b*c;
}

static int orient2d(vec2_t a, vec2_t b, vec2_t c) {
	// The 2D cross product of vectors a->b and a->p
	// See https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/
	return (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
	//return det2d(b.x - a.x, c.x - a.x,
	//             b.y - a.y, c.y - a.y);
	// vec2_t ab = vec2_sub(b, a);
	// vec2_t ac = vec2_sub(c, a);
	// return cross2d(ab, ac);
}

//TODO subpixel accuracy

// In screen coordinates the Y-axis grows down so a counter clockwise
// wound triangle area is always negative. To make the areas positive
// we swap two vertices when computing orient2d(), effectively 
// flipping the winding of the triangle.

void draw_tri(
	vec2_t v0,
	vec2_t v1,
	vec2_t v2,
	surface_t* screen,
	unsigned int color)
{
	vec2_t minb = {min(v0.x, min(v1.x, v2.x)), min(v0.y, min(v1.y, v2.y))};
	vec2_t maxb = {max(v0.x, max(v1.x, v2.x)), max(v0.y, max(v1.y, v2.y))};

	int screen_width = (int)display_get_width();
	int screen_height= (int)display_get_height();

	if (minb.x < 0) minb.x = 0;
	if (minb.y < 0) minb.y = 0;
	if (maxb.x > screen_width-1) maxb.x = screen_width-1;
	if (maxb.y > screen_height-1) maxb.y = screen_height-1;

	int area = orient2d(v0, v2, v1) / 2;
	debugf("Area: %d\n", area);

	for (int y=minb.y;y<=maxb.y;y++) {
		for (int x=minb.x;x<=maxb.x;x++) {
			vec2_t p = {x,y};
			int w0 = orient2d(v2, v1, p);
			int w1 = orient2d(v0, v2, p);
			int w2 = orient2d(v1, v0, p);

			if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
				graphics_draw_pixel(screen, x, y, color);
			}
		}
	}

}

#define INT_TO_FIXED16(x) (x<<16)

int main(void)
{
    display_context_t _dc;
    char temp[128];
	int res = 0;
	unsigned short buttons, previous = 0;

	debug_init_usblog();
	debug_init_isviewer();
    init_n64();

    while (1)
    {
		int j;
		int width[6] = { 320, 640, 256, 512, 512, 640 };
		int height[6] = { 240, 480, 240, 480, 240, 240 };
		unsigned int color;

        _dc = lockVideo(1);
		color = graphics_make_color(0xCC, 0xCC, 0xCC, 0xFF);
		graphics_fill_screen(_dc, color);

		color = graphics_make_color(0xFF, 0xFF, 0xFF, 0xFF);
		graphics_draw_line(_dc, 0, 0, width[res]-1, 0, color);
		graphics_draw_line(_dc, width[res]-1, 0, width[res]-1, height[res]-1, color);
		graphics_draw_line(_dc, width[res]-1, height[res]-1, 0, height[res]-1, color);
		graphics_draw_line(_dc, 0, height[res]-1, 0, 0, color);

		graphics_draw_line(_dc, 0, 0, width[res]-1, height[res]-1, color);
		graphics_draw_line(_dc, 0, height[res]-1, width[res]-1, 0, color);

		color = graphics_make_color(255,0,255,255);
		draw_tri(
			(vec2_t){100, 30},
			(vec2_t){20, 60},
			(vec2_t){105, 150},
			_dc,
			color);

		color = graphics_make_color(0x00, 0x00, 0x00, 0xFF);
		graphics_set_color(color, 0);


        printText(_dc, "Video Resolution Test", width[res]/16 - 10, 3);
		switch (res)
		{
			case 0:
				printText(_dc, "320x240p", width[res]/16 - 3, 5);
				break;
			case 1:
				printText(_dc, "640x480i", width[res]/16 - 3, 5);
				break;
			case 2:
				printText(_dc, "256x240p", width[res]/16 - 3, 5);
				break;
			case 3:
				printText(_dc, "512x480i", width[res]/16 - 3, 5);
				break;
			case 4:
				printText(_dc, "512x240p", width[res]/16 - 3, 5);
				break;
			case 5:
				printText(_dc, "640x240p", width[res]/16 - 3, 5);
				break;
		}

		for (j=0; j<8; j++)
		{
			sprintf(temp, "Line %d", j);
			printText(_dc, temp, 3, j);
			sprintf(temp, "Line %d", height[res]/8 - j - 1);
			printText(_dc, temp, 3, height[res]/8 - j - 1);
		}
		printText(_dc, "0123456789", 0, 16);
		printText(_dc, "9876543210", width[res]/8 - 10, 16);

        unlockVideo(_dc);

        while (1)
        {
            // wait for change in buttons
            buttons = getButtons(0);
            if (buttons ^ previous)
                break;
            delay(1);
        }

        if (A_BUTTON(buttons ^ previous))
        {
            // A changed
            if (!A_BUTTON(buttons))
			{
				resolution_t mode[6] = {
					RESOLUTION_320x240,
					RESOLUTION_640x480,
					RESOLUTION_256x240,
					RESOLUTION_512x480,
					RESOLUTION_512x240,
					RESOLUTION_640x240,
				};
				res++;
				res %= 6;
				display_close();
				display_init(mode[res], DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_RESAMPLE);
			}
		}

        previous = buttons;
    }

    return 0;
}
