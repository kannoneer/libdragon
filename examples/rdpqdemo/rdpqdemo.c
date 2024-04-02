#include "libdragon.h"
#include <malloc.h>
#include <math.h>

static sprite_t *brew_sprite;
static sprite_t *tiles_sprite;

static rspq_block_t *tiles_block;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    float scale_factor;
} object_t;

#define NUM_OBJECTS 64

static object_t objects[NUM_OBJECTS];

// Fair and fast random generation (using xorshift32, with explicit seed)
static uint32_t rand_state = 1;
static uint32_t myrand(void) {
	uint32_t x = rand_state;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 5;
	return rand_state = x;
}

// RANDN(n): generate a random number from 0 to n-1
#define RANDN(n) ({ \
	__builtin_constant_p((n)) ? \
		(myrand()%(n)) : \
		(uint32_t)(((uint64_t)myrand() * (n)) >> 32); \
})

static int32_t obj_max_x;
static int32_t obj_max_y;
static int32_t cur_tick = 0;
static uint32_t num_objs = 1;

void update(int ovfl)
{
    for (uint32_t i = 0; i < NUM_OBJECTS; i++)
    {
        object_t *obj = &objects[i];

        int32_t x = obj->x + obj->dx;
        int32_t y = obj->y + obj->dy;

        if (x >= obj_max_x) x -= obj_max_x;
        if (x < 0) x += obj_max_x;
        if (y >= obj_max_y) y -= obj_max_y;
        if (y < 0) y += obj_max_y;
        
        obj->x = x;
        obj->y = y;
        obj->scale_factor = sinf(cur_tick * 0.1f + i) * 0.5f + 1.5f;
    }
    cur_tick++;
}

void render(int cur_frame)
{
    // Attach and clear the screen
    surface_t *disp = display_get();
    //rdpq_attach_clear(disp, NULL);
    rdpq_attach(disp, NULL);

    if (true) {
        rdpq_set_color_image(disp);
        rdpq_set_mode_fill(color_from_packed32(0x001122FF));
        rdpq_fill_rectangle(0, 0, disp->width, disp->height);
    }

    // Draw the tile background, by playing back the compiled block.
    // This is using copy mode by default, but notice how it can switch
    // to standard mode (aka "1 cycle" in RDP terminology) in a completely
    // transparent way. Even if the block is compiled, the RSP commands within it
    // will adapt its commands to the current render mode, Try uncommenting
    // the line below to see.
    rdpq_debug_log_msg("tiles");
    rdpq_set_mode_copy(false);
    // rdpq_set_mode_standard();
    //rspq_block_run(tiles_block);
    
    // Draw the brew sprites. Use standard mode because copy mode cannot handle
    // scaled sprites.
    rdpq_debug_log_msg("sprites");
    rdpq_set_mode_standard();
    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(1);                // colorkey (draw pixel with alpha >= 1)

#if 0
    const float fac = 2.0f;
    rdpq_sprite_blit(brew_sprite, 0, 0, &(rdpq_blitparms_t){
        .scale_x = fac, .scale_y = fac, .filtering=true});
#else
    const float fac = 1.0f;
    rdpq_sprite_blit(brew_sprite, 0, 0, &(rdpq_blitparms_t){
        .scale_x = fac, .scale_y = fac, .filtering=false});
        #endif

    //for (uint32_t i = 0; i < num_objs; i++)
    //{
    //    rdpq_sprite_blit(brew_sprite, objects[i].x, objects[i].y, &(rdpq_blitparms_t){
    //        .scale_x = objects[i].scale_factor, .scale_y = objects[i].scale_factor,
    //    });
    //}

    rdpq_detach_show();
}


/** @brief Register uncached location in memory of VI */
#define VI_REGISTERS_ADDR       0xA4400000
/** @brief Number of useful 32-bit registers at the register base */
#define VI_REGISTERS_COUNT      14

/**
 * @brief Video Interface register structure
 *
 * Whenever trying to configure VI registers, 
 * this struct and its index definitions below can be very useful 
 * in writing comprehensive and verbose code.
 */
typedef struct vi_config_s{
    uint32_t regs[VI_REGISTERS_COUNT];
} vi_config_t;

