// perf_test.h — raw panel throughput measurement for the TFT speed tab.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    fill_mbps;    // solid full-screen fills, MB/s
    float    grad_mbps;    // gradient blits, MB/s
    float    fill_fps;     // full-screen fills per second
    uint32_t pixels_per_s; // throughput in pixels/second
    uint32_t frames;       // frames pushed during the run
} perf_result_t;

// Push `frames` full-screen frames straight to the panel (bypassing LVGL) and
// measure throughput. Blocks ~1-2 s. Call with LVGL paused (lvgl_port_stop).
void perf_run(int frames, perf_result_t *out);

#ifdef __cplusplus
}
#endif
