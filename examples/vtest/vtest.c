
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
} vec2;

vec2 vec2_add(vec2 a, vec2 b) {
	vec2 c = {a.x+b.x, a.y+b.y};
	return c;
}

vec2 vec2_sub(vec2 a, vec2 b) {
	vec2 c = {a.x-b.x, a.y-b.y};
	return c;
}

vec2 vec2_mul(vec2 a, vec2 b) {
	vec2 c = {a.x*b.x, a.y*b.y};
	return c;
}

vec2 vec2_muls(vec2 a, float s) {
	vec2 c = {a.x*s, a.y * s};
	return c;
}

vec2 vec2_div(vec2 a, vec2 b) {
	vec2 c = {a.x/b.x, a.y/b.y};
	return c;
}

vec2 vec2_divs(vec2 a, int s) {
	vec2 c = {a.x/s, a.y/s};
	return c;
}

static int cross2d(vec2 a, vec2 b) {
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

// Need x+y+1 bits to represent this result, where x and y are bit widths
// of the two input coordinates.
// Source: Ben Pye at https://fgiesen.wordpress.com/2013/02/08/triangle-rasterization-in-practice/#comment-21823
static int orient2d(vec2 a, vec2 b, vec2 c)
{
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
//TODO center the vertex coordinates for more uniform precision?
//TODO pixel center

bool isTopLeftEdge(vec2 a, vec2 b)
{
	// "In a counter-clockwise triangle, a top edge is an edge that is exactly horizontal
	//  and goes towards the left, i.e. its end point is left of its start point."
	if (a.y == b.y && b.x < a.x) return true;

	// "In a counter-clockwise triangle, a left edge is an edge that goes down,
	//  i.e. its end point is strictly below its start point."

	// But note that on the screen Y axis grows downwards so we check if the end point
	// Y coordinate is greater than start point Y.
	if (b.y > a.y) return true;
	return false;
}

bool isTopLeftEdgeTextbook(vec2 a, vec2 b)
{
	// "In a counter-clockwise triangle, a top edge is an edge that is exactly horizontal
	//  and goes towards the left, i.e. its end point is left of its start point."
	if (a.y == b.y && b.x < a.x) return true;

	// "In a counter-clockwise triangle, a left edge is an edge that goes down,
	//  i.e. its end point is strictly below its start point."
	if (b.y < a.y) return true;
	return false;
}


void draw_tri(
	vec2 v0,
	vec2 v1,
	vec2 v2,
	surface_t* screen,
	unsigned int color)
{
	vec2 minb = {min(v0.x, min(v1.x, v2.x)), min(v0.y, min(v1.y, v2.y))};
	vec2 maxb = {max(v0.x, max(v1.x, v2.x)), max(v0.y, max(v1.y, v2.y))};

	int screen_width = (int)display_get_width();
	int screen_height= (int)display_get_height();

	if (minb.x < 0) minb.x = 0;
	if (minb.y < 0) minb.y = 0;
	if (maxb.x > screen_width-1) maxb.x = screen_width-1;
	if (maxb.y > screen_height-1) maxb.y = screen_height-1;

	// In screen coordinates the Y-axis grows down so a counter clockwise
	// wound triangle area is always negative. To make the areas positive
	// we swap two vertices when computing orient2d(), effectively 
	// flipping the winding of the triangle.

	// The "isTopLeftEdge" function is written for counter-clockwise triangles.
	// But our inputs are effectively clockwise thanks to the flipped Y axis!
	// So let's swap the edge directions when calling "isTopLeftEdge" to compensate.
	// I.e. we pass in (v1, v0) instead of (v0, v1).

	int bias0 = isTopLeftEdge(v1, v2) ? 0 : -1;
	int bias1 = isTopLeftEdge(v2, v0) ? 0 : -1;
	int bias2 = isTopLeftEdge(v0, v1) ? 0 : -1;

	#if 0
	v0.y *= -1;
	v1.y *= -1;
	v2.y *= -1;
	int bias0b = isTopLeftEdgeTextbook(v1, v2) ? 0 : -1;
	int bias1b = isTopLeftEdgeTextbook(v2, v0) ? 0 : -1;
	int bias2b = isTopLeftEdgeTextbook(v0, v1) ? 0 : -1;

	debugf("0: %d vs %d\n1: %d vs %d\n2: %d vs %d\n",
		bias0, bias0b, bias1, bias1b, bias2, bias2b);

	v0.y *= -1;
	v1.y *= -1;
	v2.y *= -1;
	#endif

	int area = orient2d(v0, v2, v1) / 2;
	debugf("Area: %d\n", area);

	for (int y=minb.y;y<=maxb.y;y++) {
		for (int x=minb.x;x<=maxb.x;x++) {
			vec2 p = {x,y};
			int w0 = orient2d(v2, v1, p) + bias0;
			int w1 = orient2d(v0, v2, p) + bias1;
			int w2 = orient2d(v1, v0, p) + bias2;

			if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
				//FIXME: adjust barycentrics after biasing
				graphics_draw_pixel(screen, x, y, color);
			}
		}
	}

}

bool isTopLeftScreenClockwise(vec2 a, vec2 b)
{
	if (a.y == b.y && b.x > a.x) return true;
	if (b.y < a.y) return true;
	return false;
}


void draw_tri2(
	vec2 v0,
	vec2 v1,
	vec2 v2,
	surface_t* screen,
	unsigned int color)
{
	vec2 minb = {min(v0.x, min(v1.x, v2.x)), min(v0.y, min(v1.y, v2.y))};
	vec2 maxb = {max(v0.x, max(v1.x, v2.x)), max(v0.y, max(v1.y, v2.y))};

	int screen_width = (int)display_get_width();
	int screen_height= (int)display_get_height();

	if (minb.x < 0) minb.x = 0;
	if (minb.y < 0) minb.y = 0;
	if (maxb.x > screen_width-1) maxb.x = screen_width-1;
	if (maxb.y > screen_height-1) maxb.y = screen_height-1;

	debugf("v0: (%d, %d), v1: (%d, %d), v2: (%d, %d)\n",
		v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
	debugf("minb: (%d, %d), maxb: (%d, %d)\n", minb.x, minb.y, maxb.x, maxb.y);

    // Triangle setup
    int A01 = -(v0.y - v1.y), B01 = -(v1.x - v0.x);
    int A12 = -(v1.y - v2.y), B12 = -(v2.x - v1.x);
    int A20 = -(v2.y - v0.y), B20 = -(v0.x - v2.x);

	debugf("A01: %d\nA12: %d\nA20: %d\n", A01, A12, A20);
	debugf("B01: %d\nB12: %d\nB20: %d\n", B01, B12, B20);

    vec2 p = minb;

    // Barycentric coordinates at minX/minY corner

    int w0_row = -orient2d(v1, v2, p);
    int w1_row = -orient2d(v2, v0, p);
    int w2_row = -orient2d(v0, v1, p);

	int bias0 = isTopLeftEdge(v1, v2) ? 0 : -1;
	int bias1 = isTopLeftEdge(v2, v0) ? 0 : -1;
	int bias2 = isTopLeftEdge(v0, v1) ? 0 : -1;

	w0_row += bias0;
	w1_row += bias1;
	w2_row += bias2;

	debugf("w0_row: %d\nw1_row: %d\nw2_row: %d\n", w0_row, w1_row, w2_row);
	debugf("bias0: %d\nbias1: %d\nbias2: %d\n", bias0, bias1, bias2);

	// In screen coordinates the Y-axis grows down so a counter clockwise
	// wound triangle area is always negative. To make the areas positive
	// we swap two vertices when computing orient2d(), effectively 
	// flipping the winding of the triangle.

    for (p.y = minb.y; p.y <= maxb.y; p.y++) {
        // Barycentric coordinates at start of row
        int w0 = w0_row;
        int w1 = w1_row;
        int w2 = w2_row;

        for (p.x = minb.x; p.x <= maxb.x; p.x++) {
			// debugf("p=(%d,%d)\n", p.x, p.y);
			if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
				//FIXME: adjust barycentrics after biasing
				graphics_draw_pixel(screen, p.x, p.y, color);
			}

			// One step to the right
            w0 += A12;
            w1 += A20;
            w2 += A01;
        }

        // One row step
        w0_row += B12;
        w1_row += B20;
        w2_row += B01;
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

		vec2 v0 = {100, 30};
		vec2 v1 = {20, 30};
		vec2 v2 = {135, 150};
		vec2 v3 = {155, 40};

		draw_tri2( v0, v1, v2, _dc, graphics_make_color(255,0,255,255));
		draw_tri2( v2, v3, v0, _dc, graphics_make_color(0,255,0,255));

		color = graphics_make_color(0x00, 0x00, 0x00, 0xFF);
		graphics_set_color(color, 0);


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
