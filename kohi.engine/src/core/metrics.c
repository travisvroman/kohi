#include "metrics.h"
#include "core/kmemory.h"

#define AVG_COUNT 30

typedef struct metrics_state {
    u8 frame_avg_counter;
    f64 ms_times[AVG_COUNT];
    f64 ms_avg;
    i32 frames;
    f64 accumulated_frame_ms;
    f64 fps;
} metrics_state;

static metrics_state* state_ptr = 0;

void metrics_initialize(void) {
    if (!state_ptr) {
        state_ptr = kallocate(sizeof(metrics_state), MEMORY_TAG_ENGINE);
    }
}

void metrics_update(f64 frame_elapsed_time) {
    if (!state_ptr) {
        return;
    }

    // Calculate frame ms average
    f64 frame_ms = (frame_elapsed_time * 1000.0);
    state_ptr->ms_times[state_ptr->frame_avg_counter] = frame_ms;
    if (state_ptr->frame_avg_counter == AVG_COUNT - 1) {
        for (u8 i = 0; i < AVG_COUNT; ++i) {
            state_ptr->ms_avg += state_ptr->ms_times[i];
        }

        state_ptr->ms_avg /= AVG_COUNT;
    }
    state_ptr->frame_avg_counter++;
    state_ptr->frame_avg_counter %= AVG_COUNT;

    // Calculate frames per second.
    state_ptr->accumulated_frame_ms += frame_ms;
    if (state_ptr->accumulated_frame_ms > 1000) {
        state_ptr->fps = state_ptr->frames;
        state_ptr->accumulated_frame_ms -= 1000;
        state_ptr->frames = 0;
    }

    // Count all frames.
    state_ptr->frames++;
}

f64 metrics_fps(void) {
    if (!state_ptr) {
        return 0;
    }

    return state_ptr->fps;
}

f64 metrics_frame_time(void) {
    if (!state_ptr) {
        return 0;
    }

    return state_ptr->ms_avg;
}

void metrics_frame(f64* out_fps, f64* out_frame_ms) {
    if (!state_ptr) {
        *out_fps = 0;
        *out_frame_ms = 0;
        return;
    }

    *out_fps = state_ptr->fps;
    *out_frame_ms = state_ptr->ms_avg;
}
