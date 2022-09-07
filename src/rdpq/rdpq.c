/**
 * @file rdpq.c
 * @brief RDP Command queue
 * @ingroup rdp
 *
 * # RDP Queue: implementation details
 * 
 * This documentation block describes the internal workings of the RDP Queue.
 * This is useful to understand the implementation, but it is not required
 * to read or understand this to use rdpq. 
 * 
 * For description of the API of the RDP queue, see rdpq.h
 * 
 * ## Improvements over raw hardware programming
 * 
 * RDPQ provides a very low-level API over the RDP graphics chips,
 * exposing all its settings and most of its limits. Still, rdpq
 * tries to hide a few low-level hardware details to make programming the RDP
 * less surprising and more orthogonal. To do so, it "patches" some RDP
 * commands, typically via RSP code and depending on the current RDP state. We
 * called these improvements "fixups".
 * 
 * The documentation of the public rdpq API does not explicitly mention which
 * behavior has been adjusted via fixups. Instead, this section explains in
 * details all the fixups performed by rdpq. Reading this section is not
 * necessary to understand and use rdpq, but it might be useful for people
 * that are familiar with RDP outside of libdragon (eg: libultra programmers),
 * to avoid getting confused in places where rdpq deviates from RDP (even if
 * for the better).
 * 
 * ### Scissoring and texrects: consistent coordinates
 * 
 * The RDP SET_SCISSOR and TEXTURE_RECTANGLE commands accept a rectangle
 * whose major bounds (bottom and right) are either inclusive or exclusive,
 * depending on the current RDP cycle type (fill/copy: exclusive, 1cyc/2cyc: inclusive).
 * #rdpq_set_scissor and #rdpq_texture_rectangle, instead, always use exclusive
 * major bounds, and automatically adjust them depending on the current RDP cycle
 * type. 
 * 
 * Moreover, any time the RDP cycle type changes, the current scissoring is
 * adjusted to guarantee consistent results. This is especially important
 * where the scissoring covers the whole framebuffer, because otherwise the
 * RDP might overflow the buffer while drawing.
 * 
 * ### Avoid color image buffer overflows with auto-scissoring
 * 
 * The RDP SET_COLOR_IMAGE command only contains a memory pointer and a pitch:
 * the RDP is not aware of the actual size of the buffer in terms of width/height,
 * and expects commands to be correctly clipped, or scissoring to be configured.
 * To avoid mistakes leading to memory corruption, #rdpq_set_color_image always
 * reconfigures scissoring to respect the actual buffer size.
 * 
 * Note also that when the RDP is cold-booted, the internal scissoring register
 * contains random data. This means that this auto-scissoring fixup also
 * provides a workaround to this, by making sure scissoring is always configured
 * at least once. In fact, by forgetting to configure scissoring, the RDP
 * can happily draw outside the framebuffer, or draw nothing, or even freeze.
 * 
 * ### Autosync
 * 
 * The RDP has different internal parallel units and exposes three different
 * syncing primitives to stall and avoid write-during-use bugs: SYNC_PIPE,
 * SYNC_LOAD and SYNC_TILE. Correct usage of these commands is not complicated
 * but it can be complex to get right, and require extensive hardware testing
 * because emulators do not implement the bugs caused by the absence of RDP stalls.
 * 
 * rdpq implements a smart auto-syncing engine that tracks the commands sent
 * to RDP (on the CPU) and automatically inserts syncing whenever necessary.
 * Insertion of syncing primitives is optimal for SYNC_PIPE and SYNC_TILE, and
 * conservative for SYNC_LOAD (it does not currently handle partial TMEM updates).
 * 
 * Autosync also works within blocks, but since it is not possible to know
 * the context in which a block will be run, it has to be conservative and
 * might issue more stalls than necessary.
 * 
 * More details on the autosync engine are below.
 * 
 * ### Partial render mode changes
 * 
 * The RDP command SET_OTHER_MODES contains most the RDP mode settings.
 * Unfortunately the command does not allow to change only some settings, but
 * all of them must be reconfigured. This is in contrast with most graphics APIs
 * that allow to configure each render mode setting by itself (eg: it is possible
 * to just change the dithering algorithm).
 * 
 * rdpq instead tracks the current render mode on the RSP, and allows to do
 * partial updates via either the low-level #rdpq_change_other_modes_raw
 * function (where it is possible to change only a subset of the 56 bits),
 * or via the high-level rdpq_mode_* APIs (eg: #rdpq_mode_dithering), which
 * mostly build upon #rdpq_change_other_modes_raw in their implementation.
 *
 * ### Automatic 1/2 cycle type selection
 * 
 * The RDP has two main operating modes: 1 cycle per pixel and 2 cycles per pixel.
 * The latter is twice as slow, as the name implies, but it allows more complex
 * color combiners and/or blenders. Moreover, 2-cycles mode also allows for
 * multi-texturing.
 * 
 * At the hardware level, it is up to the programmer to explicitly activate
 * either 1-cycle or 2-cycle mode. The problem with this is that there are
 * specific rules to follow for either mode, which does not compose cleanly
 * with partial mode changes. For instance, fogging is typically implemented
 * using the 2-cycle mode as it requires two passes in the blender. If the
 * user disables fogging for some meshes, it might be more performant to switch
 * back to 1-cycle mode, but that requires also reconfiguring the combiner.
 * 
 * To solve this problem, the higher level rdpq mode APIs (rdpq_mode_*)
 * automatically select the best cycle type depending on the current settings.
 * More specifically, 1-cycle mode is preferred as it is faster, but 2-cycle
 * mode is activated whenever one of the following conditions is true:
 *
 * * A two-pass blender is configured.
 * * A two-pass combiner is configured.
 *  
 * The correct cycle-type is automatically reconfigured any time that either
 * the blender or the combiner settings are changed. Notice that this means
 * that rdpq also transparently handles a few more details for the user, to
 * make it for an easier API:
 * 
 * * In 1 cycle mode, rdpq makes sure that the second pass of the combiner and
 *   the second pass of the blender are configured exactly like the respective
 *   first passes, because the RDP hardware requires this to operate correctly.
 * * In 2 cycles mode, if a one-pass combiner was configured by the user,
 *   the second pass is automatically configured as a simple passthrough
 *   (equivalent to `((ZERO, ZERO, ZERO, COMBINED), (ZERO, ZERO, ZERO, COMBINED))`).
 * * In 2 cycles mode, if a one-pass blender was configured by the user,
 *   it is configured in the second pass, while the first pass is defined
 *   as a passthrough (equivalent to `((PIXEL_RGB, ZERO, PIXEL_RGB, ONE))`).
 *   Notice that this is required because there is no pure passthrough in
 *   second step of the blender.
 * * RDPQ_COMBINER2 macro transparently handles the texture index swap in the
 *   second cycle. So while using the macro, TEX0 always refers to the first
 *   texture and TEX1 always refers to the second texture. Moreover, uses
 *   of TEX0/TEX1 in passes where they are not allowed would cause compilation
 *   errors, to avoid triggering undefined behaviours in RDP hardware.
 * 
 * ### Fill color as standard 32-bit color
 * 
 * The RDP command SET_FILL_COLOR (used to configure the color register
 * to be used in fill cycle type) has a very low-level interface: its argument
 * is basically a 32-bit value which is copied to the framebuffer as-is,
 * irrespective of the framebuffer color depth. For a 16-bit buffer, then,
 * it must be programmed with two copies of the same 16-bit color.
 * 
 * #rdpq_set_fill_color, instead, accepts a #color_t argument and does the
 * conversion to the "packed" format internally, depending on the current
 * framebuffer's color depth.
 * 
 * ## Usage of inline functions vs no-inline
 * 
 * Most of the rdpq APIs are defined as inline functions in the header rdpq.h,
 * but they then internally call some non-public function to do emit the command.
 * So basically the actual function is split in tow parts: an inlined part and
 * a non-inlined part.
 * 
 * The reason for this split is to help the compiler generate better code. In fact,
 * it is extremely common to call rdpq functions using many constant parameters,
 * and we want those constants to be propagated into the various bit shifts and masks
 * to be assembled into single words. Once the (often constant) arguments have been
 * handled, the rest of the operation can normally be performed in a separate
 * out-of-line function.
 * 
 * ## Sending commands to RDP
 * 
 * This section describes in general how the commands flow from CPU to RDP via RSP.
 * There are several different code-paths here depending on whether the command has
 * a fixup or not, and it is part of a block.
 * 
 * ### RDRAM vs XBUS
 * 
 * In general, the rdpq library sends the commands to RDP using a buffer in RDRAM.
 * The hardware feature called XBUS (which allows to send commands from RSP DMEM
 * to RDP directly) is not used or supported. There are a few reasons for this
 * architectural choice:
 * 
 *  * DMEM is limited (4K), RSP is fast and RDP is slow. Using XBUS means that
 *    you need to create a buffer in DMEM to hold the commands; as the buffer
 *    fills, RSP can trigger RDP to fetch from it, but in general RSP will
 *    generally be faster at filling it than RDP at executing it. At that point,
 *    as the buffer can't grow too much, the RSP will have to stall, slowing
 *    down the rspq queue, which in turns could also cause stalls on the CPU. The
 *    back-pressure from RDP would basically propagate to RSP and then CPU.
 *  * One of the main advantages of using XBUS is that there is no need to copy
 *    data from RSP to RDRAM, saving memory bandwidth. To partially cope up
 *    with it, rdpq has some other tricks up its sleeve to save memory
 *    bandwidth (specifically how it works in block mode, see below).
 * 
 * The buffer in RDRAM where RDP commands are enqueued by RSP is called
 * "RDP dynamic buffer". It is used as a ring buffer, so once full, it is
 * recycled, making sure not to overwrite commands that the RDP has not
 * executed yet.
 * 
 * ### RDP commands in standard mode
 * 
 * Let's check the workflow for a standard RDP command, that is one for which
 * rdpq provides no fixups:
 * 
 *  * CPU (application code): a calls to a rdpq function is made (eg: #rdpq_load_block).
 *  * CPU (rdpq code): the implementation of #rdpq_load_block enqueues a rspq command
 *    for the rdpq overlay. This command has the same binary encoding of a real RDP
 *    LOAD_BLOCK command, while still being a valid rspq command following the rspq
 *    structure of overlay ID + command ID. In fact, the rdpq overlay is registered
 *    to cover 4 overlay IDs (0xC - 0xF), so that the whole RDP command space can be
 *    represented by it. In our example, the command is `0xF3`.
 *  * RSP (rspq code): later at some point, in parallel, the rspq engine will read
 *    the command `0xF3`, and dispatch it to the rdpq overlay.
 *  * RSP (rdpq code): the implementation for command `0xF3` is the same for all
 *    non-fixup commands: it writes the 8 bytes of the command into a temporary
 *    buffer in DMEM, and then sends it via DMA to the RDP dynamic buffer in RDRAM.
 *    This act of forwarding a command through CPU -> RSP -> RDP is called 
 *    "passthrough", and is implemented by `RDPQCmd_Passthrough8` and
 *    `RDPQCmd_Passthrough16` in the ucode (rsp_rdpq.S), and `RSPQ_RdpSend`
 *    in rsp_queue.inc.
 *  * RSP (rdpq code): after the DMA is finished, the RSP tells the RDP that
 *    a new command has been added to the dynamic buffer and can be executed
 *    whenever the RDP is ready. This is easily done by advancing the RDP
 *    `DP_END` register. When the buffer is finished, recycling it requires
 *    instead to write both `DP_START` and `DP_END`. See `RSPQCmd_RdpAppendBuffer`
 *    and `RSPQCmd_RdpSetBuffer` respectively.
 *   
 * ### RDP fixups in standard mode
 * 
 * Now let's see the workflow for a RDP fixup: these are the RDP commands which
 * are modified/tweaked by RSP to provide a more sane programming interface
 * to the programmer.
 * 
 *  * CPU (application code): a calls to a rdpq function is made (eg: #rdpq_set_scissor).
 *  * CPU (rdpq code): the implementation of #rdpq_set_scissor enqueues a rspq command
 *    for the rdpq overlay. This command does not need to have the same encoding of
 *    a real RDP command, but it is usually similar (to simplify work on the RSP).
 *    For instance, in our example the rdpq command is 0xD2, which is meaningless
 *    if sent to RDP, but has otherwise the same encoding of a real SET_SCISSOR
 *    (whose ID would be 0xED).
 *  * RSP (rspq code): later at some point, in parallel, the rspq engine will read
 *    the command `0xD2`, and dispatch it to the rdpq overlay.
 *  * RSP (rdpq code): the implementation for command `0xD2` is a RSP function called
 *    `RDPQCmd_SetScissorEx`. It inspects the RDP state to check the current cycle
 *    type and adapts the scissoring bounds if required. Then, it assembles a real
 *    SET_SCISSOR (with ID 0xD2) and calls `RSPQ_RdpSend` to send it to the RDP
 *    dynamic buffer.
 *  * RSP (rdpq code): after the DMA is finished, the RSP tells the RDP that
 *    a new command has been added to the dynamic buffer and can be executed
 *    whenever the RDP is ready.
 * 
 * The overall workflow is similar to the passthrough, but the command is
 * tweaked by RSP in the process.
 * 
 * ### RDP commands in block mode
 * 
 * In block mode, rdpq completely changes the way of operating. 
 * 
 * A rspq block (as described in rspq.c) is a buffer containing a sequence
 * of rspq commands that can be played back by RSP itself, with the CPU just
 * triggering it via #rspq_block_run. When using rdpq, the rspq block is
 * contains one additional buffer: a "RDP static buffer", which contains
 * RDP commands.
 * 
 * At block creation time, in fact, RDP commands are not enqueued as
 * rspq commands, but are rather written into this separate buffer. Instead,
 * 
 *   TO BE FINISHED ***********************
 * 
 * 
 * ## Autosync engine
 * 
 * As explained above, the autosync engine is able to emit sync commands
 * (SYNC_PIPE, SYNC_TILE, SYNC_LOAD) automatically when necessary, liberating
 * the developer from this additional task. This section describes how it
 * works.
 * 
 * The autosync engine works around one simple abstraction and logic. There are
 * "hardware resources" that can be either "used" or "changed" (aka configured)
 * by RDP commands. If a resource is in use, a command changing it requires
 * a sync before. Each resource is tracked by one bit in a single 32-bit word
 * called the "autosync state".
 * 
 * The following resources are tracked:
 * 
 *  * Pipe. This is a generic resource encompassing all render mode and hardware
 *    register changes. It maps to a single bit (`AUTOSYNC_PIPE`). All render
 *    mode commands "change" this bit (eg: #rdpq_set_other_modes_raw or
 *    #rdpq_set_yuv_parms). All draw commands "use" this bit (eg: #rdpq_triangle).
 *    So for instance, if you draw a triangle, next #rdpq_set_mode_standard call will
 *    automatically issue a `SYNC_PIPE`.
 *  * Tiles. These are 8 resources (8 bits) mapping to the 8 tile descriptors
 *    in RDP hardware, used to describe textures. There is one bit per each descriptor
 *    (`AUTOSYNC_TILE(n)`) so that tracking is actually done at the single tile
 *    granularity. Commands modifying the tile descriptor (such as #rdpq_set_tile
 *    or #rdpq_load_tile) will "change" the resource corresponding for the affect tile.
 *    Commands drawing textured primitives (eg: #rdpq_texture_rectangle) will "use"
 *    the resource. For instance, calling #rdpq_texture_rectangle using #TILE4, and
 *    later calling #rdpq_load_tile on #TILE4 will cause a `SYNC_TILE` to be issued
 *    just before the `LOAD_TILE` command. Notice that if #rdpq_load_tile used
 *    #TILE5 instead, no `SYNC_TILE` would have been issued, assuming #TILE5 was 
 *    never used before. This means that having a logic to cycle through tile
 *    descriptors (instead of always using the same) will reduce the number of
 *    `SYNC_TILE` commands.
 *  * TMEM. Currently, the whole TMEM is tracking as a single resource (using
 *    the bit defined by `AUTOSYNC_TMEM(0)`. Any command that writes to TMEM
 *    (eg: #rdpq_load_block) will "change" the resource. Any command that reads
 *    from TMEM (eg: #rdpq_triangle with a texture) will "use" the resource.
 *    Writing to TMEM while something is reading requires a `SYNC_LOAD` command
 *    to be issued.
 * 
 * Note that there is a limit with the current implementation: the RDP can use
 * multiple tiles with a single command (eg: when using multi-texturing or LODs),
 * but we are not able to track that correctly: all drawing commands for now
 * assume that a single tile will be used. If this proves to be a problem, it is
 * always possible to call #rdpq_sync_tile to manually issue a sync.
 * 
 * Autosync also works with blocks, albeit conservatively. When recording
 * a block, it is not possible to know what the autosync state will be at the
 * point of call (and obviously, it could be called in different situations
 * with different states). The engine thus handles the worst case: at the
 * beginning of a block, it assumes that all resources are "in use". This might
 * cause some sync commands to be run in situations where it would not be
 * strictly required, but the performance impact is unlikely to be noticeable.
 * 
 * Autosync engine can be enabled or disabled via #rdpq_config_enable /
 * #rdpq_config_disable. Remember that manually issuing sync commands require
 * careful debugging on real hardware, as no emulator today is able to 
 * reproduce the effects of a missing sync command.
 * 
 */

