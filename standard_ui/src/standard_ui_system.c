#include "standard_ui_system.h"

#include <containers/darray.h>
#include <core/kmemory.h>
#include <core/logger.h>
#include <defines.h>

typedef struct standard_ui_state {
    standard_ui_system_config config;
    // Array of pointers to controls, the system does not own these. The application does.
    u32 total_control_count;
    u32 active_control_count;
    sui_control** active_controls;
    u32 inactive_control_count;
    sui_control** inactive_controls;
} standard_ui_state;

b8 standard_ui_system_initialize(u64* memory_requirement, void* state, void* config) {
    if (!memory_requirement) {
        KERROR("standard_ui_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }
    standard_ui_system_config* typed_config = (standard_ui_system_config*)config;
    if (typed_config->max_control_count == 0) {
        KFATAL("standard_ui_system_initialize - config.max_control_count must be > 0.");
        return false;
    }

    // Memory layout: struct + active control array + inactive_control_array
    u64 struct_requirement = sizeof(standard_ui_state);
    u64 active_array_requirement = sizeof(sui_control) * typed_config->max_control_count;
    u64 inactive_array_requirement = sizeof(sui_control) * typed_config->max_control_count;
    *memory_requirement = struct_requirement + active_array_requirement + inactive_array_requirement;

    if (!state) {
        return true;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;
    typed_state->config = *typed_config;
    typed_state->active_controls = (void*)((u8*)state + struct_requirement);
    kzero_memory(typed_state->active_controls, sizeof(sui_control) * typed_config->max_control_count);
    typed_state->inactive_controls = (void*)((u8*)typed_state->active_controls + active_array_requirement);
    kzero_memory(typed_state->inactive_controls, sizeof(sui_control) * typed_config->max_control_count);

    KTRACE("Initialized standard UI system.");

    return true;
}

void standard_ui_system_shutdown(void* state) {
    if (state) {
        standard_ui_state* typed_state = (standard_ui_state*)state;
        // Unload and destroy inactive controls.
        for (u32 i = 0; i < typed_state->inactive_control_count; ++i) {
            sui_control* c = typed_state->inactive_controls[i];
            c->unload(c);
            c->destroy(c);
        }
        // Unload and destroy active controls.
        for (u32 i = 0; i < typed_state->active_control_count; ++i) {
            sui_control* c = typed_state->active_controls[i];
            c->unload(c);
            c->destroy(c);
        }
    }
}

b8 standard_ui_system_update(void* state, struct frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* c = typed_state->active_controls[i];
        c->update(c, p_frame_data);
    }

    return true;
}

b8 standard_ui_system_render(void* state, sui_control* root, struct frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    if (root->render) {
        if (!root->render(root, p_frame_data)) {
            KERROR("Root element failed to render. See logs for more details");
            return false;
        }
    }

    if (root->children) {
        u32 length = darray_length(root->children);
        for (u32 i = 0; i < length; ++i) {
            sui_control* c = root->children[i];
            if (!standard_ui_system_render(state, c, p_frame_data)) {
                KERROR("Child element failed to render. See logs for more details");
                return false;
            }
        }
    }

    return true;
}

b8 standard_ui_system_update_active(void* state, sui_control* control) {
    if (!state) {
        return false;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;
    u32* src_limit = control->is_active ? &typed_state->inactive_control_count : &typed_state->active_control_count;
    u32* dst_limit = control->is_active ? &typed_state->active_control_count : &typed_state->inactive_control_count;
    sui_control** src_array = control->is_active ? typed_state->inactive_controls : typed_state->active_controls;
    sui_control** dst_array = control->is_active ? typed_state->active_controls : typed_state->inactive_controls;
    for (u32 i = 0; i < *src_limit; ++i) {
        if (src_array[i] == control) {
            sui_control* c = src_array[i];
            dst_array[*dst_limit] = c;
            (*dst_limit)++;

            // Copy the rest of the array inward.
            kcopy_memory(((u8*)src_array) + (i * sizeof(sui_control*)), ((u8*)src_array) + ((i + 1) * sizeof(sui_control*)), sizeof(sui_control*) * ((*src_limit) - i));
            src_array[*src_limit] = 0;
            (*src_limit)--;
            return true;
        }
    }

    KERROR("Unable to find control to update active on, maybe control is not registered?");
    return false;
}

b8 stanard_ui_system_register_control(void* state, sui_control* control) {
    if (!state) {
        return false;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;
    if (typed_state->total_control_count >= typed_state->config.max_control_count) {
        KERROR("Unable to find free space to register sui control. Registration failed.");
        return false;
    }

    typed_state->total_control_count++;
    // Found available space, put it there.
    typed_state->inactive_controls[typed_state->inactive_control_count] = control;
    typed_state->inactive_control_count++;
    return true;
}
