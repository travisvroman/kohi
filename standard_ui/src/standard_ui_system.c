#include "standard_ui_system.h"

#include "core/logger.h"

typedef struct standard_ui_state {
    standard_ui_system_config config;
} standard_ui_state;

static standard_ui_state* state_ptr = 0;

b8 standard_ui_system_initialize(u64* memory_requirement, void* state, void* config) {
    if (!memory_requirement) {
        KERROR("standard_ui_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }
    standard_ui_system_config* typed_config = (standard_ui_system_config*)config;
    /* if (typed_config->dummy== 0) {
        KFATAL("standard_ui_system_initialize - config.max_camera_count must be > 0.");
        return false;
    } */

    u64 struct_requirement = sizeof(standard_ui_state);
    *memory_requirement = struct_requirement;

    if (!state) {
        return true;
    }

    state_ptr = (standard_ui_state*)state;
    state_ptr->config = *typed_config;

    KTRACE("Initialized standard UI system.");

    return true;
}

void standard_ui_system_shutdown(void* state) {
    if (state) {
        //
    }
}

b8 standard_ui_system_update(void* state, const struct frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    return true;
}