#include "rdpq.h"
#include "rdpq_internal.h"
#include "rdpq_constants.h"
#include "rdpq_debug_internal.h"
#include "rspq.h"
#include "rspq/rspq_internal.h"
#include "rspq_constants.h"
#include "rdpq_macros.h"
#include "interrupt.h"
#include "utils.h"
#include "rdp.h"
#include <string.h>
#include <math.h>
#include <float.h>

// The fixup for fill rectangle and texture rectangle uses the exact same code in IMEM.
// It needs to also adjust the command ID with the same constant (via XOR), so make
// sure that we defined the fixups in the right position to make that happen.
_Static_assert(
    (RDPQ_CMD_FILL_RECTANGLE ^ RDPQ_CMD_FILL_RECTANGLE_EX) == 
    (RDPQ_CMD_TEXTURE_RECTANGLE ^ RDPQ_CMD_TEXTURE_RECTANGLE_EX),
    "invalid command numbering");

static void rdpq_assert_handler(rsp_snapshot_t *state, uint16_t assert_code);

/** @brief The rdpq ucode overlay */
DEFINE_RSP_UCODE(rsp_rdpq, 
    .assert_handler=rdpq_assert_handler);

/** @brief State of the rdpq ucode overlay.
 * 
 * This must be kept in sync with rsp_rdpq.S.
 */
