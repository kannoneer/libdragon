/**
 * Pixelshader - example of using RSP to manipulate pixels
 * 
 * This example shows how to achieve additive alpha blending using RSP.
 * It is meant just as an example of doing per-pixel effects with RSP. The
 * name "pixel shader" is catchy but the technique cannot be used as a real
 * pixel shader: it is only possible to preprocess a texture/sprite using
 * RSP before using RDP to draw it.
 */

#include <libdragon.h>
#include "rsp_blend_constants.h"
#include "rsp_fill_constants.h"

static uint32_t ovl_id;
static uint32_t ovl_fill_id;
static void rsp_blend_assert_handler(rsp_snapshot_t *state, uint16_t code);
static void rsp_fill_assert_handler(rsp_snapshot_t *state, uint16_t code);

enum {
    // Overlay commands. This must match the command table in the RSP code
    RSP_BLEND_CMD_SET_SOURCE   = 0x0,
    RSP_BLEND_CMD_PROCESS_LINE = 0x1,
};

// Overlay definition
DEFINE_RSP_UCODE(rsp_blend,
    .assert_handler = rsp_blend_assert_handler);

DEFINE_RSP_UCODE(rsp_fill,
    .assert_handler = rsp_fill_assert_handler);

void rsp_overlays_init(void) {
    // Initialize if rspq (if it isn't already). It's best practice to let all overlays
    // always call rspq_init(), so that they can be themselves initialized in any order
    // by the user.
    rspq_init();
    ovl_id = rspq_overlay_register(&rsp_blend);
    ovl_fill_id = rspq_overlay_register(&rsp_fill);
}

void rsp_blend_assert_handler(rsp_snapshot_t *state, uint16_t code) {
    switch (code) {
    case ASSERT_INVALID_WIDTH:
        printf("Invalid surface width (%ld)\nMust be multiple of 8 and less than 640\n",
            state->gpr[8]); // read current width from t0 (reg #8): we know it's there at assert point
        return;
    }
}

void rsp_fill_assert_handler(rsp_snapshot_t *state, uint16_t code) {
    switch (code) {
    case ASSERT_FAIL:
        printf("t0 was (%ld)\n",
            state->gpr[8]); // read current width from t0 (reg #8): we know it's there at assert point
        return;
    }
}

void rsp_fill_set_screen_size(surface_t *dst) {

    assertf(surface_get_format(dst) == FMT_RGBA16, "rsp_fill only handles RGB555 surfaces");
    rspq_write(ovl_fill_id, RSP_FILL_CMD_SET_SCREEN_SIZE, 0, (dst->width << 16) | dst->height, dst->stride);
    // rspq_write(ovl_fill_id, RSP_FILL_CMD_SET_SCREEN_SIZE, (uint32_t)(123), 0);
}

void rsp_fill_draw_constant_color(surface_t *dst, int x, int y, color_t color)
{
    assertf(surface_get_format(dst) == FMT_RGBA16, "rsp_fill only handles RGB555 surfaces");
    uintptr_t address = PhysicalAddr(dst->buffer) + (y * dst->stride) + (x * sizeof(uint16_t));
    uint16_t rgb555 = color_to_packed16(color);
    rspq_write(ovl_fill_id, RSP_FILL_CMD_DRAW_CONSTANT, 0, address, rgb555);
}

void rsp_fill_downsample_tile(surface_t *dst, surface_t *src, int srcx, int srcy, int dstx, int dsty, uint8_t bias)
{
    assertf(surface_get_format(dst) == FMT_RGBA16, "rsp_fill only handles RGB555 surfaces");
    assertf(surface_get_format(src) == FMT_RGBA16, "rsp_fill only handles RGB555 surfaces");
    uintptr_t dst_address = PhysicalAddr(dst->buffer) + (dsty * dst->stride) + (dstx * sizeof(uint16_t));
    uintptr_t src_address = PhysicalAddr(src->buffer) + (srcy * src->stride) + (srcx * sizeof(uint16_t));

    // command<2> FillCmd_Downsample(u32 biasIn, u32 dstAddress, u32 srcAddress, u32 dstSrcStride)
    rspq_write(ovl_fill_id, RSP_FILL_CMD_DOWNSAMPLE, bias & 0x000000ff, dst_address, src_address, (dst->stride << 16) | src->stride);

}

void rsp_fill_cachetest(surface_t *tex, uint32_t ofs) {
    // ofs = 4; // offset in bytes
    // uint16_t data[] = {999,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    // data_cache_hit_writeback(data, sizeof(data));

    void* ptr = tex->buffer;

    uint16_t right_answer = *((u_uint16_t*)(((void*)ptr) + ofs));
    debugf("C-side read value: %u\n", right_answer);
    uintptr_t address = PhysicalAddr(ptr);
    rspq_write(ovl_fill_id, RSP_FILL_CMD_CACHETEST, 0, address + ofs);
}


