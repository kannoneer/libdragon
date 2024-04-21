/**
 * Pixelshader - example of using RSP to manipulate pixels
 * 
 * This example shows how to achieve additive alpha blending using RSP.
 * It is meant just as an example of doing per-pixel effects with RSP. The
 * name "pixel shader" is catchy but the technique cannot be used as a real
 * pixel shader: it is only possible to preprocess a texture/sprite using
 * RSP before using RDP to draw it.
 */

#include <stdlib.h>
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

void rsp_fill_gathertest(uint16_t* offsets) {
    uintptr_t address = PhysicalAddr(offsets);
    rspq_write(ovl_fill_id, RSP_FILL_CMD_GATHERTEST, 0, address);
}

void rsp_fill_load_texture(uint16_t* pixels)
{
    uintptr_t address = PhysicalAddr(pixels);
    // assertf((address & 0x0f) == 0, "physical texture load address should be 16-byte aligned"); // unaligned DMA should work
    rspq_write(ovl_fill_id, RSP_FILL_CMD_LOAD_TEXTURE, 0, address);
}

void rsp_fill_store_texture(uint16_t* dest_pixels)
{
    uintptr_t address = PhysicalAddr(dest_pixels);
    // assertf((address & 0x0f) == 0, "physical texture store address should be 16-byte aligned");
    rspq_write(ovl_fill_id, RSP_FILL_CMD_STORE_TEXTURE, 0, address);
}