/** @brief Base pointer to hardware Video interface registers that control various aspects of VI configuration.
 * Shouldn't be used by itself, use VI_ registers to get/set their values. */
#define VI_REGISTERS      ((volatile uint32_t*)VI_REGISTERS_ADDR)
/** @brief VI Index register of controlling general display filters/bitdepth configuration */
#define VI_CTRL           (&VI_REGISTERS[0])
/** @brief VI Index register of RDRAM base address of the video output Frame Buffer. This can be changed as needed to implement double or triple buffering. */
#define VI_ORIGIN         (&VI_REGISTERS[1])
/** @brief VI Index register of width in pixels of the frame buffer. */
#define VI_WIDTH          (&VI_REGISTERS[2])
/** @brief VI Index register of vertical interrupt. */
#define VI_V_INTR         (&VI_REGISTERS[3])
/** @brief VI Index register of the current half line, sampled once per line. */
#define VI_V_CURRENT      (&VI_REGISTERS[4])
/** @brief VI Index register of sync/burst values */
#define VI_BURST          (&VI_REGISTERS[5])
/** @brief VI Index register of total visible and non-visible lines. 
 * This should match either NTSC (non-interlaced: 0x20D, interlaced: 0x20C) or PAL (non-interlaced: 0x271, interlaced: 0x270) */
#define VI_V_SYNC         (&VI_REGISTERS[6])
/** @brief VI Index register of total width of a line */
#define VI_H_SYNC         (&VI_REGISTERS[7])
/** @brief VI Index register of an alternate scanline length for one scanline during vsync. */
#define VI_H_SYNC_LEAP    (&VI_REGISTERS[8])
/** @brief VI Index register of start/end of the active video image, in screen pixels */
#define VI_H_VIDEO        (&VI_REGISTERS[9])
/** @brief VI Index register of start/end of the active video image, in screen half-lines. */
#define VI_V_VIDEO        (&VI_REGISTERS[10])
/** @brief VI Index register of start/end of the color burst enable, in half-lines. */
#define VI_V_BURST        (&VI_REGISTERS[11])
/** @brief VI Index register of horizontal subpixel offset and 1/horizontal scale up factor. */
#define VI_X_SCALE        (&VI_REGISTERS[12])
/** @brief VI Index register of vertical subpixel offset and 1/vertical scale up factor. */
#define VI_Y_SCALE        (&VI_REGISTERS[13])

/** @brief VI register by index (0-13)*/
#define VI_TO_REGISTER(index) (((index) >= 0 && (index) <= VI_REGISTERS_COUNT)? &VI_REGISTERS[index] : NULL)

/** @brief VI index from register */
#define VI_TO_INDEX(reg) ((reg) - VI_REGISTERS)


/** @brief VI_CTRL Register setting: enable dedither filter. */
#define VI_DEDITHER_FILTER_ENABLE           (1<<16)
/** @brief VI_CTRL Register setting: default value for pixel advance. */
#define VI_PIXEL_ADVANCE_DEFAULT            (0b0011 << 12)
/** @brief VI_CTRL Register setting: default value for pixel advance on iQue. */
#define VI_PIXEL_ADVANCE_BBPLAYER           (0b0001 << 12)
/** @brief VI_CTRL Register setting: disable AA / resamp. */
#define VI_AA_MODE_NONE                     (0b11 << 8)
/** @brief VI_CTRL Register setting: disable AA / enable resamp. */
#define VI_AA_MODE_RESAMPLE                 (0b10 << 8)
/** @brief VI_CTRL Register setting: enable AA / enable resamp, fetch pixels when needed. */
#define VI_AA_MODE_RESAMPLE_FETCH_NEEDED    (0b01 << 8)
/** @brief VI_CTRL Register setting: enable AA / enable resamp, fetch pixels always. */
#define VI_AA_MODE_RESAMPLE_FETCH_ALWAYS    (0b00 << 8)
/** @brief VI_CTRL Register setting: enable interlaced output. */
#define VI_CTRL_SERRATE                     (1<<6)
/** @brief VI_CTRL Register setting: enable divot filter (fixes 1 pixel holes after AA). */
#define VI_DIVOT_ENABLE                     (1<<4)
/** @brief VI_CTRL Register setting: enable gamma correction filter. */
#define VI_GAMMA_ENABLE                     (1<<3)
/** @brief VI_CTRL Register setting: enable gamma correction filter and hardware dither the least significant color bit on output. */
#define VI_GAMMA_DITHER_ENABLE              (1<<2)
/** @brief VI_CTRL Register setting: framebuffer source format */
#define VI_CTRL_TYPE                        (0b11)
/** @brief VI_CTRL Register setting: set the framebuffer source as 32-bit. */
#define VI_CTRL_TYPE_32_BPP                 (0b11)
/** @brief VI_CTRL Register setting: set the framebuffer source as 16-bit (5-5-5-3). */
#define VI_CTRL_TYPE_16_BPP                 (0b10)
/** @brief VI_CTRL Register setting: set the framebuffer source as blank (no data and no sync, TV screens will either show static or nothing). */
#define VI_CTRL_TYPE_BLANK                  (0b00)