void rsp_blend_set_source(surface_t *src) {
    assertf(surface_get_format(src) == FMT_RGBA16, 
        "rsp_blend only handles RGB555 surfaces");
    rspq_write(ovl_id, RSP_BLEND_CMD_SET_SOURCE, PhysicalAddr(src->buffer),
        (src->width << 16) | src->height);
}

void rsp_blend_process_line(surface_t *dest, int x0, int y0, int numlines) {
    assertf(surface_get_format(dest) == FMT_RGBA16, 
        "rsp_blend only handles RGB555 surfaces");

    void *line = dest->buffer + y0 * dest->stride + x0 * 2;
    for (int i=0; i<numlines; i++) {
        rspq_write(ovl_id, RSP_BLEND_CMD_PROCESS_LINE, PhysicalAddr(line));
        line += dest->stride;
    }
}





int main(void) {
    debug_init_isviewer();
    debug_init_usblog();
    display_init(RESOLUTION_640x480, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();
    rdpq_init();
    rdpq_debug_start();

    sprite_t* bkg = sprite_load("rom:/background.sprite");
    sprite_t* flare1 = sprite_load("rom:/flare1.sprite");
    sprite_t* texture = sprite_load("rom:/texture.rgba16.sprite");
    rdpq_font_t *font = rdpq_font_load("rom:/encode.font64");
    enum { MYFONT = 1 };
    rdpq_text_register_font(MYFONT, font);

    surface_t bkgsurf = sprite_get_pixels(bkg);
    surface_t flrsurf = sprite_get_pixels(flare1);
    surface_t texsurf = sprite_get_pixels(texture);

    // surface_t downscaled = surface_alloc(FMT_RGBA16, display_get_width()/2, display_get_height()/2);

    rsp_overlays_init();  // init our custom overlay

    bool use_rdp = false;

    uint32_t last_frame = 0;
    uint32_t cur_frame = 0;

    while (1) {
        cur_frame = TICKS_READ();

        surface_t *screen = display_get();
        rdpq_attach(screen, NULL);

        // Draw the background
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(&bkgsurf, 0, 0, NULL);

        // Draw help text on the top of the screen
        rdpq_set_mode_fill(RGBA32(0,0,0,0));
        rdpq_fill_rectangle(0, 0, screen->width, 30);
        rdpq_text_printf(NULL, MYFONT, 40, 20, "Additive blending with %s (press A to toggle) -- %d us", use_rdp ? "RDP" : "RSP", TIMER_MICROS(last_frame));
        

        if (use_rdp) {
            // Draw the flare using RDP additive blending (will overflow)
            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
            rdpq_tex_blit(&flrsurf, 30, 60, NULL);
            rdpq_detach_show();
        } else {
            // Detach the RDP.
            rdpq_detach();
            
            // Add a fence. This makes the RSP wait until the RDP has finished drawing,
            // which is what we need as we are going to process the pixels of the background
            // with the RSP.
            rdpq_fence();

            rsp_fill_set_screen_size(screen);
            rsp_fill_draw_constant_color(screen, 60, 80, (color_t){255,0,128,255});

            // Configure source surface
            // rsp_blend_set_source(&flrsurf);

            // // Apply blending
            // rsp_blend_process_line(screen, 30, 60, flrsurf.height);

            // Wait for RSP to finish processing
            rspq_wait();
            //for (int x=0;x<10;x++) {
            //    int y = 100;
            //    rsp_fill_downsample_tile(&downscaled, screen, x*16, y*16, x*8, y*8, 0);
            //}
            rsp_fill_cachetest(&texsurf, 4);
            rspq_wait();

            // rdpq_attach(screen, NULL);
            // rdpq_set_mode_copy(false);
            // rdpq_tex_blit(&downscaled, 0, 0, NULL);
            // rdpq_fence();
            // rdpq_detach();

            // Draw the flare using RSP additive blending (will not overflow)
            display_show(screen);
        }

        // Wait until RSP+RDP are idle. This is normally not required, but we force it here
        // to measure the exact frame computation time.
        rspq_wait();
        last_frame = TICKS_READ() - cur_frame;

        joypad_poll();
        joypad_buttons_t keys = joypad_get_buttons_pressed(JOYPAD_PORT_1);
        if (keys.a) {
            use_rdp = !use_rdp;
        }
    }
}
