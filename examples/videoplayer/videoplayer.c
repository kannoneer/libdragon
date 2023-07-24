#include <libdragon.h>
#include "../../src/video/profile.h"

#define NUM_DISPLAY   4

void audio_poll(void) {	
	if (audio_can_write()) {    	
		PROFILE_START(PS_AUDIO, 0);
		short *buf = audio_write_begin();
		mixer_poll(buf, audio_get_buffer_length());
		audio_write_end();
		PROFILE_STOP(PS_AUDIO, 0);
	}
}

void video_poll(void) {


}

int main(void) {
	controller_init();
	debug_init_isviewer();
	debug_init_usblog();

	display_init(RESOLUTION_320x240, DEPTH_32_BPP, NUM_DISPLAY, GAMMA_NONE, ANTIALIAS_OFF);
	dfs_init(DFS_DEFAULT_LOCATION);
	rdpq_init();

	audio_init(44100, 4);
	mixer_init(8);

	mpeg2_t mp2;
	//mpeg2_open(&mp2, "rom:/hirvikallo2.m1v");
	//mpeg2_open(&mp2, "rom:/hakuvalo1_edit.m1v");
	mpeg2_open(&mp2, "rom:/supercut.m1v");

	// wav64_t music;
	// wav64_open(&music, "bbb.wav64");

	float fps = mpeg2_get_framerate(&mp2);
	throttle_init(fps, 0, 8);

	// mixer_ch_play(0, &music.wave);

	debugf("start\n");
	int nframes = 0;
	display_context_t disp = 0;

	while (1) {
		mixer_throttle(44100.0f / fps);

		if (!mpeg2_next_frame(&mp2))
			break;

		disp = display_get();

		// rdpq_attach(disp, NULL);
		rdpq_attach_clear(disp, NULL);

		mpeg2_draw_frame(&mp2, disp);

		rdpq_detach();
		rdpq_fence();


		//rdpq_set_fill_color(RGBA32(255, 255, 255, 255));
		//rdpq_set_blend_color(RGBA32(255, 255, 255, 255));

		// Darken the whole image
		rdpq_attach(disp, NULL);

		rdpq_set_mode_standard();
		//rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        //rdpq_set_mode_copy(true);
		// rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,0),       (0,0,0,PRIM)));

		if (false) {
			const int lvl = 24;
			rdpq_set_prim_color(RGBA32(lvl, lvl, lvl, 255));
			rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,TEX0), (0,0,0,TEX0)));
			rdpq_tex_blit(disp, 0, 0, NULL);
		} else  if (true) {
			const int lvl = 255;
			rdpq_set_prim_color(RGBA32(lvl, lvl, lvl, 64));
			rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,TEX0,0), (0,0,0,TEX0)));
			rdpq_tex_blit(disp, 0, 0, NULL);

		} else {
			rdpq_set_mode_standard();
			rdpq_set_prim_color(RGBA32(64,64,64, 40));
			rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,0),       (0,0,0,PRIM)));
			//rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
			rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
			rdpq_fill_rectangle(0, 0, 320, 240);
		}

		/*
        rdpq_set_mode_standard();
		rdpq_set_prim_color(RGBA32(0,0,0, 128));
		rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
		rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_fill_rectangle(0, 0, 320, 240);

		// Additive noise
		rdpq_set_prim_color(RGBA32(255,255,255, 128));
		rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);

		//rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
		rdpq_mode_combiner(RDPQ_COMBINER1((NOISE,0,PRIM,0),       (0,0,0,PRIM)));

		// Fazana: RDP noise you do through the combiner
		// Fazana: then the density of the noise is based off the alpha of the primitive

        rdpq_fill_rectangle(0, 0, 320, 240);
		*/
		rdpq_detach_show();
   

		audio_poll();

		nframes++;
		// uint32_t t1 = TICKS_READ();
		// if (TICKS_DISTANCE(t0, t1) > TICKS_PER_SECOND && nframes) {
		// 	float fps = (float)nframes / (float)TICKS_DISTANCE(t0,t1) * TICKS_PER_SECOND;
		// 	debugf("FPS: %.2f\n", fps);
		// 	t0 = t1;
		// 	nframes = 0;
		// }

		int ret = throttle_wait();
		if (ret < 0) {
			debugf("videoplayer: frame %d too slow (%d Kcycles)\n", nframes, -ret);
		}

		audio_poll();

		PROFILE_START(PS_SYNC, 0);
		rspq_wait();
		PROFILE_STOP(PS_SYNC, 0);
	}
}
