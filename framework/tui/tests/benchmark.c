#define _POSIX_C_SOURCE 200809L

#include "kofun_tui.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum { BATCHES = 9, FRAMES_PER_BATCH = 20000 };

static uint64_t nanoseconds(struct timespec value) {
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static void sort(uint64_t *values, size_t count) {
    for (size_t outer = 1; outer < count; ++outer) {
        uint64_t value = values[outer];
        size_t inner = outer;
        while (inner > 0 && values[inner - 1] > value) {
            values[inner] = values[inner - 1];
            --inner;
        }
        values[inner] = value;
    }
}

int main(void) {
    KofunTuiCapabilities capabilities = {true, true, KOFUN_TUI_COLOR_TRUE};
    KofunTuiViewport viewport = {120, 30};
    uint64_t samples[BATCHES];
    size_t observed = 0;
    for (size_t batch = 0; batch < BATCHES; ++batch) {
        struct timespec before;
        struct timespec after;
        char storage[1024];
        (void)clock_gettime(CLOCK_MONOTONIC, &before);
        for (unsigned frame = 0; frame < FRAMES_PER_BATCH; ++frame) {
            KofunTuiBuffer buffer;
            KofunTuiRenderStats stats;
            kofun_tui_buffer_init(&buffer, storage, sizeof(storage));
            stats = kofun_tui_render_progress(&buffer, capabilities, viewport,
                (KofunTuiProgress){"compile 東京 e\xCC\x81", frame % 100U, 100U, frame});
            observed ^= stats.bytes;
        }
        (void)clock_gettime(CLOCK_MONOTONIC, &after);
        samples[batch] = (nanoseconds(after) - nanoseconds(before)) / FRAMES_PER_BATCH;
    }
    sort(samples, BATCHES);
    printf("TUI benchmark: median %llu ns/frame; 2,000,000 ns frame budget (guard=%zu)\n",
        (unsigned long long)samples[BATCHES / 2], observed);
    return samples[BATCHES / 2] > UINT64_C(2000000) ? 1 : 0;
}