typedef struct rdpq_state_s {
    uint64_t sync_full;                 ///< Last SYNC_FULL command
    uint32_t address_table[RDPQ_ADDRESS_TABLE_SIZE];    ///< Address lookup table
    rspq_rdp_mode_t modes[3];           ///< Modes stack
    uint32_t rdram_state_address;       ///< Address of this state in RDRAM
} rdpq_state_t;

/** @brief Mirror in RDRAM of the state of the rdpq ucode. */ 
static rdpq_state_t *rdpq_state;

bool __rdpq_inited = false;             ///< True if #rdpq_init was called

/** @brief Current configuration of the rdpq library. */ 
static uint32_t rdpq_config;

/** @brief RDP block management state */
rdpq_block_state_t rdpq_block_state;

/** @brief Tracking state of RDP */
rdpq_tracking_t rdpq_tracking;

/** 
 * @brief RDP interrupt handler 
 *
 * The RDP interrupt is triggered after a SYNC_FULL command is finished
 * (all previous RDP commands are fully completed). In case the user
 * requested a callback to be called when that specific SYNC_FULL
 * instance has finished, the interrupt routine must call the specified
 * callback.
 */
static void __rdpq_interrupt(void) {
    assert(*SP_STATUS & SP_STATUS_SIG_RDPSYNCFULL);

    // Fetch the current RDP buffer for tracing
    if (rdpq_trace_fetch) rdpq_trace_fetch();

    // The state has been updated to contain a copy of the last SYNC_FULL command
    // that was sent to RDP. The command might contain a callback to invoke.
    // Extract it to local variables.
    uint32_t w0 = (rdpq_state->sync_full >> 32) & 0x00FFFFFF;
    uint32_t w1 = (rdpq_state->sync_full >>  0) & 0xFFFFFFFF;

    // Notify the RSP that we've serviced this SYNC_FULL interrupt. If others
    // are pending, they can be scheduled now, even as we execute the callback.
    MEMORY_BARRIER();
    *SP_STATUS = SP_WSTATUS_CLEAR_SIG_RDPSYNCFULL;

    // If there was a callback registered, call it.
    if (w0) {
        void (*callback)(void*) = (void (*)(void*))CachedAddr(w0 | 0x80000000);
        void* arg = (void*)w1;

        callback(arg);
    }
}

