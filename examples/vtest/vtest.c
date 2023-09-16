
#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <libdragon.h>

#include "transforms.h"

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

typedef struct vec3
{
	int x, y, z;
} vec3;

typedef struct vec3f
{
	float x, y, z;
} vec3f;

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

// static int cross2d(vec2 a, vec2 b) {
// 	return a.x * b.y - a.y * b.x;
// }

vec3 cross3d(vec3 a, vec3 b) {
	vec3 c;
	c.x = a.y * b.z - a.z * b.y;
	c.y = a.z * b.x - a.x * b.z;
	c.z = a.x * b.y - a.y * b.x;
	return c;
}

vec3f cross3df(vec3f a, vec3f b) {
	vec3f c;
	c.x = a.y * b.z - a.z * b.y;
	c.y = a.z * b.x - a.x * b.z;
	c.z = a.x * b.y - a.y * b.x;
	return c;
}

vec3 vec3_sub(vec3 a, vec3 b) {
	vec3 c = {a.x-b.x, a.y-b.y, a.z-b.z};
	return c;
}

vec3f vec3f_sub(vec3f a, vec3f b) {
	vec3f c = {a.x-b.x, a.y-b.y, a.z-b.z};
	return c;
}

/* The determinant of a 2D matrix
 * [ a b ]
 * [ c d ]
 **/
//static int det2d(int a, int b, int c, int d)
//{
//	return a*d - b*c;
//}

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

//TODO setup with integer Z
//	TODO scale float Z to integer scale
//TODO write to 16-bit framebuffer. can use surface_t* and RGBA16 or IA16 format
//TODO center the vertex coordinates for more uniform precision?
//TODO pixel center "instead use the “integer + 0.5” convention used by both OpenGL and Direct3D"
//	   The + 0.5 part figures into the initial triangle setup (which now sees slightly different coordinates) but the sampling positions are still on a grid with 1-pixel spacing which is all we need to make this work.
//DONE fill check without barycentrics. we already have wX_row as ints
//DONE subpixel accuracy
//		DONE use integer vertices
//		DONE orient2d made subpixel aware by result >> fractionalbits

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

#define FLOAT_TO_FIXED32(f) (int32_t)(f * 0xffff)
#define FLOAT_TO_U16(f) (uint16_t)(f * 0xffff);
#define U16_TO_FLOAT(u) ((float)u * 0.0002442002f) // Approximately f/0xffff


