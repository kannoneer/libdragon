// occult
// Z-Buffer Renderer and Occlusion Tester Library
// 
//TODO setup with integer Z
//	TODO scale float Z to integer scale
//TODO write to 16-bit framebuffer. can use surface_t* and RGBA16 or IA16 format
// TODO compute output buffer incrementally and not via multiplication
//TODO pixel center "instead use the “integer + 0.5” convention used by both OpenGL and Direct3D"
//	   The + 0.5 part figures into the initial triangle setup (which now sees slightly different coordinates) but the sampling positions are still on a grid with 1-pixel spacing which is all we need to make this work.
//		Q: N64 follows D3D9 convention with top left corner being (0,0)?
//TODO flip orient2d winding instead of negating returned signed area
//TODO support other depth range than (0,1)
//DONE center the vertex coordinates for more uniform precision?
//DONE fill check without barycentrics. we already have wX_row as ints
//DONE subpixel accuracy
//		DONE use integer vertices
//		DONE orient2d made subpixel aware by result >> fractionalbits

#include "cpumath.h"
#include "transforms.h" // for vertex_t
#include <surface.h>
#include <n64types.h>
#include <memory.h>
#include <malloc.h>

#define SUBPIXEL_BITS (2)
#define SUBPIXEL_SCALE (1<<SUBPIXEL_BITS)

#define FLOAT_TO_FIXED32(f) (int32_t)(f * 0xffff)
#define FLOAT_TO_U16(f) (uint16_t)(f * 0xffff);
#define U16_TO_FLOAT(u) ((float)u * 0.0002442002f) // Approximately f/0xffff

struct occ_culler_s {
    struct {
        int x;
        int y;
        int width;
        int height;
    } viewport;
    matrix_t proj;
    matrix_t mvp;
};

typedef struct occ_culler_s occ_culler_t;

occ_culler_t* occ_culler_alloc(int x, int y, int width, int height) {
    occ_culler_t* culler = malloc(sizeof(occ_culler_t));
    return culler;
}

void occ_culler_free(occ_culler_t* culler) {
    free(culler);
}

void occ_set_projection_matrix(occ_culler_t* culler, matrix_t proj)
{
    culler->proj = proj;
}

void occ_set_mvp_matrix(occ_culler_t* culler, matrix_t mvp)
{
    culler->mvp = mvp;
}

void occ_clear_zbuffer(surface_t* zbuffer) {
	// 0xffff = max depth
	for (int y=0;y<zbuffer->height;y++) {
		memset(zbuffer->buffer + zbuffer->stride * y, 0xff, zbuffer->stride);
	}
}

#if 0
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
#endif

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
	surface_t* zbuffer)
{
	vec2 minb = {min(v0_in.x, min(v1_in.x, v2_in.x)), min(v0_in.y, min(v1_in.y, v2_in.y))};
	vec2 maxb = {max(v0_in.x, max(v1_in.x, v2_in.x)), max(v0_in.y, max(v1_in.y, v2_in.y))};

	if (minb.x < 0) minb.x = 0;
	if (minb.y < 0) minb.y = 0;
	if (maxb.x > zbuffer->width-1) maxb.x = zbuffer->width-1;
	if (maxb.y > zbuffer->height-1) maxb.y = zbuffer->height-1;

	// Round box X coordinate to an even number
	// minb.x = minb.x & (~1);

	// Discard if any of the vertices cross the near plane
	if (Z0f <= 0.f || Z1f <= 0.0f || Z2f <= 0.0f) {
		return;
	}

	// Move origin to center of the screen for more symmetric range.
	vec2 center_ofs = {-(zbuffer->width >> 1), -(zbuffer->height >> 1)};
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
	float Zf_row = 
		  (w0_row - bias0) * Z0f
		+ (w1_row - bias1) * Z1f
		+ (w2_row - bias2) * Z2f;
	Zf_row /= (float)area2x;
	int32_t Z_row_fixed32 = FLOAT_TO_FIXED32(Zf_row);

	debugf("zf_row: %f\n", Zf_row);
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

        float Zf_incr = Zf_row;
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
				// int red = 0;
				// int green = Z_fixed32 >> 8;
				// int blue = 0;
				#endif

				uint16_t depth = Z_fixed32; // TODO don't we want top 16 bits?
				u_uint16_t* output = (u_uint16_t*)(zbuffer->buffer + (zbuffer->stride * p.y + p.x * sizeof(uint16_t)));
				*output = depth;
				//*output = depth;
				//unsigned int pixelcolor = graphics_make_color(red, green, blue, 255);
				//graphics_draw_pixel(screen, p.x, p.y, pixelcolor);
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

        Zf_row += dZdy;
		Z_row_fixed32 += dZdy_fixed32;
    }

	debugf("worst_relerror: %f %%, worst_abserror: %f\n", 100 * worst_relerror, worst_abserror);
}

void occ_draw_indexed_mesh(occ_culler_t* occ, surface_t* zbuffer, const vertex_t* cube_vertices, const uint16_t* cube_indices, uint32_t num_indices)
{
	for (int is=0; is < num_indices; is+=3) {
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
			vertex_pre_tr(&verts[i], &occ->mvp);
			vertex_calc_screenspace(&verts[i]);
		}

        //if (config.verbose_setup) {
        //    debugf("pos=(%f, %f, %f, %f), cs_pos=(%f, %f, %f, %f), clip_code=%d\n",
        //           verts[0].obj_attributes.position[0],
        //           verts[0].obj_attributes.position[1],
        //           verts[0].obj_attributes.position[2],
        //           verts[0].obj_attributes.position[3],
        //           verts[0].cs_pos[0],
        //           verts[0].cs_pos[1],
        //           verts[0].cs_pos[2],
        //           verts[0].cs_pos[3],
        //           verts[0].clip_code);
        //    debugf("screen_pos: (%f, %f), depth=%f, inv_w=%f\n",
        //           verts[0].screen_pos[0],
        //           verts[0].screen_pos[1],
        //           verts[0].depth,
        //           verts[0].inv_w);
        //}

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
			zbuffer);


	}
}