void rdpq_init()
{
    // Do nothing if rdpq was already initialized
    if (__rdpq_inited)
        return;

    rspq_init();

    // Get a pointer to the RDRAM copy of the rdpq ucode state.
    rdpq_state = UncachedAddr(rspq_overlay_get_state(&rsp_rdpq));

    // Initialize the ucode state.
    memset(rdpq_state, 0, sizeof(rdpq_state_t));
    rdpq_state->rdram_state_address = PhysicalAddr(rdpq_state);
    for (int i=0;i<3;i++)
        rdpq_state->modes[i].other_modes = ((uint64_t)RDPQ_OVL_ID << 32) + ((uint64_t)RDPQ_CMD_SET_OTHER_MODES << 56);
    
    // Register the rdpq overlay at a fixed position (0xC)
    rspq_overlay_register_static(&rsp_rdpq, RDPQ_OVL_ID);

    // Clear library globals
    memset(&rdpq_block_state, 0, sizeof(rdpq_block_state));
    rdpq_config = RDPQ_CFG_DEFAULT;
    rdpq_tracking.autosync = 0;
    rdpq_tracking.mode_freeze = false;

    // Register an interrupt handler for DP interrupts, and activate them.
    register_DP_handler(__rdpq_interrupt);
    set_DP_interrupt(1);

    // Remember that initialization is complete
    __rdpq_inited = true;
}

void rdpq_close()
{
    if (!__rdpq_inited)
        return;
    
    rspq_overlay_unregister(RDPQ_OVL_ID);

    set_DP_interrupt( 0 );
    unregister_DP_handler(__rdpq_interrupt);

    __rdpq_inited = false;
}

