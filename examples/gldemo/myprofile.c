#include "myprofile.h"
#include "debug.h"
#include "n64sys.h"
#include <memory.h>
#include <stdio.h>

#define SCALE_RESULTS  2048

static uint64_t slot_total[PS_NUM_SLOTS];
static uint64_t slot_total_count[PS_NUM_SLOTS];
static uint64_t total_time;
static uint64_t last_frame;
uint64_t my_slot_frame_cur[PS_NUM_SLOTS];
static int frames;

void my_profile_init(void) {
	memset(slot_total, 0, sizeof(slot_total));
	memset(slot_total_count, 0, sizeof(slot_total_count));
	memset(my_slot_frame_cur, 0, sizeof(my_slot_frame_cur));
	frames = 0;

	total_time = 0;
	last_frame = TICKS_READ();
}

void my_profile_next_frame(void) {
	for (int i=0;i<PS_NUM_SLOTS;i++) {
		// Extract and save the total time for this frame.
		slot_total[i] += my_slot_frame_cur[i] >> 32;
		slot_total_count[i] += my_slot_frame_cur[i] & 0xFFFFFFFF;
		my_slot_frame_cur[i] = 0;
	}
	frames++;

	// Increment total profile time. Make sure to handle overflow of the
	// hardware profile counter, as it happens frequently.
	uint64_t count = TICKS_READ();
	total_time += TICKS_DISTANCE(last_frame, count);
	last_frame = count;
}

static void my_stats(ProfileSlot slot, uint64_t frame_avg, uint32_t *mean, float *partial) {
	*mean = slot_total[slot]/frames;
	*partial = (float)*mean * 100.0 / (float)frame_avg;
}

void my_profile_dump(void) {
	debugf("Performance profile\n");
	debugf("%-14s %4s %6s %6s\n", "Slot", "Cnt", "Avg", "Cum");
	debugf("----------------------------------\n");

	debugf("Frames: %d\n", frames);
	uint64_t frame_avg = total_time / frames;
	debugf("frame avg: %llu\n", frame_avg);
	char buf[64];

#define DUMP_SLOT(slot, name) ({ \
	uint32_t mean; float partial; \
	my_stats(slot, frame_avg, &mean, &partial); \
	sprintf(buf, "%2.1f", partial); \
	debugf("%-25s %4llu %6ld %5s%%\n", name, \
		  slot_total_count[slot] / frames, \
		  mean/SCALE_RESULTS, \
		 buf); \
})

	DUMP_SLOT(PS_UPDATE, "Update");
	DUMP_SLOT(PS_RENDER, "Render");
	DUMP_SLOT(PS_RENDER_BG, "  - Background");
	DUMP_SLOT(PS_RENDER_SIM, "  - Simulation");
	DUMP_SLOT(PS_RENDER_SIM_DRAWCALLS,"    - Drawcalls");
	DUMP_SLOT(PS_RENDER_SIM_SETUP, "    - Setup");

	DUMP_SLOT(PS_RENDER_SHADOWS, "  - Shadows");
	DUMP_SLOT(PS_RENDER_SHADOWS_INVERT, "    - Basis inversion");
	DUMP_SLOT(PS_RENDER_FLARE, "  - Flare");
	DUMP_SLOT(PS_RENDER_POSTPROC, "  - Postproc");
	DUMP_SLOT(PS_MUSIC, "Music");

	debugf("----------------------------------\n");
	debugf("Profiled frames:      %4d\n", frames);
	debugf("Frames per second:    %4.1f\n", (float)TICKS_PER_SECOND/(float)(frame_avg > 0 ? frame_avg : 1));
	debugf("Average frame time:   %4lld\n", frame_avg/SCALE_RESULTS);
	debugf("Target frame time:    %4d\n", TICKS_PER_SECOND/24/SCALE_RESULTS);
}
