#include "standard_ui_system.h"

#include <containers/darray.h>
#include <core/event.h>
#include <core/input.h>
#include <core/systems_manager.h>
#include <defines.h>
#include <identifiers/identifier.h>
#include <identifiers/khandle.h>
#include <input_types.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <math/math_types.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <resources/resource_types.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/geometry_system.h>
#include <systems/shader_system.h>
#include <systems/texture_system.h>
#include <systems/xform_system.h>

#include "core/engine.h"
#include "kohi.plugin.ui.standard_version.h"

static b8 standard_ui_system_mouse_down(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (mouse_buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        if (control->internal_mouse_down || control->on_mouse_down) {
            mat4 model = xform_world_get(control->xform);
            mat4 inv = mat4_inverse(model);
            vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
            if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
                control->is_pressed = true;
                if (control->internal_mouse_down) {
                    control->internal_mouse_down(typed_state, control, evt);
                }
                if (control->on_mouse_down) {
                    control->on_mouse_down(typed_state, control, evt);
                }
            }
        }
    }
    return false;
}
static b8 standard_ui_system_mouse_up(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (mouse_buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        control->is_pressed = false;

        if (control->internal_mouse_up || control->on_mouse_up) {
            mat4 model = xform_world_get(control->xform);
            mat4 inv = mat4_inverse(model);
            vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
            if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
                if (control->internal_mouse_up) {
                    control->internal_mouse_up(typed_state, control, evt);
                }
                if (control->on_mouse_up) {
                    control->on_mouse_up(typed_state, control, evt);
                }
            }
        }
    }
    return false;
}
static b8 standard_ui_system_click(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (mouse_buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        if (control->on_click || control->internal_click) {
            mat4 model = xform_world_get(control->xform);
            mat4 inv = mat4_inverse(model);
            vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
            if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
                if (control->internal_click) {
                    control->internal_click(typed_state, control, evt);
                }
                if (control->on_click) {
                    control->on_click(typed_state, control, evt);
                }
            }
        }
    }
    return false;
}

static b8 standard_ui_system_move(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.x = context.data.i16[0];
    evt.y = context.data.i16[1];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        if (control->on_mouse_over || control->on_mouse_out || control->internal_mouse_over || control->internal_mouse_out) {
            mat4 model = xform_world_get(control->xform);
            mat4 inv = mat4_inverse(model);
            vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
            vec2 transformed_vec2 = (vec2){transformed_evt.x, transformed_evt.y};
            if (rect_2d_contains_point(control->bounds, transformed_vec2)) {
                KTRACE("Button hover: %s", control->name);
                if (!control->is_hovered) {
                    control->is_hovered = true;
                    if (control->internal_mouse_over) {
                        control->internal_mouse_over(typed_state, control, evt);
                    }
                    if (control->on_mouse_over) {
                        control->on_mouse_over(typed_state, control, evt);
                    }
                }

                // Move events are only triggered while actually over the control.
                if (control->internal_mouse_move) {
                    control->internal_mouse_move(typed_state, control, evt);
                }
                if (control->on_mouse_move) {
                    control->on_mouse_move(typed_state, control, evt);
                }
            } else {
                if (control->is_hovered) {
                    control->is_hovered = false;
                    if (control->internal_mouse_out) {
                        control->internal_mouse_out(typed_state, control, evt);
                    }
                    if (control->on_mouse_out) {
                        control->on_mouse_out(typed_state, control, evt);
                    }
                }
            }
        }
    }
    return false;
}

static void texture_resource_loaded(kresource* resource, void* listener) {
    standard_ui_state* state = (standard_ui_state*)listener;

    // Setup the texture map.
    kresource_texture_map* map = &state->atlas;
    map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    map->filter_minify = map->filter_magnify = TEXTURE_FILTER_MODE_NEAREST;
    map->texture = &state->atlas_texture;
    if (!renderer_kresource_texture_map_resources_acquire(state->renderer, map)) {
        KERROR("Unable to acquire texture map resources. StandardUI cannot be initialized.");
    }
}

