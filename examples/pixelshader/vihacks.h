#ifndef VI_HACKS_H
#define VI_HACKS_H

#include <stdint.h>
#include <assert.h>

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
#endif