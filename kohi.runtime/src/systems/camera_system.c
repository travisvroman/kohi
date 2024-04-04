#include "camera_system.h"

#include "containers/hashtable.h"
#include "kstring.h"
#include "logger.h"
#include "renderer/camera.h"

typedef struct camera_lookup {
    u16 id;
    u16 reference_count;
    camera c;
} camera_lookup;

typedef struct camera_system_state {
    camera_system_config config;
    hashtable lookup;
    void* hashtable_block;
    camera_lookup* cameras;

    // A default, non-registered camera that always exists as a fallback.
    camera default_camera;
} camera_system_state;

static camera_system_state* state_ptr;

b8 camera_system_initialize(u64* memory_requirement, void* state, void* config) {
    camera_system_config* typed_config = (camera_system_config*)config;
    if (typed_config->max_camera_count == 0) {
        KFATAL("camera_system_initialize - config.max_camera_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    u64 struct_requirement = sizeof(camera_system_state);
    u64 array_requirement = sizeof(camera_lookup) * typed_config->max_camera_count;
    u64 hashtable_requirement = sizeof(camera_lookup) * typed_config->max_camera_count;
    *memory_requirement = struct_requirement + array_requirement + hashtable_requirement;

    if (!state) {
        return true;
    }

    state_ptr = (camera_system_state*)state;
    state_ptr->config = *typed_config;

    // The array block is after the state. Already allocated, so just set the pointer.
    void* array_block = state + struct_requirement;
    state_ptr->cameras = array_block;

    // Hashtable block is after array.
    void* hashtable_block = array_block + array_requirement;

    // Create a hashtable for camera lookups.
    hashtable_create(sizeof(u16), typed_config->max_camera_count, hashtable_block, false, &state_ptr->lookup);

    // Fill the hashtable with invalid references to use as a default.
    u16 invalid_id = INVALID_ID_U16;
    hashtable_fill(&state_ptr->lookup, &invalid_id);

    // Invalidate all cameras in the array.
    u32 count = state_ptr->config.max_camera_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->cameras[i].id = INVALID_ID_U16;
        state_ptr->cameras[i].reference_count = 0;
    }

    // Setup default camera.
    state_ptr->default_camera = camera_create();

    return true;
}

void camera_system_shutdown(void* state) {
    camera_system_state* s = (camera_system_state*)state;
    if (s) {
        // NOTE: If cameras need to be destroyed, do it here.
        // // Invalidate all cameras in the array.
        // u32 count = s->config.max_camera_count;
        // for (u32 i = 0; i < count; ++i) {
        //     if (s->cameras[i].id != INVALID_ID) {

        //     }
        // }
    }

    state_ptr = 0;
}

camera* camera_system_acquire(const char* name) {
    if (state_ptr) {
        if (strings_equali(name, DEFAULT_CAMERA_NAME)) {
            return &state_ptr->default_camera;
        }
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->lookup, name, &id)) {
            KERROR("camera_system_acquire failed lookup. Null returned.");
            return 0;
        }

        if (id == INVALID_ID_U16) {
            // Find free slot
            for (u16 i = 0; i < state_ptr->config.max_camera_count; ++i) {
                if (state_ptr->cameras[i].id == INVALID_ID_U16) {
                    id = i;
                    break;
                }
            }
            if (id == INVALID_ID_U16) {
                KERROR("camera_system_acquire failed to acquire new slot. Adjust camera system config to allow more. Null returned.");
                return 0;
            }

            // Create/register the new camera.
            KTRACE("Creating new camera named '%s'...", name);
            state_ptr->cameras[id].c = camera_create();
            state_ptr->cameras[id].id = id;

            // Update the hashtable.
            hashtable_set(&state_ptr->lookup, name, &id);
        }
        state_ptr->cameras[id].reference_count++;
        return &state_ptr->cameras[id].c;
    }

    KERROR("camera_system_acquire called before system initialzation. Null returned.");
    return 0;
}

void camera_system_release(const char* name) {
    if (state_ptr) {
        if (strings_equali(name, DEFAULT_CAMERA_NAME)) {
            KTRACE("Cannot release default camera. Nothing was done.");
            return;
        }
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->lookup, name, &id)) {
            KWARN("camera_system_release failed lookup. Nothing was done.");
        }

        if (id != INVALID_ID_U16) {
            // Decrement the reference count, and reset the camera if the counter reaches 0.
            state_ptr->cameras[id].reference_count--;
            if (state_ptr->cameras[id].reference_count < 1) {
                camera_reset(&state_ptr->cameras[id].c);
                state_ptr->cameras[id].id = INVALID_ID_U16;
                hashtable_set(&state_ptr->lookup, name, &state_ptr->cameras[id].id);
            }
        }
    }
}

camera* camera_system_get_default(void) {
    if (state_ptr) {
        return &state_ptr->default_camera;
    }

    KERROR("camera_system_get_default called before system was initialized. Returned null.");
    return 0;
}