void draw_tri2(
	vec2 v0,
	vec2 v1,
	vec2 v2,
	float z0,
	float z1,
	float z2,
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

	debugf("\n%s\n", __FUNCTION__);
	debugf("v0: (%d, %d), v1: (%d, %d), v2: (%d, %d)\n",
		v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
	debugf("minb: (%d, %d), maxb: (%d, %d)\n", minb.x, minb.y, maxb.x, maxb.y);

    // Triangle setup
	// Sign flipped when compared to Fabian Giesen's example code to make it work
	// for counter clockwise triangles in screen coordinates with a flipped Y coord.
    int A01 = -(v0.y - v1.y), B01 = -(v1.x - v0.x);
    int A12 = -(v1.y - v2.y), B12 = -(v2.x - v1.x);
    int A20 = -(v2.y - v0.y), B20 = -(v0.x - v2.x);

	debugf("A01: %d\nA12: %d\nA20: %d\n", A01, A12, A20);
	debugf("B01: %d\nB12: %d\nB20: %d\n", B01, B12, B20);

    vec2 p = minb;

	int area2x = -orient2d(v0, v1, v2);

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
	
	// Setup Z interpolation
	// See https://tutorial.math.lamar.edu/classes/calciii/eqnsofplanes.aspx
	// and https://fgiesen.wordpress.com/2011/07/08/a-trip-through-the-graphics-pipeline-2011-part-7/

	vec3f v01 = vec3f_sub((vec3f){v1.x, v1.y, z1}, (vec3f){v0.x, v0.y, z0});
	vec3f v02 = vec3f_sub((vec3f){v2.x, v2.y, z2}, (vec3f){v0.x, v0.y, z0});

	vec3f N = cross3df(v01, v02);

	debugf("v01: (%f, %f, %f)\n", v01.x, v01.y, v01.z);
	debugf("v02: (%f, %f, %f)\n", v02.x, v02.y, v02.z);
	debugf("N: (%f, %f, %f)\n", N.x, N.y, N.z);

	float dzdx = -N.x/N.z;
	float dzdy = -N.y/N.z;

	debugf("dzdx, dxdy: (%f, %f)\n", dzdx, dzdy);

	int32_t dzdx_fixed32 = FLOAT_TO_FIXED32(-N.x/N.z);
	int32_t dzdy_fixed32 = FLOAT_TO_FIXED32(-N.y/N.z);

	// Q: Is Z now perspective correct?
	// A: Yes, see https://fgiesen.wordpress.com/2013/02/11/depth-buffers-done-quick-part/#comment-3892

	// Compute Z at top-left corner of bounding box.
	// Undo fill rule biases already here because they are a constant offset anyway.
	float zf_row = 
		  (w0_row - bias0) * z0
		+ (w1_row - bias1) * z1
		+ (w2_row - bias2) * z2;
	zf_row /= (float)area2x;
	int32_t z_row_fixed32 = FLOAT_TO_FIXED32(zf_row);

	debugf("zf_row: %f\n", zf_row);
	debugf("w0_row: %d\nw1_row: %d\nw2_row: %d\n", w0_row, w1_row, w2_row);
	debugf("bias0: %d\nbias1: %d\nbias2: %d\n", bias0, bias1, bias2);

	float worst_relerror = 0.0f;
	float worst_abserror = 0.0f;

    for (p.y = minb.y; p.y <= maxb.y; p.y++) {
        // Barycentric coordinates at start of row
        int w0 = w0_row;
        int w1 = w1_row;
        int w2 = w2_row;

        float zf_incr = zf_row;
		int32_t z_fixed32 = z_row_fixed32;

        for (p.x = minb.x; p.x <= maxb.x; p.x++) {
			if (
				(p.x == v0.x && p.y == v0.y)
				|| (p.x == v1.x && p.y == v1.y)
				|| (p.x == v2.x && p.y == v2.y)
				) {
				debugf("(%d, %d) = %f\n", p.x, p.y, zf_incr);
			}

			if ((w0|w1|w2) >= 0) {
				// adjust barycentrics after biasing
				float lambda0 = (float)(w0 - bias0) / area2x;
				float lambda1 = (float)(w1 - bias1) / area2x;
				float lambda2 = (float)(w2 - bias2) / area2x;
				// zf_incr: 	Incrementally computed floating point Z
				// zf_bary: 	Barycentric interpolated Z
				// z_fixed32: 	Incrementally computed 16.16 fixed point Z
				// zfi: 		z_fixed32 normalized to floating point
				float zf_bary = lambda0 * z0 + lambda1 * z1 + lambda2 * z2;
				float zfi = z_fixed32 / (float)0xffff;
				float error = (zf_bary - zfi);
				float relerror = error / max(zf_bary, 1e-6);
				worst_relerror = max(worst_relerror, relerror);
				worst_abserror = max(worst_abserror, error);

				#if 0
				int red = lambda0 * 255;
				int green = lambda1 * 255;
				int blue = lambda2 * 255;
				#else
				int red = 0;
				//int green = zf * 255;
				int green = z_fixed32 >> 8;
				int blue = 0;
				#endif

				unsigned int pixelcolor = graphics_make_color(red, green, blue, 255);

				graphics_draw_pixel(screen, p.x, p.y, pixelcolor);
			}

			// One step to the right
            w0 += A12;
            w1 += A20;
            w2 += A01;

            zf_incr += dzdx;
			z_fixed32 += dzdx_fixed32;
        }

        // One row step
        w0_row += B12;
        w1_row += B20;
        w2_row += B01;

        zf_row += dzdy;
		z_row_fixed32 += dzdy_fixed32;
    }

	debugf("worst_relerror: %f %%, worst_abserror: %f\n", 100 * worst_relerror, worst_abserror);
}

#define SUBPIXEL_BITS (2)
#define SUBPIXEL_SCALE (1<<SUBPIXEL_BITS)

static int orient2d_subpixel(vec2 a, vec2 b, vec2 c)
{
	// We multiply two I.F fixed point numbers resulting in (I-F).2F format,
	// so we shift by F to the right to get the the result in I.F format again.
	return ((b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x)) >> SUBPIXEL_BITS;
}

void draw_tri3(
	vec2 v0_in,
	vec2 v1_in,
	vec2 v2_in,
	float Z0f,
	float Z1f,
	float Z2f,
	surface_t* screen,
	unsigned int color)
{
	vec2 minb = {min(v0_in.x, min(v1_in.x, v2_in.x)), min(v0_in.y, min(v1_in.y, v2_in.y))};
	vec2 maxb = {max(v0_in.x, max(v1_in.x, v2_in.x)), max(v0_in.y, max(v1_in.y, v2_in.y))};

	int screen_width = (int)screen->width;
	int screen_height= (int)screen->height;

	if (minb.x < 0) minb.x = 0;
	if (minb.y < 0) minb.y = 0;
	if (maxb.x > screen_width-1) maxb.x = screen_width-1;
	if (maxb.y > screen_height-1) maxb.y = screen_height-1;

	// Round box X coordinate to an even number
	// minb.x = minb.x & (~1);

	// Discard if any of the vertices cross the near plane
	if (Z0f <= 0.f || Z1f <= 0.0f || Z2f <= 0.0f) {
		return;
	}

	// Move origin to center of the screen for more symmetric range.
	vec2 center_ofs = {-(screen->width >> 1), -(screen->height >> 1)};
	vec2 v0 = {v0_in.x + center_ofs.x, v0_in.y + center_ofs.y};
	vec2 v1 = {v1_in.x + center_ofs.x, v1_in.y + center_ofs.y};
	vec2 v2 = {v2_in.x + center_ofs.x, v2_in.y + center_ofs.y};
    vec2 p_start = {minb.x + center_ofs.x, minb.y + center_ofs.y};

	v0 = vec2_muls(v0, SUBPIXEL_SCALE);
	v1 = vec2_muls(v1, SUBPIXEL_SCALE);
	v2 = vec2_muls(v2, SUBPIXEL_SCALE);
    p_start = vec2_muls(p_start, SUBPIXEL_SCALE);

	debugf("\n%s\n", __FUNCTION__);
	debugf("z0f: %f, z1f: %f, z2f: %f\n", Z0f, Z1f, Z2f);
	debugf("v0: (%d, %d), v1: (%d, %d), v2: (%d, %d)\n",
		v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
	debugf("minb: (%d, %d), maxb: (%d, %d)\n", minb.x, minb.y, maxb.x, maxb.y);

    // Triangle setup
	// Sign flipped when compared to Fabian Giesen's example code to make it work
	// for counter clockwise triangles in screen coordinates with a flipped Y coord.
    int A01 = -(v0.y - v1.y), B01 = -(v1.x - v0.x);
    int A12 = -(v1.y - v2.y), B12 = -(v2.x - v1.x);
    int A20 = -(v2.y - v0.y), B20 = -(v0.x - v2.x);

	debugf("A01: %d\nA12: %d\nA20: %d\n", A01, A12, A20);
	debugf("B01: %d\nB12: %d\nB20: %d\n", B01, B12, B20);


	int area2x = -orient2d_subpixel(v0, v1, v2);
	if (area2x <= 0) return;

    // Barycentric coordinates at minX/minY corner

    int w0_row = -orient2d_subpixel(v1, v2, p_start);
    int w1_row = -orient2d_subpixel(v2, v0, p_start);
    int w2_row = -orient2d_subpixel(v0, v1, p_start);

	int bias0 = isTopLeftEdge(v1, v2) ? 0 : -1;
	int bias1 = isTopLeftEdge(v2, v0) ? 0 : -1;
	int bias2 = isTopLeftEdge(v0, v1) ? 0 : -1;

	w0_row += bias0;
	w1_row += bias1;
	w2_row += bias2;
	
	// Setup Z interpolation
	// See https://tutorial.math.lamar.edu/classes/calciii/eqnsofplanes.aspx
	// and https://fgiesen.wordpress.com/2011/07/08/a-trip-through-the-graphics-pipeline-2011-part-7/

	vec3f v01 = vec3f_sub((vec3f){v1.x, v1.y, Z1f}, (vec3f){v0.x, v0.y, Z0f});
	vec3f v02 = vec3f_sub((vec3f){v2.x, v2.y, Z2f}, (vec3f){v0.x, v0.y, Z0f});

	vec3f N = cross3df(v01, v02);

	debugf("v01: (%f, %f, %f)\n", v01.x, v01.y, v01.z);
	debugf("v02: (%f, %f, %f)\n", v02.x, v02.y, v02.z);
	debugf("N: (%f, %f, %f)\n", N.x, N.y, N.z);

	float dZdx = -N.x/N.z;
	float dZdy = -N.y/N.z;

	debugf("dZdx, dZdy: (%f, %f)\n", dZdx, dZdy);

	int32_t dZdx_fixed32 = FLOAT_TO_FIXED32(-N.x/N.z);
	int32_t dZdy_fixed32 = FLOAT_TO_FIXED32(-N.y/N.z);

	// Q: Is Z now perspective correct?
	// A: Yes, see https://fgiesen.wordpress.com/2013/02/11/depth-buffers-done-quick-part/#comment-3892

	// Compute Z at top-left corner of bounding box.
	// Undo fill rule biases already here because they are a constant offset anyway.
	float zf_row = 
		  (w0_row - bias0) * Z0f
		+ (w1_row - bias1) * Z1f
		+ (w2_row - bias2) * Z2f;
	zf_row /= (float)area2x;
	int32_t Z_row_fixed32 = FLOAT_TO_FIXED32(zf_row);

	debugf("zf_row: %f\n", zf_row);
	debugf("w0_row: %d\nw1_row: %d\nw2_row: %d\n", w0_row, w1_row, w2_row);
	debugf("bias0: %d\nbias1: %d\nbias2: %d\n", bias0, bias1, bias2);

	float worst_relerror = 0.0f;
	float worst_abserror = 0.0f;

	// Only 'p', 'minb' and 'maxb' are in whole-pixel coordinates here. Others all in sub-pixel scale.
	vec2 p={-1,-1};

    for (p.y = minb.y; p.y <= maxb.y; p.y++) {
        // Barycentric coordinates at start of row
        int w0 = w0_row;
        int w1 = w1_row;
        int w2 = w2_row;

        float Zf_incr = zf_row;
		int32_t Z_fixed32 = Z_row_fixed32;

        for (p.x = minb.x; p.x <= maxb.x; p.x++) {
			if (
				(p.x == v0.x && p.y == v0.y)
				|| (p.x == v1.x && p.y == v1.y)
				|| (p.x == v2.x && p.y == v2.y)
				) {
				debugf("(%d, %d) = %f\n", p.x, p.y, Zf_incr);
			}

			if ((w0|w1|w2) >= 0) {
				// adjust barycentrics after biasing
				float lambda0 = (float)(w0 - bias0) / area2x;
				float lambda1 = (float)(w1 - bias1) / area2x;
				float lambda2 = (float)(w2 - bias2) / area2x;
				// zf_incr: 	Incrementally computed floating point Z
				// zf_bary: 	Barycentric interpolated Z
				// z_fixed32: 	Incrementally computed 16.16 fixed point Z
				// zfi: 		z_fixed32 normalized to floating point
				float Zf_bary = lambda0 * Z0f + lambda1 * Z1f + lambda2 * Z2f;
				float Zfi = Z_fixed32 / (float)0xffff;
				float error = (Zf_bary - Zfi);
				float relerror = error / max(Zf_bary, 1e-6);
				worst_relerror = max(worst_relerror, relerror);
				worst_abserror = max(worst_abserror, error);

				#if 0
				int red = lambda0 * 255;
				int green = lambda1 * 255;
				int blue = lambda2 * 255;
				#else
				int red = 0;
				//int green = zf * 255;
				int green = Z_fixed32 >> 8;
				int blue = 0;
				#endif

				unsigned int pixelcolor = graphics_make_color(red, green, blue, 255);

				graphics_draw_pixel(screen, p.x, p.y, pixelcolor);
			}

			// One step to the right
            w0 += A12;
            w1 += A20;
            w2 += A01;

            Zf_incr += dZdx;
			Z_fixed32 += dZdx_fixed32;
        }

        // One row step
        w0_row += B12;
        w1_row += B20;
        w2_row += B01;

        zf_row += dZdy;
		Z_row_fixed32 += dZdy_fixed32;
    }

	debugf("worst_relerror: %f %%, worst_abserror: %f\n", 100 * worst_relerror, worst_abserror);
}

void draw_cube(display_context_t dc)
{
	cpu_glViewport(0, 0, display_get_width(), display_get_height(), display_get_width(), display_get_height());
	cpu_glDepthRange(0, 1);

    const float aspect_ratio = (float)display_get_width() / (float)display_get_height();
    const float near_plane = 1.0f;
    const float far_plane = 50.0f;

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

	for (int is=0; is < sizeof(cube_indices)/sizeof(cube_indices[0]); is+=3) {
		const uint16_t* inds = &cube_indices[is];
		vtx_t verts[3] = {0};

		for (int i=0;i<3;i++) {
			verts[i].obj_attributes = cube_vertices[inds[i]];
			verts[i].obj_attributes.position[3] = 1.0f; // TODO where does cpu pipeline set this?
			// TODO miksi w on 1.1 cs_posissa? luulisi ett' se oli z eik' y
			// 		Vastaus: se on z, mutta z:n etumerkki flipataan projektiomatriisissa
			debugf("i=%d, pos[3] = %f\n", i, verts[i].obj_attributes.position[3]);
		}

		for (int i=0;i<3;i++) {
			vertex_pre_tr(&verts[i], &mvp);
			vertex_calc_screenspace(&verts[i]);
		}

        if (config.verbose_setup) {
            debugf("pos=(%f, %f, %f, %f), cs_pos=(%f, %f, %f, %f), clip_code=%d\n",
                   verts[0].obj_attributes.position[0],
                   verts[0].obj_attributes.position[1],
                   verts[0].obj_attributes.position[2],
                   verts[0].obj_attributes.position[3],
                   verts[0].cs_pos[0],
                   verts[0].cs_pos[1],
                   verts[0].cs_pos[2],
                   verts[0].cs_pos[3],
                   verts[0].clip_code);
            debugf("screen_pos: (%f, %f), depth=%f, inv_w=%f\n",
                   verts[0].screen_pos[0],
                   verts[0].screen_pos[1],
                   verts[0].depth,
                   verts[0].inv_w);
        }

        vec2 screenverts[3];
		float screenzs[3];
		for (int i=0;i<3;i++) {
			screenverts[i].x = verts[i].screen_pos[0]; // FIXME: float2int conversion?
			screenverts[i].y = verts[i].screen_pos[1];
			screenzs[i] = verts[i].depth;
		}

		draw_tri3(
			screenverts[0], screenverts[1], screenverts[2],
			screenzs[0], screenzs[1], screenzs[2],
			dc, graphics_make_color(255,0,255,255));


	}
}

int main(void)
{
    display_context_t _dc;
	int res = 0;
	unsigned short buttons, previous = 0;

	config.wait_for_press = false;
	config.verbose_setup = false;

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

		color = graphics_make_color(0x00, 0x00, 0x00, 0xFF);
		graphics_set_color(color, 0);

		draw_cube(_dc);

        unlockVideo(_dc);

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
