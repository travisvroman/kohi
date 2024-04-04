#include "timeline_system.h"
#include "kassert.h"
#include "khandle.h"
#include "kmemory.h"
#include "logger.h"
#include "core/systems_manager.h"
#include "defines.h"

typedef struct timeline_data {
    /** @brief The time in seconds since the last frame. */
    f32 delta_time;

    /** @brief The total amount of time in seconds the application has been running. */
    f64 total_time;

    /**
     * @brief The current scale of this timeline. Default is 1.0. 0 is paused. Negative is rewind, if
     * supported by the system using this timeline.
     */
    f32 time_scale;
} timeline_data;

typedef struct timeline_system_state {
    timeline_data* timelines;
    u64* handle_uuids;
    u32 entry_count;
} timeline_system_state;

static void ensure_allocated(timeline_system_state* state, u32 entry_count) {
    if (state->entry_count < entry_count) {
        {
            u64 old_size = sizeof(timeline_data) * state->entry_count;
            u64 new_size = sizeof(timeline_data) * entry_count;
            state->timelines = kreallocate(state->timelines, old_size, new_size, MEMORY_TAG_ARRAY);
        }
        {
            u64 old_size = sizeof(u64) * state->entry_count;
            u64 new_size = sizeof(u64) * entry_count;
            state->handle_uuids = kreallocate(state->handle_uuids, old_size, new_size, MEMORY_TAG_ARRAY);
        }
        // Invalidate all new "slots" by setting the handle uuid to INVALID_ID_U64.
        for (u32 i = state->entry_count; i < entry_count; ++i) {
            state->handle_uuids[i] = INVALID_ID_U64;
        }

        state->entry_count = entry_count;
    }
}

b8 timeline_system_initialize(u64* memory_requirement, void* memory, void* config) {
    if (!memory_requirement) {
        KERROR("timeline_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }

    *memory_requirement = sizeof(timeline_system_state);
    if (!memory) {
        return true;
    }

    timeline_system_state* state = memory;
    // TODO: Maybe read this from config?
    const u32 start_entry_count = 4;
    ensure_allocated(state, start_entry_count); // Prevent lots of early reallocs.
    state->entry_count = start_entry_count;

    // Setup default timelines.
    timeline_system_create(1.0f); // engine
    timeline_system_create(1.0f); // game

    return true;
}

void timeline_system_shutdown(void* state) {
    timeline_system_state* typed_state = state;
    if (typed_state->timelines) {
        kfree(typed_state->timelines, sizeof(timeline_data) * typed_state->entry_count, MEMORY_TAG_ARRAY);
        typed_state->timelines = 0;
    }

    if (typed_state->handle_uuids) {
        kfree(typed_state->handle_uuids, sizeof(u64) * typed_state->entry_count, MEMORY_TAG_ARRAY);
        typed_state->handle_uuids = 0;
    }

    typed_state->entry_count = 0;
}

b8 timeline_system_update(void* state, f32 engine_delta_time) {
    timeline_system_state* typed_state = state;
    if (typed_state->timelines && typed_state->handle_uuids) {
        for (u32 i = 0; i < typed_state->entry_count; ++i) {
            // Only update slots that contain active timelines.
            if (typed_state->handle_uuids[i] != INVALID_ID_U64) {
                f32 scaled_delta = (engine_delta_time * typed_state->timelines[i].time_scale);
                typed_state->timelines[i].delta_time = scaled_delta;
                typed_state->timelines[i].total_time += scaled_delta;
            }
        }
    }

    return true;
}

k_handle timeline_system_create(f32 scale) {
    k_handle new_handle;
    timeline_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_TIMELINE);
    for (u32 i = 0; i < state->entry_count; ++i) {
        if (state->handle_uuids[i] == INVALID_ID_U64) {
            // Found a free slot. Use it.
            new_handle = k_handle_create(i);

            state->handle_uuids[i] = new_handle.unique_id.uniqueid;
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
    new_handle = k_handle_create(old_count);

    state->handle_uuids[old_count] = new_handle.unique_id.uniqueid;
    state->timelines[old_count].total_time = 0;
    state->timelines[old_count].delta_time = 0;
    state->timelines[old_count].time_scale = scale;

    return new_handle;
}

void timeline_system_destroy(k_handle timeline) {
    if (timeline.handle_index < 2) {
        KERROR("timeline_system_destroy cannot be called for default engine or game timelines.");
        return;
    }
    if (k_handle_is_invalid(timeline)) {
        return;
    }
    timeline_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_TIMELINE);
    // Check that the passed-in handle is not stale.
    if (state->handle_uuids[timeline.handle_index] != timeline.unique_id.uniqueid) {
        // It's stale, do nothing.
        return;
    }

    // Clear the data and Invalidate the handle.
    kzero_memory(&state->timelines[timeline.handle_index], sizeof(timeline_data));
    state->handle_uuids[timeline.handle_index] = INVALID_ID_U64;
}

static timeline_data* timeline_get_at(k_handle timeline) {
    if (k_handle_is_invalid(timeline)) {
        KWARN("Cannot get timeline for invalid handle.");
        return 0;
    }

    timeline_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_TIMELINE);
    KASSERT_MSG(timeline.handle_index < state->entry_count, "Provided handle index is out of range.");

    // Check that the passed-in handle is not stale.
    if (state->handle_uuids[timeline.handle_index] == timeline.unique_id.uniqueid) {
        return &state->timelines[timeline.handle_index];
    }

    // Stale, return null.
    KWARN("Attempting to get a timeline with a stale handle. No timeline will be returned.");
    return 0;
}

f32 timeline_system_scale_get(k_handle timeline) {
    timeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->time_scale;
}
void timeline_system_scale_set(k_handle timeline, f32 scale) {
    if (timeline.handle_index == 0) {
        // NOTE: 0 is always the engine scale, which should never be modified!
        KWARN("timeline_system_scale_set cannot be used against the default engine timeline");
        return;
    }
    timeline_data* data = timeline_get_at(timeline);
    if (data) {
        data->time_scale = scale;
    }
}

f32 timeline_system_total_get(k_handle timeline) {
    timeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->total_time;
}

f32 timeline_system_delta_get(k_handle timeline) {
    timeline_data* data = timeline_get_at(timeline);
    if (!data) {
        return 0;
    }
    return data->delta_time;
}

k_handle timeline_system_get_engine(void) {
    timeline_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_TIMELINE);
    k_handle handle;
    handle.handle_index = 0;
    handle.unique_id.uniqueid = state->handle_uuids[0];
    return handle;
}

k_handle timeline_system_get_game(void) {
    timeline_system_state* state = systems_manager_get_state(K_SYSTEM_TYPE_TIMELINE);
    k_handle handle;
    handle.handle_index = 1;
    handle.unique_id.uniqueid = state->handle_uuids[1];
    return handle;
}