uint32_t rdpq_config_set(uint32_t cfg)
{
    uint32_t prev = rdpq_config;
    rdpq_config = cfg;
    return prev;
}

uint32_t rdpq_config_enable(uint32_t cfg)
{
    return rdpq_config_set(rdpq_config | cfg);
}

uint32_t rdpq_config_disable(uint32_t cfg)
{
    return rdpq_config_set(rdpq_config & ~cfg);
}

void rdpq_fence(void)
{
    // We want the RSP to wait until the RDP is finished. We do this in
    // two steps: first we issue a SYNC_FULL (we don't need CPU-side callbacks),
    // then we send the internal rspq command that make the RSP spin-wait
    // until the RDP is idle. The RDP becomes idle only after SYNC_FULL is done.
    rdpq_sync_full(NULL, NULL);
    rspq_int_write(RSPQ_CMD_RDP_WAIT_IDLE);
}

/** @brief Assert handler for RSP asserts (see "RSP asserts" documentation in rsp.h) */
static void rdpq_assert_handler(rsp_snapshot_t *state, uint16_t assert_code)
{
    switch (assert_code)
    {
    case RDPQ_ASSERT_FILLCOPY_BLENDING:
        printf("Cannot call rdpq_mode_blender in fill or copy mode\n");
        break;

    case RDPQ_ASSERT_MIPMAP_COMB2:
        printf("Interpolated mipmap cannot work with a custom 2-pass combiner\n");
        break;

    default:
        printf("Unknown assert\n");
        break;
    }
}

/** @brief Autosync engine: mark certain resources as in use */
void __rdpq_autosync_use(uint32_t res) { 
    rdpq_tracking.autosync |= res;
}

/** 
 * @brief Autosync engine: mark certain resources as being changed.
 * 
 * This is the core of the autosync engine. Whenever a resource is "changed"
 * while "in use", a SYNC command must be issued. This is a slightly conservative
 * approach, as the RDP might already have finished using that resource,
 * but we have no way to know it.
 * The SYNC command will then reset the "use" status of each respective resource.
 */
void __rdpq_autosync_change(uint32_t res) {
    res &= rdpq_tracking.autosync;
    if (res) {
        if ((res & AUTOSYNC_TILES) && (rdpq_config & RDPQ_CFG_AUTOSYNCTILE))
            rdpq_sync_tile();
        if ((res & AUTOSYNC_TMEMS) && (rdpq_config & RDPQ_CFG_AUTOSYNCLOAD))
            rdpq_sync_load();
        if ((res & AUTOSYNC_PIPE)  && (rdpq_config & RDPQ_CFG_AUTOSYNCPIPE))
            rdpq_sync_pipe();
    }
}

/**
 * @name RDP block management functions.
 * 
 * All the functions in this group are called in the context of creation
 * of a RDP block (part of a rspq block). See the top-level documentation
 * for a general overview of how RDP blocks work.
 * 
 * @{
 */

/** 
 * @brief Initialize RDP block mangament
 * 
 * This is called by #rspq_block_begin. It resets all the block management
 * state to default.
 * 
 * Notice that no allocation is performed. This is because we do block
 * allocation lazily as soon as a rdpq command is issued. In fact, if
 * the block does not contain rdpq commands, it would be a waste of time
 * and memory to allocate a RDP buffer. The allocations will be performed
 * by #__rdpq_block_next_buffer as soon as a rdpq command is written.
 * 
 * @see #rspq_block_begin
 * @see #__rdpq_block_next_buffer
 */
void __rdpq_block_begin()
{
    memset(&rdpq_block_state, 0, sizeof(rdpq_block_state));

    // Save the tracking state (to be recovered when the block is done)
    rdpq_block_state.previous_tracking = rdpq_tracking;

    // Initialize tracking state for a new block
    rdpq_tracking = (rdpq_tracking_t){
        // current autosync status is unknown because blocks can be
        // played in any context. So assume the worst: all resources
        // are being used. This will cause all SYNCs to be generated,
        // which is the safest option.
        .autosync = ~0,
        // we don't know whether mode changes will be frozen or not
        // when the block will play. Assume the worst (and thus
        // do not optimize out mode changes).
        .mode_freeze = false,
    };
}

/** 
 * @brief Allocate a new RDP block buffer, chaining it to the current one (if any) 
 * 
 * This function is called by #rdpq_write and #rdpq_fixup_write when we are about
 * to write a rdpq command in a block, and the current RDP buffer is full
 * (`wptr + cmdsize >= wend`). By extension, it is also called when the current
 * RDP buffer has not been allocated yet (`wptr == wend == NULL`).
 * 
 * @see #rdpq_write
 * @see #rdpq_fixup_write
 */
void __rdpq_block_next_buffer(void)
{
    struct rdpq_block_state_s *st = &rdpq_block_state;

    // Configure block minimum size
    if (st->bufsize == 0) {
        st->bufsize = RDPQ_BLOCK_MIN_SIZE;
        assert(RDPQ_BLOCK_MIN_SIZE >= RDPQ_MAX_COMMAND_SIZE);
    }

    // Allocate next chunk (double the size of the current one).
    // We use doubling here to reduce overheads for large blocks
    // and at the same time start small.
    int memsz = sizeof(rdpq_block_t) + st->bufsize*sizeof(uint32_t);
    rdpq_block_t *b = malloc_uncached(memsz);

    // Chain the block to the current one (if any)
    b->next = NULL;
    if (st->last_node) {
        st->last_node->next = b;
    }
    st->last_node = b;
    if (!st->first_node) st->first_node = b;

    // Set write pointer and sentinel for the new buffer
    st->wptr = b->cmds;
    st->wend = b->cmds + st->bufsize;

    assertf((PhysicalAddr(st->wptr) & 0x7) == 0,
        "start not aligned to 8 bytes: %lx", PhysicalAddr(st->wptr));
    assertf((PhysicalAddr(st->wend) & 0x7) == 0,
        "end not aligned to 8 bytes: %lx", PhysicalAddr(st->wend));

    // Save the pointer to the current position in the RSP queue. We're about
    // to write a RSPQ_CMD_RDP_SET_BUFFER that we might need to coalesce later.
    extern volatile uint32_t *rspq_cur_pointer;
    st->last_rdp_append_buffer = rspq_cur_pointer;

    // Enqueue a rspq command that will make the RDP DMA registers point to the
    // new buffer (though with DP_START==DP_END, as the buffer is currently empty).
    rspq_int_write(RSPQ_CMD_RDP_SET_BUFFER,
        PhysicalAddr(st->wptr), PhysicalAddr(st->wptr), PhysicalAddr(st->wend));

    // Grow size for next buffer
    if (st->bufsize < RDPQ_BLOCK_MAX_SIZE) st->bufsize *= 2;
}

