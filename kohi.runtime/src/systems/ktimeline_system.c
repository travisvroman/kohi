#include "ktimeline_system.h"

#include "core/engine.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"

typedef struct ktimeline_data {
    /** @brief The time in seconds since the last frame. */
    f32 delta_time;

    /** @brief The total amount of time in seconds the application has been running. */
    f64 total_time;

    /**
     * @brief The current scale of this timeline. Default is 1.0. 0 is paused. Negative is rewind, if
     * supported by the system using this timeline.
     */
    f32 time_scale;
} ktimeline_data;

typedef enum ktimeline_flags {
    KTIMELINE_FLAG_NONE = 0,
    KTIMELINE_FLAG_FREE_BIT = 1 << 0
} ktimeline_flags;

typedef u32 ktimeline_flag_bits;

typedef struct ktimeline_system_state {
    ktimeline_data* timelines;
    ktimeline_flag_bits* flags;
    u32 entry_count;
} ktimeline_system_state;

static void ensure_allocated(ktimeline_system_state* state, u32 entry_count) {
    if (state->entry_count < entry_count) {
        {
            u64 old_size = sizeof(ktimeline_data) * state->entry_count;
            u64 new_size = sizeof(ktimeline_data) * entry_count;
            state->timelines = kreallocate(state->timelines, old_size, new_size, MEMORY_TAG_ARRAY);
        }
        {
            u64 old_size = sizeof(ktimeline_flag_bits) * state->entry_count;
            u64 new_size = sizeof(ktimeline_flag_bits) * entry_count;
            state->flags = kreallocate(state->flags, old_size, new_size, MEMORY_TAG_ARRAY);
        }
        // Set all new "slots" to "free".
        for (u32 i = state->entry_count; i < entry_count; ++i) {
            state->flags[i] = FLAG_SET(state->flags[i], KTIMELINE_FLAG_FREE_BIT, true);
        }

        state->entry_count = entry_count;
    }
}

b8 ktimeline_system_initialize(u64* memory_requirement, void* memory, void* config) {
    if (!memory_requirement) {
        KERROR("timeline_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }

    *memory_requirement = sizeof(ktimeline_system_state);
    if (!memory) {
        return true;
    }

    ktimeline_system_state* state = memory;
    // TODO: Maybe read this from config?
    const u32 start_entry_count = 4;
    ensure_allocated(state, start_entry_count); // Prevent lots of early reallocs.
    state->entry_count = start_entry_count;

    // Setup default timelines.
    ktimeline_system_create(1.0f); // engine
    ktimeline_system_create(1.0f); // game

    return true;
}

void ktimeline_system_shutdown(void* state) {
    ktimeline_system_state* typed_state = state;
    if (typed_state->timelines) {
        kfree(typed_state->timelines, sizeof(ktimeline_data) * typed_state->entry_count, MEMORY_TAG_ARRAY);
        typed_state->timelines = 0;
    }

    if (typed_state->flags) {
        kfree(typed_state->flags, sizeof(ktimeline_flag_bits) * typed_state->entry_count, MEMORY_TAG_ARRAY);
        typed_state->flags = 0;
    }

    typed_state->entry_count = 0;
}

b8 ktimeline_system_update(void* state, f32 engine_delta_time) {
    ktimeline_system_state* typed_state = state;
    if (typed_state->timelines && typed_state->flags) {
        for (u32 i = 0; i < typed_state->entry_count; ++i) {
            // Only update slots that contain active timelines.
            if (!FLAG_GET(typed_state->flags[i], KTIMELINE_FLAG_FREE_BIT)) {
                f32 scaled_delta = (engine_delta_time * typed_state->timelines[i].time_scale);
                typed_state->timelines[i].delta_time = scaled_delta;
                typed_state->timelines[i].total_time += scaled_delta;
            }
        }
    }

    return true;
}

ktimeline ktimeline_system_create(f32 scale) {
    ktimeline new_handle;
    ktimeline_system_state* state = engine_systems_get()->timeline_system;
    for (u32 i = 0; i < state->entry_count; ++i) {
        if (FLAG_GET(state->flags[i], KTIMELINE_FLAG_FREE_BIT)) {
            // Found a free slot. Use it.
            new_handle = i;

            state->flags[i] = FLAG_SET(state->flags[i], KTIMELINE_FLAG_FREE_BIT, false);
            state->timelines[i].total_time = 0;
            state->timelines[i].delta_time = 0;
            state->timelines[i].time_scale = scale;

            return new_handle;
        }
    }

    // No free slot available, realloc and use the next new slot.
    u32 old_count = state->entry_count;

    ensure_allocated(state, old_count * 2);

    // Found a free slot. Use it.
    new_handle = old_count;

    state->flags[old_count] = FLAG_SET(state->flags[old_count], KTIMELINE_FLAG_FREE_BIT, false);
    state->timelines[old_count].total_time = 0;
    state->timelines[old_count].delta_time = 0;
    state->timelines[old_count].time_scale = scale;

    return new_handle;
}

void ktimeline_system_destroy(ktimeline timeline) {
    if (timeline < 2) {
        KERROR("timeline_system_destroy cannot be called for default engine or game timelines.");
        return;
    }
    if (timeline == KTIMELINE_INVALID) {
        return;
    }
    ktimeline_system_state* state = engine_systems_get()->timeline_system;

    // Clear the data and Invalidate the handle.
    kzero_memory(&state->timelines[timeline], sizeof(ktimeline_data));
    // Mark as free.
    state->flags[timeline] = FLAG_SET(state->flags[timeline], KTIMELINE_FLAG_FREE_BIT, true);
}

static ktimeline_data* timeline_get_at(ktimeline timeline) {
    if (timeline == KTIMELINE_INVALID) {
        KWARN("Cannot get timeline for invalid handle.");
        return 0;
    }

    ktimeline_system_state* state = engine_systems_get()->timeline_system;
    KASSERT_MSG(timeline < state->entry_count, "Provided handle index is out of range.");

    return &state->timelines[timeline];
}

f32 ktimeline_system_scale_get(ktimeline timeline) {
    ktimeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->time_scale;
}
void ktimeline_system_scale_set(ktimeline timeline, f32 scale) {
    if (timeline < 2) {
        // NOTE: 0 is always the engine scale, which should never be modified!
        KWARN("timeline_system_scale_set cannot be used against the default engine timeline");
        return;
    }
    ktimeline_data* data = timeline_get_at(timeline);
    if (data) {
        data->time_scale = scale;
    }
}

f32 ktimeline_system_total_get(ktimeline timeline) {
    ktimeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->total_time;
}

f32 ktimeline_system_delta_get(ktimeline timeline) {
    ktimeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->delta_time;
}

ktimeline ktimeline_system_get_engine(void) {
    // 0 is the engine timeline
    return 0;
}

ktimeline ktimeline_system_get_game(void) {
    // 1 is the game timeline
    return 1;
}