b8 standard_ui_system_initialize(u64* memory_requirement, standard_ui_state* state, standard_ui_system_config* config) {
    if (!memory_requirement) {
        KERROR("standard_ui_system_initialize requires a valid pointer to memory_requirement.");
        return false;
    }
    if (config->max_control_count == 0) {
        KFATAL("standard_ui_system_initialize - config.max_control_count must be > 0.");
        return false;
    }

    // Memory layout: struct + active control array + inactive_control_array
    u64 struct_requirement = sizeof(standard_ui_state);
    u64 active_array_requirement = sizeof(sui_control) * config->max_control_count;
    u64 inactive_array_requirement = sizeof(sui_control) * config->max_control_count;
    *memory_requirement = struct_requirement + active_array_requirement + inactive_array_requirement;

    if (!state) {
        return true;
    }

    state->renderer = engine_systems_get()->renderer_system;

    state->config = *config;
    state->active_controls = (void*)((u8*)state + struct_requirement);
    kzero_memory(state->active_controls, sizeof(sui_control) * config->max_control_count);
    state->inactive_controls = (void*)((u8*)state->active_controls + active_array_requirement);
    kzero_memory(state->inactive_controls, sizeof(sui_control) * config->max_control_count);

    sui_base_control_create(state, "__ROOT__", &state->root);

    // Atlas texture.
    b8 request_result = texture_system_request(
        kname_create("StandardUIAtlas"),
        kname_create("PluginUiStandard"),
        state,
        texture_resource_loaded,
        &state->atlas_texture);
    if (!request_result) {
        KERROR("Failed to request atlas texture for standard UI.");
        // TODO: use default texture instead.
        return false;
    }

    // Atlas texture map.
    kresource_texture_map* map = &state->atlas;
    map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    map->filter_minify = map->filter_magnify = TEXTURE_FILTER_MODE_NEAREST;
    map->texture = &state->atlas_texture;
    if (!renderer_kresource_texture_map_resources_acquire(state->renderer, map)) {
        return false;
        KERROR("Unable to acquire atlas texture map resources. StandardUI cannot be initialized.");
    }

    // Listen for input events.
    event_register(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
    event_register(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
    event_register(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
    event_register(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

    state->focused_id = INVALID_ID_U64;

    KTRACE("Initialized standard UI system (%s).", KVERSION);

    return true;
}

void standard_ui_system_shutdown(standard_ui_state* state) {
    if (state) {
        // Stop listening for input events.
        event_unregister(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
        event_unregister(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
        event_unregister(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
        event_unregister(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

        // Unload and destroy inactive controls.
        for (u32 i = 0; i < state->inactive_control_count; ++i) {
            sui_control* c = state->inactive_controls[i];
            c->unload(state, c);
            c->destroy(state, c);
        }
        // Unload and destroy active controls.
        for (u32 i = 0; i < state->active_control_count; ++i) {
            sui_control* c = state->active_controls[i];
            c->unload(state, c);
            c->destroy(state, c);
        }

        // Release texture map for UI atlas.
        renderer_kresource_texture_map_resources_release(state->renderer, &state->atlas);

        // Release texture for UI atlas.
        if (state->atlas.texture) {
            texture_system_release_resource(state->atlas.texture);
            state->atlas.texture = 0;
        }
    }
}

b8 standard_ui_system_update(standard_ui_state* state, struct frame_data* p_frame_data) {
    if (!state) {
        return false;
    }

    for (u32 i = 0; i < state->active_control_count; ++i) {
        sui_control* c = state->active_controls[i];
        c->update(state, c, p_frame_data);
    }

    return true;
}

void standard_ui_system_render_prepare_frame(standard_ui_state* state, const struct frame_data* p_frame_data) {
    if (!state) {
        return;
    }

    for (u32 i = 0; i < state->active_control_count; ++i) {
        sui_control* c = state->active_controls[i];
        if (c->render_prepare) {
            c->render_prepare(state, c, p_frame_data);
        }
    }
}

b8 standard_ui_system_render(standard_ui_state* state, sui_control* root, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!state) {
        return false;
    }

    render_data->ui_atlas = &state->atlas;

    if (!root) {
        root = &state->root;
    }

    if (root->render) {
        if (!root->render(state, root, p_frame_data, render_data)) {
            KERROR("Root element failed to render. See logs for more details");
            return false;
        }
    }

    if (root->children) {
        u32 length = darray_length(root->children);
        for (u32 i = 0; i < length; ++i) {
            sui_control* c = root->children[i];
            if (!c->is_visible) {
                continue;
            }
            if (!standard_ui_system_render(state, c, p_frame_data, render_data)) {
                KERROR("Child element failed to render. See logs for more details");
                return false;
            }
        }
    }

    return true;
}

b8 standard_ui_system_update_active(standard_ui_state* state, sui_control* control) {
    if (!state) {
        return false;
    }

    u32* src_limit = control->is_active ? &state->inactive_control_count : &state->active_control_count;
    u32* dst_limit = control->is_active ? &state->active_control_count : &state->inactive_control_count;
    sui_control** src_array = control->is_active ? state->inactive_controls : state->active_controls;
    sui_control** dst_array = control->is_active ? state->active_controls : state->inactive_controls;
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

b8 standard_ui_system_register_control(standard_ui_state* state, sui_control* control) {
    if (!state) {
        return false;
    }

    if (state->total_control_count >= state->config.max_control_count) {
        KERROR("Unable to find free space to register sui control. Registration failed.");
        return false;
    }

    state->total_control_count++;
    // Found available space, put it there.
    state->inactive_controls[state->inactive_control_count] = control;
    state->inactive_control_count++;
    return true;
}

b8 standard_ui_system_control_add_child(standard_ui_state* state, sui_control* parent, sui_control* child) {
    if (!child) {
        return false;
    }

    if (!parent) {
        parent = &state->root;
    }

    if (!parent->children) {
        parent->children = darray_create(sui_control*);
    }

    if (child->parent) {
        if (!standard_ui_system_control_remove_child(state, child->parent, child)) {
            KERROR("Failed to remove child from parent before reparenting.");
            return false;
        }
    }

    darray_push(parent->children, child);
    child->parent = parent;

    return true;
}

b8 standard_ui_system_control_remove_child(standard_ui_state* state, sui_control* parent, sui_control* child) {
    if (!parent || !child) {
        return false;
    }

    if (!parent->children) {
        KERROR("Cannot remove a child from a parent which has no children.");
        false;
    }

    u32 child_count = darray_length(parent->children);
    for (u32 i = 0; i < child_count; ++i) {
        if (parent->children[i] == child) {
            sui_control* popped;
            darray_pop_at(parent->children, i, &popped);
            child->parent = 0;

            return true;
        }
    }

    KERROR("Unable to remove child which is not a child of given parent.");
    return false;
}

void standard_ui_system_focus_control(standard_ui_state* state, sui_control* control) {
    state->focused_id = control ? control->id.uniqueid : INVALID_ID_U64;
}

b8 sui_base_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control) {
    if (!out_control) {
        return false;
    }

    // Set all controls to visible by default.
    out_control->is_visible = true;

    // Assign function pointers.
    out_control->destroy = sui_base_control_destroy;
    out_control->load = sui_base_control_load;
    out_control->unload = sui_base_control_unload;
    out_control->update = sui_base_control_update;
    out_control->render = sui_base_control_render;

    out_control->name = string_duplicate(name);
    out_control->id = identifier_create();

    out_control->xform = xform_create();

    return true;
}
void sui_base_control_destroy(standard_ui_state* state, struct sui_control* self) {
    if (self) {
        // TODO: recurse children/unparent?

        if (self->internal_data && self->internal_data_size) {
            kfree(self->internal_data, self->internal_data_size, MEMORY_TAG_UI);
        }
        if (self->name) {
            string_free(self->name);
        }
        kzero_memory(self, sizeof(sui_control));
    }
}

b8 sui_base_control_load(standard_ui_state* state, struct sui_control* self) {
    if (!self) {
        return false;
    }

    return true;
}
void sui_base_control_unload(standard_ui_state* state, struct sui_control* self) {
    if (!self) {
        //
    }
}

static void sui_recalculate_world_xform(standard_ui_state* state, struct sui_control* self) {
    xform_calculate_local(self->xform);
    mat4 local = xform_local_get(self->xform);

    if (self->parent) {
        sui_recalculate_world_xform(state, self->parent);
        mat4 parent_world = xform_world_get(self->parent->xform);
        mat4 self_world = mat4_mul(local, parent_world);
        xform_world_set(self->xform, self_world);
    } else {
        xform_world_set(self->xform, local);
    }
}

b8 sui_base_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    sui_recalculate_world_xform(state, self);

    return true;
}
b8 sui_base_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!self) {
        return false;
    }

    return true;
}

void sui_control_position_set(standard_ui_state* state, struct sui_control* self, vec3 position) {
    xform_position_set(self->xform, position);
}

vec3 sui_control_position_get(standard_ui_state* state, struct sui_control* self) {
    return xform_position_get(self->xform);
}