/**
 * @brief Finish creation of a RDP block.
 * 
 * This is called by #rspq_block_end. It finalizes block creation
 * and return a pointer to the first node of the block, which will
 * be put within the #rspq_block_t structure, so to be able to 
 * reference it in #__rdpq_block_run and #__rdpq_block_free.
 * 
 * @return rdpq_block_t*  The created block (first node)
 * 
 * @see #rspq_block_end
 * @see #__rdpq_block_run
 * @see #__rdpq_block_free
 */
rdpq_block_t* __rdpq_block_end()
{
    struct rdpq_block_state_s *st = &rdpq_block_state;
    rdpq_block_t *ret = st->first_node;

    // Save the current autosync state in the first node of the RDP block.
    // This makes it easy to recover it when the block is run
    if (st->first_node)
        st->first_node->tracking = rdpq_tracking;

    // Recover tracking state before the block creation started
    rdpq_tracking = st->previous_tracking;

    return ret;
}

/** @brief Run a block (called by #rspq_block_run). */
void __rdpq_block_run(rdpq_block_t *block)
{
    // We are about to run a block that contains rdpq commands.
    // During creation, we tracked some state for the block 
    // and saved it into the block structure; set it as current,
    // because from now on we can assume the block would and the
    // state of the engine must match the state at the end of the block.
    if (block)
        rdpq_tracking = block->tracking;
}

/** 
 * @brief Free a block 
 * 
 * This function is called when a block is freed. It is called
 * by #rspq_block_free.
 * 
 * @see #rspq_block_free.
 */
void __rdpq_block_free(rdpq_block_t *block)
{
    // Go through the chain and free all nodes
    while (block) {
        void *b = block;
        block = block->next;
        free_uncached(b);
    }
}

/**
 * @brief Set a new RDP write pointer, and enqueue a RSP command to run the buffer until there
 * 
 * This function is called by #rdpq_write after some RDP commands have been written
 * into the block's RDP buffer. A rspq command #RSPQ_CMD_RDP_APPEND_BUFFER will be issued
 * so that the RSP will tell the RDP to fetch and run the new commands, appended at
 * the end of the current buffer.
 * 
 * If possible, though, this function will coalesce the command with an immediately
 * preceding RSPQ_CMD_RDP_APPEND_BUFFER (or even RSPQ_CMD_RDP_SET_BUFFER, if we are
 * at the start of the buffer), so that only a single RSP command is issued, which
 * covers multiple RDP commands.
 * 
 * @param wptr    New block's RDP write pointer
 */
void __rdpq_block_update(volatile uint32_t *wptr)
{
    struct rdpq_block_state_s *st = &rdpq_block_state;
    uint32_t phys_old = PhysicalAddr(st->wptr);
    uint32_t phys_new = PhysicalAddr(wptr);
    st->wptr = wptr;

    assertf((phys_old & 0x7) == 0, "old not aligned to 8 bytes: %lx", phys_old);
    assertf((phys_new & 0x7) == 0, "new not aligned to 8 bytes: %lx", phys_new);

    if (st->last_rdp_append_buffer && (*st->last_rdp_append_buffer & 0xFFFFFF) == phys_old) {
        // Update the previous command.
        // It can be either a RSPQ_CMD_RDP_SET_BUFFER or RSPQ_CMD_RDP_APPEND_BUFFER,
        // but we still need to update it to the new END pointer.
        *st->last_rdp_append_buffer = (*st->last_rdp_append_buffer & 0xFF000000) | phys_new;
    } else {
        // A fixup has emitted some commands, so we need to emit a new
        // RSPQ_CMD_RDP_APPEND_BUFFER in the RSP queue of the block
        extern volatile uint32_t *rspq_cur_pointer;
        st->last_rdp_append_buffer = rspq_cur_pointer;
        rspq_int_write(RSPQ_CMD_RDP_APPEND_BUFFER, phys_new);
    }
}

/** 
 * @brief Set a new RDP write pointer, but don't enqueue RSP commands
 * 
 * This is semantically like #__rdpq_block_update, but it doesn't enqueue any RSP
 * command. It is called by #rdpq_fixup_write: in fact, the fixup is already
 * a RSP command which will then be in charge of sending the commands to RDP,
 * so no action is required here.
 * 
 * @param wptr    New block's RDP write pointer
 */
void __rdpq_block_update_norsp(volatile uint32_t *wptr)
{
    struct rdpq_block_state_s *st = &rdpq_block_state;
    st->wptr = wptr;
    st->last_rdp_append_buffer = NULL;
}

/** @} */


/**
 * @name Helpers to write generic RDP commands
 * 
 * All the functions in this group are wrappers around #rdpq_write to help
 * generating RDP commands. They are called by inlined functions in rdpq.h.
 * See the top-level documentation about inline functions to understand the
 * reason of this split.
 *  
 * @{
 */