/** Under VI_ORIGIN  */
/** @brief VI_ORIGIN Register: set the address of a framebuffer. */
#define VI_ORIGIN_SET(value)                ((value & 0xFFFFFF) << 0)

/** Under VI_WIDTH   */
/** @brief VI_ORIGIN Register: set the width of a framebuffer. */
#define VI_WIDTH_SET(value)                 ((value & 0xFFF) << 0)

/** Under VI_V_CURRENT  */
/** @brief VI_V_CURRENT Register: default value for vblank begin line. */
#define VI_V_CURRENT_VBLANK                 2

/** Under VI_V_INTR    */
/** @brief VI_V_INTR Register: set value for vertical interrupt. */
#define VI_V_INTR_SET(value)                ((value & 0x3FF) << 0)
/** @brief VI_V_INTR Register: default value for vertical interrupt. */
#define VI_V_INTR_DEFAULT                   0x3FF

/** Under VI_BURST     */
/** @brief VI_BURST Register: set start of color burst in pixels from hsync. */
#define VI_BURST_START(value)               ((value & 0x3F) << 20)
/** @brief VI_BURST Register: set vertical sync width in half lines. */
#define VI_VSYNC_WIDTH(value)               ((value & 0x7)  << 16)
/** @brief VI_BURST Register: set color burst width in pixels. */
#define VI_BURST_WIDTH(value)               ((value & 0xFF) << 8)
/** @brief VI_BURST Register: set horizontal sync width in pixels. */
#define VI_HSYNC_WIDTH(value)               ((value & 0xFF) << 0)

/** @brief VI_BURST Register: NTSC default start of color burst in pixels from hsync. */
#define VI_BURST_START_NTSC                 62
/** @brief VI_BURST Register: NTSC default vertical sync width in half lines. */
#define VI_VSYNC_WIDTH_NTSC                 5
/** @brief VI_BURST Register: NTSC default color burst width in pixels. */
#define VI_BURST_WIDTH_NTSC                 34
/** @brief VI_BURST Register: NTSC default horizontal sync width in pixels. */
#define VI_HSYNC_WIDTH_NTSC                 57

/** @brief VI_BURST Register: PAL default start of color burst in pixels from hsync. */
#define VI_BURST_START_PAL                  64
/** @brief VI_BURST Register: PAL default vertical sync width in half lines. */
#define VI_VSYNC_WIDTH_PAL                  4
/** @brief VI_BURST Register: PAL default color burst width in pixels.  */
#define VI_BURST_WIDTH_PAL                  35
/** @brief VI_BURST Register: PAL default horizontal sync width in pixels. */
#define VI_HSYNC_WIDTH_PAL                  58

/** @brief VI period for showing one NTSC and MPAL picture in ms. */
#define VI_PERIOD_NTSC_MPAL                 ((float)1000/60)
/** @brief VI period for showing one PAL picture in ms. */
#define VI_PERIOD_PAL                       ((float)1000/50)

/**  Under VI_X_SCALE   */
/** @brief VI_X_SCALE Register: set 1/horizontal scale up factor (value is converted to 2.10 format) */
#define VI_X_SCALE_SET(value)               (( 1024*(value) + 320 ) / 640)

