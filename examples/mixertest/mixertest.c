#include <libdragon.h>

// Mixer channel allocation
#define CHANNEL_SFX1    0
#define CHANNEL_SFX2    1
#define CHANNEL_MUSIC   2

int main(void) {
	debug_init_usblog();
	debug_init_isviewer();
	joypad_init();
	display_init(RESOLUTION_512x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE);

	int ret = dfs_init(DFS_DEFAULT_LOCATION);
	assert(ret == DFS_ESUCCESS);

	audio_init(48000, 4);
	mixer_init(16);  // Initialize up to 16 channels

	wav64_t sfx_cannon;//, sfx_laser, sfx_monosample;

	wav64_open(&sfx_cannon, "rom:/ac_loop_2_22.wav64");
	
	// wav64_open(&sfx_laser, "rom:/laser.wav64");
	// wav64_set_loop(&sfx_laser, true);

	// wav64_open(&sfx_monosample, "rom:/monosample8.wav64");
	// wav64_set_loop(&sfx_monosample, true);
    //wav64_play(&sfx_cannon, CHANNEL_SFX1);
    const float vol = 0.1f;
    mixer_ch_set_vol(CHANNEL_SFX1, vol, vol);
    wav64_set_loop(&sfx_cannon, true);
    wav64_play(&sfx_cannon, CHANNEL_SFX1);

	while (1) {
		display_context_t disp = display_get();
		graphics_fill_screen(disp, 0);
		graphics_draw_text(disp, 200-75, 10, "Audio mixer test");
		display_show(disp);

		// Check whether one audio buffer is ready, otherwise wait for next
		// frame to perform mixing.
		mixer_try_play();
	}
}