/** @brief Write a standard 8-byte RDP command */
__attribute__((noinline))
void __rdpq_write8(uint32_t cmd_id, uint32_t arg0, uint32_t arg1)
{
    rdpq_write((cmd_id, arg0, arg1));
}

/** @brief Write a standard 8-byte RDP command, which changes some autosync resources  */
__attribute__((noinline))
void __rdpq_write8_syncchange(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync)
{
    __rdpq_autosync_change(autosync);
    __rdpq_write8(cmd_id, arg0, arg1);
}

/** @brief Write a standard 8-byte RDP command, which uses some autosync resources  */
__attribute__((noinline))
void __rdpq_write8_syncuse(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync)
{
    __rdpq_autosync_use(autosync);
    __rdpq_write8(cmd_id, arg0, arg1);
}

/** @brief Write a standard 8-byte RDP command, which changes some autosync resources and uses others. */
__attribute__((noinline))
void __rdpq_write8_syncchangeuse(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t autosync_c, uint32_t autosync_u)
{
    __rdpq_autosync_change(autosync_c);
    __rdpq_autosync_use(autosync_u);
    __rdpq_write8(cmd_id, arg0, arg1);
}

/** @brief Write a standard 16-byte RDP command  */
__attribute__((noinline))
void __rdpq_write16(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    rdpq_write((cmd_id, arg0, arg1, arg2, arg3));
}

/** @brief Write a standard 16-byte RDP command, which uses some autosync resources  */
__attribute__((noinline))
void __rdpq_write16_syncuse(uint32_t cmd_id, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t autosync)
{
    __rdpq_autosync_use(autosync);
    __rdpq_write16(cmd_id, arg0, arg1, arg2, arg3);
}

/** @brief Write a 8-byte RDP command fixup. */
__attribute__((noinline))
void __rdpq_fixup_write8_pipe(uint32_t cmd_id, uint32_t w0, uint32_t w1)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (cmd_id, w0, w1),
        (cmd_id, w0, w1)
    );
}

/** @} */


/**
 * @name RDP fixups out-of-line implementations
 *
 * These are the out-of line implementations of RDP commands which needs specific logic,
 * mostly because they are fixups.
 * 
 * @{
 */

/** @brief Out-of-line implementation of #rdpq_texture_rectangle */
__attribute__((noinline))
void __rdpq_texture_rectangle(uint32_t w0, uint32_t w1, uint32_t w2, uint32_t w3)
{
    int tile = (w1 >> 24) & 7;
    // FIXME: this can also use tile+1 in case the combiner refers to TEX1
    // FIXME: this can also use tile+2 and +3 in case SOM activates texture detail / sharpen
    __rdpq_autosync_use(AUTOSYNC_PIPE | AUTOSYNC_TILE(tile) | AUTOSYNC_TMEM(0));
    rdpq_fixup_write(
        (RDPQ_CMD_TEXTURE_RECTANGLE_EX, w0, w1, w2, w3),  // RSP
        (RDPQ_CMD_TEXTURE_RECTANGLE_EX, w0, w1, w2, w3)   // RDP
    );
}

/** @brief Out-of-line implementation of #rdpq_texture_rectangle */
__attribute__((noinline))
void __rdpq_fill_rectangle(uint32_t w0, uint32_t w1)
{
    __rdpq_autosync_use(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (RDPQ_CMD_FILL_RECTANGLE_EX, w0, w1),  // RSP
        (RDPQ_CMD_FILL_RECTANGLE_EX, w0, w1)   // RDP
    );
}


/** @brief Out-of-line implementation of #rdpq_set_scissor */
__attribute__((noinline))
void __rdpq_set_scissor(uint32_t w0, uint32_t w1)
{
    // NOTE: SET_SCISSOR does not require SYNC_PIPE
    rdpq_fixup_write(
        (RDPQ_CMD_SET_SCISSOR_EX, w0, w1),  // RSP
        (RDPQ_CMD_SET_SCISSOR_EX, w0, w1)   // RDP
    );
}

/** @brief Out-of-line implementation of #rdpq_set_fill_color */
__attribute__((noinline))
void __rdpq_set_fill_color(uint32_t w1)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (RDPQ_CMD_SET_FILL_COLOR_32, 0, w1), // RSP
        (RDPQ_CMD_SET_FILL_COLOR_32, 0, w1)  // RDP
    );
}

/** @brief Out-of-line implementation of #rdpq_set_color_image */
__attribute__((noinline))
void __rdpq_set_color_image(uint32_t w0, uint32_t w1, uint32_t sw0, uint32_t sw1)
{
    // SET_COLOR_IMAGE on RSP always generates an additional SET_SCISSOR, so make sure there is
    // space for it in case of a static buffer (in a block).
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (RDPQ_CMD_SET_COLOR_IMAGE, w0, w1), // RSP
        (RDPQ_CMD_SET_COLOR_IMAGE, w0, w1), (RDPQ_CMD_SET_FILL_COLOR, 0, 0) // RDP
    );

    if (rdpq_config & RDPQ_CFG_AUTOSCISSOR)
        __rdpq_set_scissor(sw0, sw1);
}

void rdpq_set_color_image(surface_t *surface)
{
    assertf((PhysicalAddr(surface->buffer) & 63) == 0,
        "buffer pointer is not aligned to 64 bytes, so it cannot be used as RDP color image");
    rdpq_set_color_image_raw(0, PhysicalAddr(surface->buffer), 
        surface_get_format(surface), surface->width, surface->height, surface->stride);
}

