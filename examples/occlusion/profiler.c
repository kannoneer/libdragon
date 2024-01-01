#include "profiler.h"

#include <stdint.h>
#include <n64sys.h>
#include <debug.h>

static uint64_t prof_times[REGION_COUNT]={};
static uint32_t prof_starts[REGION_COUNT]={};
static uint32_t prof_num_samples = 0;

void prof_next_frame()
{
    prof_num_samples++;
}

void prof_begin(int region)
{
    prof_starts[region] = TICKS_READ();
}

void prof_end(int region)
{
    uint32_t delta = TICKS_SINCE(prof_starts[region]);
    prof_times[region] += (uint64_t)delta;
}

float prof_get_ms(int region) {
    return TICKS_TO_US(prof_times[region]) / 1e3f;
}

void prof_print_stats()
{
    uint64_t total_ticks = 0;
    for (int i = 0; i < REGION_COUNT; i++) {
        total_ticks += prof_times[i];
    }

    #define PRINT_REGION(idx, name) \
        debugf("%-16s %6.3f %% (%.3f ms)\n", name, (prof_times[idx] / (double)(total_ticks+1))*100, TICKS_TO_US(prof_times[idx])/1e3);


    PRINT_REGION(REGION_FRUSTUM_CULL, "Frustum culling")
    PRINT_REGION(REGION_TEST_OBJECTS, "Test objects");
    PRINT_REGION(REGION_DRAW_OCCLUDERS, "Draw occluders");
    PRINT_REGION(REGION_CULL_OCCLUDERS, "  Cull occluders");
    PRINT_REGION(REGION_TEST_OCCLUDERS, "  Test occluders");
    PRINT_REGION(REGION_TRANSFORM, "Transform")
    PRINT_REGION(REGION_TRANSFORM_MVP, "  MVP")
    PRINT_REGION(REGION_TRANSFORM_ROUGH, "  Rough");
    PRINT_REGION(REGION_TRANSFORM_DRAW, "  Draw");

    PRINT_REGION(REGION_PROBES, "Fog probes");
    PRINT_REGION(REGION_RASTERIZATION, "Rasterization")
    PRINT_REGION(REGION_TESTING, "Testing")

}

void prof_reset_stats()
{
    for (int i = 0; i < REGION_COUNT; i++) {
        prof_times[i] = 0;
    }

    prof_num_samples = 0;
}