/**  Under VI_Y_SCALE   */
/** @brief VI_Y_SCALE Register: set 1/vertical scale up factor (value is converted to 2.10 format) */
#define VI_Y_SCALE_SET(value)               (( 1024*(value) + 120 ) / 240)

inline void my_vi_write_safe(volatile uint32_t *reg, uint32_t value){
    assert(reg); /* This should never happen */
    *reg = value;
    MEMORY_BARRIER();
}

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    display_init(RESOLUTION_640x480, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

    joypad_init();
    timer_init();

    uint32_t display_width = display_get_width();
    uint32_t display_height = display_get_height();
    
    dfs_init(DFS_DEFAULT_LOCATION);

    rdpq_init();
    rdpq_debug_start();

    // brew_sprite = sprite_load("rom:/rachel_buch_moor.sprite");
    brew_sprite = sprite_load("rom:/dither.sprite");

    obj_max_x = display_width - brew_sprite->width;
    obj_max_y = display_height - brew_sprite->height;

    for (uint32_t i = 0; i < NUM_OBJECTS; i++)
    {
        object_t *obj = &objects[i];

        obj->x = RANDN(obj_max_x);
        obj->y = RANDN(obj_max_y);

        obj->dx = -3 + RANDN(7);
        obj->dy = -3 + RANDN(7);
    }

    tiles_sprite = sprite_load("rom:/tiles.sprite");

    surface_t tiles_surf = sprite_get_pixels(tiles_sprite);

    // Create a block for the background, so that we can replay it later.
    rspq_block_begin();

    // Check if the sprite was compiled with a paletted format. Normally
    // we should know this beforehand, but for this demo we pretend we don't
    // know. This also shows how rdpq can transparently work in both modes.
    bool tlut = false;
    tex_format_t tiles_format = sprite_get_format(tiles_sprite);
    if (tiles_format == FMT_CI4 || tiles_format == FMT_CI8) {
        // If the sprite is paletted, turn on palette mode and load the
        // palette in TMEM. We use the mode stack for demonstration,
        // so that we show how a block can temporarily change the current
        // render mode, and then restore it at the end.
        rdpq_mode_push();
        rdpq_mode_tlut(TLUT_RGBA16);
        rdpq_tex_upload_tlut(sprite_get_palette(tiles_sprite), 0, 16);
        tlut = true;
    }
    uint32_t tile_width = tiles_sprite->width / tiles_sprite->hslices;
    uint32_t tile_height = tiles_sprite->height / tiles_sprite->vslices;
 
    for (uint32_t ty = 0; ty < display_height; ty += tile_height)
    {
        for (uint32_t tx = 0; tx < display_width; tx += tile_width)
        {
            // Load a random tile among the 4 available in the texture,
            // and draw it as a rectangle.
            // Notice that this code is agnostic to both the texture format
            // and the render mode (standard vs copy), it will work either way.
            int s = RANDN(2)*32, t = RANDN(2)*32;
            rdpq_tex_upload_sub(TILE0, &tiles_surf, NULL, s, t, s+32, t+32);
            rdpq_texture_rectangle(TILE0, tx, ty, tx+32, ty+32, s, t);
        }
    }
    
    // Pop the mode stack if we pushed it before
    if (tlut) rdpq_mode_pop();
    tiles_block = rspq_block_end();

    update(0);
    new_timer(TIMER_TICKS(1000000 / 60), TF_CONTINUOUS, update);

    int cur_frame = 0;
    uint32_t shift = 512;
    while (1)
    {
        joypad_poll();
        joypad_buttons_t ckeys = joypad_get_buttons_pressed(JOYPAD_PORT_1);

        if (ckeys.a) {
            shift = shift + 16;
        }
        if (ckeys.b) {
            shift = shift - 16;
        }

        debugf("shift: %lu\n", shift);

        uint32_t ofs = (shift)<<16;
        my_vi_write_safe(VI_X_SCALE, VI_X_SCALE_SET(640) | ofs);
        my_vi_write_safe(VI_Y_SCALE, VI_Y_SCALE_SET(480) | ofs);
        render(cur_frame);


        if (ckeys.c_up && num_objs < NUM_OBJECTS) {
            ++num_objs;
        }

        if (ckeys.c_down && num_objs > 1) {
            --num_objs;
        }

        cur_frame++;
    }
}