void rdpq_set_z_image(surface_t *surface)
{
    assertf(surface_get_format(surface) == FMT_RGBA16, "the format of the Z-buffer surface must be RGBA16");
    assertf((PhysicalAddr(surface->buffer) & 63) == 0,
        "buffer pointer is not aligned to 64 bytes, so it cannot be used as RDP Z image");
    rdpq_set_z_image_raw(0, PhysicalAddr(surface->buffer));
}

void rdpq_set_texture_image(surface_t *surface)
{
    tex_format_t fmt = surface_get_format(surface);
    assertf((PhysicalAddr(surface->buffer) & 7) == 0,
        "buffer pointer is not aligned to 8 bytes, so it cannot be used as RDP texture image");
    rdpq_set_texture_image_raw(0, PhysicalAddr(surface->buffer), fmt, 
        TEX_FORMAT_BYTES2PIX(fmt, surface->stride), surface->height);
}

/** @brief Out-of-line implementation of #rdpq_set_other_modes_raw */
__attribute__((noinline))
void __rdpq_set_other_modes(uint32_t w0, uint32_t w1)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (RDPQ_CMD_SET_OTHER_MODES, w0, w1),  // RSP
        (RDPQ_CMD_SET_OTHER_MODES, w0, w1), (RDPQ_CMD_SET_SCISSOR, 0, 0)   // RDP
    );
}

/** @brief Out-of-line implementation of #rdpq_change_other_modes_raw */
__attribute__((noinline))
void __rdpq_change_other_modes(uint32_t w0, uint32_t w1, uint32_t w2)
{
    __rdpq_autosync_change(AUTOSYNC_PIPE);
    rdpq_fixup_write(
        (RDPQ_CMD_MODIFY_OTHER_MODES, w0, w1, w2),
        (RDPQ_CMD_SET_OTHER_MODES, 0, 0), (RDPQ_CMD_SET_SCISSOR, 0, 0)   // RDP
    );
}

uint64_t rdpq_get_other_modes_raw(void)
{
    rsp_queue_t *state = __rspq_get_state();
    return state->rdp_mode.other_modes;
}

void rdpq_sync_full(void (*callback)(void*), void* arg)
{
    uint32_t w0 = PhysicalAddr(callback);
    uint32_t w1 = (uint32_t)arg;

    // We encode in the command (w0/w1) the callback for the RDP interrupt,
    // and we need that to be forwarded to RSP dynamic command.
    rdpq_fixup_write(
        (RDPQ_CMD_SYNC_FULL, w0, w1), // RSP
        (RDPQ_CMD_SYNC_FULL, w0, w1)  // RDP
    );

    // The RDP is fully idle after this command, so no sync is necessary.
    rdpq_tracking.autosync = 0;
}

void rdpq_sync_pipe(void)
{
    __rdpq_write8(RDPQ_CMD_SYNC_PIPE, 0, 0);
    rdpq_tracking.autosync &= ~AUTOSYNC_PIPE;
}

void rdpq_sync_tile(void)
{
    __rdpq_write8(RDPQ_CMD_SYNC_TILE, 0, 0);
    rdpq_tracking.autosync &= ~AUTOSYNC_TILES;
}

void rdpq_sync_load(void)
{
    __rdpq_write8(RDPQ_CMD_SYNC_LOAD, 0, 0);
    rdpq_tracking.autosync &= ~AUTOSYNC_TMEMS;
}

/** @} */

/* Extern inline instantiations. */
extern inline void rdpq_set_fill_color(color_t color);
extern inline void rdpq_set_fill_color_stripes(color_t color1, color_t color2);
extern inline void rdpq_set_fog_color(color_t color);
extern inline void rdpq_set_blend_color(color_t color);
extern inline void rdpq_set_prim_color(color_t color);
extern inline void rdpq_set_env_color(color_t color);
extern inline void rdpq_set_prim_depth_raw(uint16_t primitive_z, int16_t primitive_delta_z);
extern inline void rdpq_load_tlut(rdpq_tile_t tile, uint8_t lowidx, uint8_t highidx);
extern inline void rdpq_set_tile_size_fx(rdpq_tile_t tile, uint16_t s0, uint16_t t0, uint16_t s1, uint16_t t1);
extern inline void rdpq_load_block(rdpq_tile_t tile, uint16_t s0, uint16_t t0, uint16_t num_texels, uint16_t tmem_pitch);
extern inline void rdpq_load_block_fx(rdpq_tile_t tile, uint16_t s0, uint16_t t0, uint16_t num_texels, uint16_t dxt);
extern inline void rdpq_load_tile_fx(rdpq_tile_t tile, uint16_t s0, uint16_t t0, uint16_t s1, uint16_t t1);
extern inline void rdpq_set_tile_full(rdpq_tile_t tile, tex_format_t format, uint16_t tmem_addr, uint16_t tmem_pitch, uint8_t palette, uint8_t ct, uint8_t mt, uint8_t mask_t, uint8_t shift_t, uint8_t cs, uint8_t ms, uint8_t mask_s, uint8_t shift_s);
extern inline void rdpq_set_other_modes_raw(uint64_t mode);
extern inline void rdpq_change_other_modes_raw(uint64_t mask, uint64_t val);
extern inline void rdpq_fill_rectangle_fx(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
extern inline void rdpq_set_color_image_raw(uint8_t index, uint32_t offset, tex_format_t format, uint32_t width, uint32_t height, uint32_t stride);
extern inline void rdpq_set_z_image_raw(uint8_t index, uint32_t offset);
extern inline void rdpq_set_texture_image_raw(uint8_t index, uint32_t offset, tex_format_t format, uint16_t width, uint16_t height);
extern inline void rdpq_set_lookup_address(uint8_t index, void* rdram_addr);
extern inline void rdpq_set_tile(rdpq_tile_t tile, tex_format_t format, uint16_t tmem_addr, uint16_t tmem_pitch, uint8_t palette);