void rsp_fill_store_tile(uint16_t* dest_pixels)
{
    uintptr_t address = PhysicalAddr(dest_pixels);
    // assertf((address & 0x0f) == 0, "physical tile dest address should be 16-byte aligned");
    rspq_write(ovl_fill_id, RSP_FILL_CMD_STORE_TILE, 0, address);
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

#include "vihacks.h"


void set_blur(bool on)
{
    //uint32_t ofs = (on ? 512 : 0)<<16;
    static uint32_t xmove;
    uint32_t ofsx = on ? (uint32_t)(1024*0.5f) : 0;
    uint32_t ofsy = on ? (uint32_t)(1024*0.5f) : 0;
    //xmove = (xmove + 16) % 2048;
    //debugf("xmove: %lu\n", xmove);
    my_vi_write_safe(VI_X_SCALE, VI_X_SCALE_SET(display_get_width()) | (ofsx<<16));
    my_vi_write_safe(VI_Y_SCALE, VI_Y_SCALE_SET(display_get_height()) | (ofsy<<16));
}


int main(void) {
    debug_init_isviewer();
    debug_init_usblog();
    display_init((resolution_t){320, 240, INTERLACE_OFF}, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    // display_init((resolution_t){640, 480, INTERLACE_HALF}, DEPTH_16_BPP, 2, GAMMA_NONE, FILTERS_RESAMPLE);
    dfs_init(DFS_DEFAULT_LOCATION);
    joypad_init();
    rdpq_init();
    rdpq_debug_start();

    bool blur = false;
    bool do_dither = false;

    sprite_t* bkg = sprite_load("rom:/background.sprite");
    sprite_t* flare1 = sprite_load("rom:/flare1.sprite");
    sprite_t* texture = sprite_load("rom:/texture_32.rgba16.sprite");
    rdpq_font_t *font = rdpq_font_load("rom:/encode.font64");
    enum { MYFONT = 1 };
    rdpq_text_register_font(MYFONT, font);

    surface_t bkgsurf = sprite_get_pixels(bkg);
    surface_t flrsurf = sprite_get_pixels(flare1);
    surface_t texsurf = sprite_get_pixels(texture);

    assertf(surface_get_format(&texsurf) == FMT_RGBA16, "RGB555 expected");
    uint16_t* texture_data = malloc_uncached_aligned(16, texsurf.height * texsurf.stride);
    memcpy(texture_data, texsurf.buffer, texsurf.height * texsurf.stride);

    // surface_t downscaled = surface_alloc(FMT_RGBA16, display_get_width()/2, display_get_height()/2);

    rsp_overlays_init();  // init our custom overlay

    bool use_rdp = false;

    uint32_t last_frame = 0;
    uint32_t cur_frame = 0;

    float anim = 5.0f;

    while (1) {
        cur_frame = TICKS_READ();
        float time = get_ticks_ms() / 1000.0f;
        // anim = time;
    // set_blur(blur);

        surface_t *screen = display_get();
        rdpq_attach(screen, NULL);

        // Draw the background
        rdpq_set_mode_copy(true);
        rdpq_tex_blit(&bkgsurf, 0, 0, NULL);

        // Draw help text on the top of the screen
        rdpq_set_mode_fill(RGBA32(0,0,0,255));
        rdpq_fill_rectangle(0, 0, screen->width, 30);
        rdpq_text_printf(NULL, MYFONT, 40, 20, "RSP gather experiment (%s, %s) -- %d us", blur ? "Blur" : "Sharp", do_dither ? "Dither" : "-", TIMER_MICROS(last_frame));
        

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
            // rsp_fill_cachetest(&texsurf, 2);
            // rsp_fill_cachetest(&texsurf, 4);
            // rsp_fill_cachetest(&texsurf, 512+8);
            // rsp_fill_cachetest(&texsurf, 512+10);

            //for (int i=0;i<8;i++) {
            //    tex[i] = i;
            //}

            rsp_fill_load_texture(texture_data);
            uint16_t result[16*16]={0};
            data_cache_hit_writeback_invalidate(result, sizeof(result));
            rsp_fill_store_texture(result);
            rspq_wait();
            debugf("texture (texture_data = 0x%p\n", texture_data);
            for (int i=0;i<8;i++) {
                debugf("texture[%d] 0x%x vs 0x%x\n", i, ((uint16_t*)(texture_data))[i], result[i]);
            }

            const int TEXW=texsurf.width;
            const int TEXH=texsurf.height;
            const int TILEW=16;
            const int TILEH=16;
            const int TILE_NUM_X = display_get_width()/TILEW;
            const int TILE_NUM_Y = (display_get_height()-32)/TILEH;

            float ang = 0.1f*anim;
            float scale = 1.5f + 0.4f*cos(anim*0.5f);
            scale = scale;
            float cosa = cos(ang);
            float sina = sin(ang);

            for (int tiley=0;tiley<TILE_NUM_Y;tiley++) {
            for (int tilex=0;tilex<TILE_NUM_X;tilex++) {
            uint16_t offsets[16*16]={0};
            uint16_t counter =0;

            float dither[2][2][2] = {
                {{0.25f, 0.00f}, {0.5f, 0.75f}},
                {{0.75f, 0.50f}, {0.0f, 0.25f}},
            };

            for (int y=0;y<TILEH;y++) {
            for (int x=0;x<TILEW;x++) {
                int tpixx = (x+tilex*TILEW);
                int tpixy = (y+tiley*TILEH);
                float tx = tpixx;
                float ty = tpixy;
                tx -= (TILE_NUM_X*0.5f)*TILEW;
                ty -= (TILE_NUM_Y*0.5f)*TILEH;

                tx *= scale;
                ty *= scale;
                tx += 16.0;
                ty += 16.0;
                float fx = tx*cosa + ty*sina;
                float fy = -tx*sina + ty*cosa;
                if (fx < 0) fx = -fx;
                if (fy < 0) fy = -fy;
                if (tpixx < 0) tpixx = -tpixx;
                if (tpixy < 0) tpixy = -tpixy;
                int ia=tpixy&1, ib=tpixx&1;
                float dx = dither[ia][ib][0];
                float dy = dither[ia][ib][1];
                //debugf("tx=%d, ty=%d, dither[%d][%d]={%f, %f}\n", tx, ty, ia, ib, dx, dy);
                if (do_dither) {
                    fx += dx;
                    fy += dy;
                }
                int ix = (int)(fx) % TEXW;
                int iy = (int)(fy) % TEXH;
                offsets[y*TILEW+x] = iy*texsurf.stride + sizeof(uint16_t)*ix;
            }
            }
            data_cache_hit_writeback(offsets, sizeof(offsets));
            
            debugf("offsets:\n");
            for (int i=0;i<8;i++) {
                debugf("offsets[%d]=0x%x\n", i, offsets[i]);
            }

            rsp_fill_gathertest(offsets);
            uint16_t tile[16*16]={0};
            for (int i=0;i<sizeof(tile)/2;i++) {
                tile[i] = 0x0000fffe;
            }
            data_cache_hit_writeback_invalidate(tile, sizeof(tile));
            rsp_fill_store_tile(tile);

            rspq_wait();

            uint32_t dsty=32 + tiley*TILEH;
            uint32_t dstx=0 + tilex*TILEW;
            for (int y=0;y<16;y++) {
                memcpy(screen->buffer + (screen->stride*(dsty+y)+dstx*sizeof(uint16_t)), &tile[y*16], sizeof(uint16_t)*16);
            }
            
            if (true) {
            debugf("tile:\n");
            for (int i=0;i<8;i++) {
                debugf("tile[%d] 0x%x\n", i, tile[i]);
            }

            // debugf("HALT\n");
            // while (true) {}

            }
            }
            }

            // rdpq_attach(screen, NULL);
            // rdpq_set_mode_copy(false);
            // rdpq_tex_blit(&downscaled, 0, 0, NULL);
            // rdpq_fence();
            // rdpq_detach();

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

        if (keys.c_left) { blur = !blur; set_blur(blur); };
        if (keys.c_right) { do_dither = !do_dither; };

        keys = joypad_get_buttons_held(JOYPAD_PORT_1);
        if (keys.d_up) anim += 0.15f;
        if (keys.d_down) anim -= 0.15f;
    }
}
