#ifndef PROFILER_H_
#define PROFILER_H_

#include <stdint.h>
#include <n64sys.h>
#include <debug.h>

#define REGION_TRANSFORM (0)
#define REGION_RASTERIZATION (1)
#define REGION_TESTING (2)
#define REGION_FRUSTUM_CULL (3)
#define REGION_DRAW_OCCLUDERS (4)
#define REGION_CULL_OCCLUDERS (5)
#define REGION_PROBES (6)
#define REGION_TEST_OCCLUDERS (7)
#define REGION_TRANSFORM_MVP (9)
#define REGION_TRANSFORM_ROUGH (10)
#define REGION_TRANSFORM_DRAW (11)
#define REGION_COUNT (12)

void prof_next_frame();
void prof_begin(int region);
void prof_end(int region);
void prof_print_stats();
void prof_reset_stats();

#endif