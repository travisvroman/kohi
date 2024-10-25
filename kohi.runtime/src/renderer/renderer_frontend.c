#include "renderer_frontend.h"

#include "containers/darray.h"
#include "containers/freelist.h"
#include "containers/hashtable.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kvar.h"
#include "debug/kassert.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "platform/platform.h"
#include "renderer/renderer_types.h"
#include "renderer/renderer_utils.h"
#include "renderer/viewport.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/material_system.h"
#include "systems/plugin_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

struct texture_internal_data;

typedef struct texture_lookup {
    u64 uniqueid;
    struct texture_internal_data* data;
} texture_lookup;

typedef struct renderer_dynamic_state {
    vec4 viewport;
    vec4 scissor;

    b8 depth_test_enabled;
    b8 depth_write_enabled;
    b8 stencil_test_enabled;

    u32 stencil_reference;
    u32 stencil_compare_mask;
    u32 stencil_write_mask;

    renderer_stencil_op fail_op;
    renderer_stencil_op pass_op;
    renderer_stencil_op depth_fail_op;
    renderer_compare_op compare_op;

    renderer_winding winding;
} renderer_dynamic_state;

typedef struct renderer_system_state {
    /** @brief The current frame number. */
    u64 frame_number;
    // The viewport information for the given window.
    struct viewport* active_viewport;

    // The actual loaded plugin obtained from the plugin system.
    kruntime_plugin* backend_plugin;
    // The interface to the backend plugin. This is a cold-cast from backend_plugin->plugin_state.
    renderer_backend_interface* backend;

    // darray Collection of renderer-specific texture data.
    texture_lookup* textures;

    // The number of render targets. Typically lines up with the amount of swapchain images.
    // NOTE: Standardizing the rule here that all windows should have the same number here.
    u8 render_target_count;

    /** @brief The object vertex buffer, used to hold geometry vertices. */
    renderbuffer geometry_vertex_buffer;
    /** @brief The object index buffer, used to hold geometry indices. */
    renderbuffer geometry_index_buffer;

    // Renderer options.
    // Use PCF filtering
    b8 use_pcf;

    // Current dynamic state settings.
    renderer_dynamic_state dynamic_state;
    // Frame defaults - dynamic state settings that are reapplied at the beginning of every frame.
    renderer_dynamic_state frame_default_dynamic_state;
} renderer_system_state;

static void reapply_dynamic_state(renderer_system_state* state, const renderer_dynamic_state* dynamic_state);

b8 renderer_system_deserialize_config(const char* config_str, renderer_system_config* out_config) {
    if (!config_str || !out_config) {
        KERROR("renderer_system_deserialize_config requires a valid pointer to out_config and config_str");
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(config_str, &tree)) {
        KERROR("Failed to parse renderer system config.");
        return false;
    }

    // backend_plugin_name is required.
    if (!kson_object_property_value_get_string(&tree.root, "backend_plugin_name", &out_config->backend_plugin_name)) {
        KERROR("renderer_system_deserialize_config: config does not contain backend_plugin_name, which is required.");
        return false;
    }

    if (!kson_object_property_value_get_bool(&tree.root, "vsync", &out_config->vsync)) {
        // Default to true if not defined.
        out_config->vsync = true;
    }

    if (!kson_object_property_value_get_bool(&tree.root, "enable_validation", &out_config->enable_validation)) {
        // Default to true if not defined.
        out_config->enable_validation = true;
    }

    if (!kson_object_property_value_get_bool(&tree.root, "power_saving", &out_config->power_saving)) {
        // Default to true if not defined.
        out_config->power_saving = true;
    }

    kson_tree_cleanup(&tree);

    return true;
}

static b8 renderer_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_KVAR_CHANGED) {
        renderer_system_state* state = listener_inst;
        kvar_change* change = context.data.custom_data.data;
        if (strings_equali("use_pcf", change->name)) {
            i32 use_pcf_val;
            kvar_i32_get("use_pcf", &use_pcf_val);
            state->use_pcf = use_pcf_val == 0 ? false : true;
            return true;
        }
    }

    return false;
}

b8 renderer_system_initialize(u64* memory_requirement, renderer_system_state* state, const renderer_system_config* config) {
    *memory_requirement = sizeof(renderer_system_state);
    if (state == 0) {
        return true;
    }

    // Get the configured plugin.
    const engine_system_states* systems = engine_systems_get();
    state->backend_plugin = plugin_system_get(systems->plugin_system, config->backend_plugin_name);
    if (!state->backend_plugin) {
        KERROR("Failed to load required backend plugin for renderer. See logs for details.");
        return false;
    }

    // Cold-cast to the known type and keep a convenience pointer.
    state->backend = (renderer_backend_interface*)state->backend_plugin->plugin_state;
    // Keep a copy of the frontend state for the backend.
    state->backend->frontend_state = state;

    state->frame_number = 0;
    state->active_viewport = 0;

    // FIXME: Have the backend query the frontend for these properties instead.
    renderer_backend_config renderer_config = {};
    renderer_config.application_name = config->application_name;
    renderer_config.flags = 0;
    if (config->vsync) {
        renderer_config.flags |= RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT;
    }
    if (config->enable_validation) {
        renderer_config.flags |= RENDERER_CONFIG_FLAG_ENABLE_VALIDATION;
    }
    if (config->power_saving) {
        renderer_config.flags |= RENDERER_CONFIG_FLAG_POWER_SAVING_BIT;
    }

    // Create the vsync kvar
    kvar_i32_set("vsync", 0, (renderer_config.flags & RENDERER_CONFIG_FLAG_VSYNC_ENABLED_BIT) ? 1 : 0);

    // Renderer options
    // Add a kvar to track PCF filtering enabled/disabled.
    kvar_i32_set("use_pcf", 0, 1); // On by default.
    i32 use_pcf_val;
    kvar_i32_get("use_pcf", &use_pcf_val);
    state->use_pcf = use_pcf_val == 0 ? false : true;
    event_register(EVENT_CODE_KVAR_CHANGED, state, renderer_on_event);

    // Initialize the backend.
    if (!state->backend->initialize(state->backend, &renderer_config)) {
        KERROR("Renderer backend failed to initialize. Shutting down.");
        return false;
    }

    // Default dynamic state settings.
    state->dynamic_state.viewport = (vec4){0, 0, 1280, 720};
    state->dynamic_state.scissor = (vec4){0, 0, 1280, 720};
    state->dynamic_state.depth_test_enabled = true;
    state->dynamic_state.depth_write_enabled = true;
    state->dynamic_state.stencil_test_enabled = false;
    state->dynamic_state.stencil_reference = 0;
    state->dynamic_state.stencil_write_mask = 0;
    state->dynamic_state.fail_op = RENDERER_STENCIL_OP_KEEP;
    state->dynamic_state.pass_op = RENDERER_STENCIL_OP_REPLACE;
    state->dynamic_state.depth_fail_op = RENDERER_STENCIL_OP_KEEP;
    state->dynamic_state.compare_op = RENDERER_COMPARE_OP_ALWAYS;
    state->dynamic_state.winding = RENDERER_WINDING_COUNTER_CLOCKWISE;

    // Take a copy as the frame default.
    state->frame_default_dynamic_state = state->dynamic_state;

    // Geometry vertex buffer
    // TODO: make this configurable.
    const u64 vertex_buffer_size = sizeof(vertex_3d) * 20 * 1024 * 1024;
    if (!renderer_renderbuffer_create("renderbuffer_vertexbuffer_globalgeometry", RENDERBUFFER_TYPE_VERTEX, vertex_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST, &state->geometry_vertex_buffer)) {
        KERROR("Error creating vertex buffer.");
        return false;
    }
    renderer_renderbuffer_bind(&state->geometry_vertex_buffer, 0);

    // Geometry index buffer
    // TODO: Make this configurable.
    const u64 index_buffer_size = sizeof(u32) * 100 * 1024 * 1024;
    if (!renderer_renderbuffer_create("renderbuffer_indexbuffer_globalgeometry", RENDERBUFFER_TYPE_INDEX, index_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST, &state->geometry_index_buffer)) {
        KERROR("Error creating index buffer.");
        return false;
    }
    renderer_renderbuffer_bind(&state->geometry_index_buffer, 0);

    return true;
}

void renderer_system_shutdown(renderer_system_state* state) {
    if (state) {
        renderer_system_state* typed_state = (renderer_system_state*)state;

        renderer_wait_for_idle();

        // Destroy buffers.
        renderer_renderbuffer_destroy(&typed_state->geometry_vertex_buffer);
        renderer_renderbuffer_destroy(&typed_state->geometry_index_buffer);

        // Shutdown the plugin
        typed_state->backend->shutdown(typed_state->backend);
    }
}

u64 renderer_system_frame_number_get(struct renderer_system_state* state) {
    if (!state) {
        return INVALID_ID_U64;
    }
    return state->frame_number;
}

b8 renderer_on_window_created(struct renderer_system_state* state, struct kwindow* window) {
    if (!window) {
        KERROR("renderer_on_window_created requires a valid pointer to a window");
        return false;
    }

    // Create a new window state and register it.
    window->renderer_state = kallocate(sizeof(kwindow_renderer_state), MEMORY_TAG_RENDERER);

    window->renderer_state->colourbuffer = kallocate(sizeof(kresource_texture), MEMORY_TAG_RENDERER);
    window->renderer_state->depthbuffer = kallocate(sizeof(kresource_texture), MEMORY_TAG_RENDERER);

    // Start with invalid colour/depth buffer texture handles. // nocheckin
    window->renderer_state->colourbuffer->renderer_texture_handle = k_handle_invalid();
    window->renderer_state->depthbuffer->renderer_texture_handle = k_handle_invalid();

    // Create backend resources (i.e swapchain, surface, images, etc.).
    if (!state->backend->window_create(state->backend, window)) {
        KERROR("Renderer backend failed to create resources for new window. See logs for details.");
        return false;
    }

    return true;
}

void renderer_on_window_destroyed(struct renderer_system_state* state, struct kwindow* window) {
    if (window) {

        // Destroy on backend first.
        state->backend->window_destroy(state->backend, window);
    }
}

void renderer_on_window_resized(struct renderer_system_state* state, const struct kwindow* window) {
    state->backend->window_resized(state->backend, window);
}

void renderer_begin_debug_label(const char* label_text, vec3 colour) {
#ifdef _DEBUG
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (state_ptr) {
        state_ptr->backend->begin_debug_label(state_ptr->backend, label_text, colour);
    }
#endif
}

void renderer_end_debug_label(void) {
#ifdef _DEBUG
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (state_ptr) {
        state_ptr->backend->end_debug_label(state_ptr->backend);
    }
#endif
}

b8 renderer_frame_prepare(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    KASSERT(state && p_frame_data);

    // Increment the frame number.
    // This always occurs no matter what, even if a frame doesn't wind up rendering.
    state->frame_number++;

    return state->backend->frame_prepare(state->backend, p_frame_data);
}

b8 renderer_frame_prepare_window_surface(struct renderer_system_state* state, struct kwindow* window, struct frame_data* p_frame_data) {
    KASSERT(state && window && p_frame_data);

    // Fire off to the backend.
    return state->backend->frame_prepare_window_surface(state->backend, window, p_frame_data);
}

b8 renderer_frame_command_list_begin(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    b8 result = state->backend->frame_commands_begin(state->backend, p_frame_data);

    // Reapply frame defaults if successful.
    if (result) {
        reapply_dynamic_state(state, &state->frame_default_dynamic_state);
    }

    return result;
}

b8 renderer_frame_command_list_end(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    return state->backend->frame_commands_end(state->backend, p_frame_data);
}

b8 renderer_frame_submit(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    return state->backend->frame_submit(state->backend, p_frame_data);
}

b8 renderer_frame_present(struct renderer_system_state* state, struct kwindow* window, struct frame_data* p_frame_data) {

    // End the frame. If this fails, it is likely unrecoverable.
    return state->backend->frame_present(state->backend, window, p_frame_data);
}

void renderer_viewport_set(vec4 rect) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.viewport = rect;
    state_ptr->backend->viewport_set(state_ptr->backend, rect);
}

void renderer_viewport_reset(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->viewport_reset(state_ptr->backend);
}

void renderer_scissor_set(vec4 rect) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.scissor = rect;
    state_ptr->backend->scissor_set(state_ptr->backend, rect);
}

void renderer_scissor_reset(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->scissor_reset(state_ptr->backend);
}

void renderer_winding_set(renderer_winding winding) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.winding = winding;
    state_ptr->backend->winding_set(state_ptr->backend, winding);
}

void renderer_set_stencil_test_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.stencil_test_enabled = enabled;
    state_ptr->backend->set_stencil_test_enabled(state_ptr->backend, enabled);
}

void renderer_set_stencil_reference(u32 reference) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.stencil_reference = reference;
    state_ptr->backend->set_stencil_reference(state_ptr->backend, reference);
}

void renderer_set_depth_test_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.depth_test_enabled = enabled;
    state_ptr->backend->set_depth_test_enabled(state_ptr->backend, enabled);
}

void renderer_set_depth_write_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Cache dynamic state.
    state_ptr->dynamic_state.depth_write_enabled = enabled;
    state_ptr->backend->set_depth_write_enabled(state_ptr->backend, enabled);
}

void renderer_set_stencil_op(renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Cache dynamic state.
    state_ptr->dynamic_state.fail_op = fail_op;
    state_ptr->dynamic_state.pass_op = pass_op;
    state_ptr->dynamic_state.depth_fail_op = depth_fail_op;
    state_ptr->dynamic_state.compare_op = compare_op;
    state_ptr->backend->set_stencil_op(state_ptr->backend, fail_op, pass_op, depth_fail_op, compare_op);
}

void renderer_begin_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data, rect_2d render_area, u32 colour_target_count, k_handle* colour_targets, k_handle depth_stencil_target, u32 depth_stencil_layer) {
    struct texture_internal_data** colour_datas = 0;
    KASSERT_MSG(render_area.width != 0 && render_area.height != 0, "renderer_begin_rendering must have a width and height.");
    if (colour_target_count) {
        if (colour_target_count == 1) {
            // Optimization: Skip array allocation and just pass through the address of it.
            if (k_handle_is_invalid(colour_targets[0])) {
                KFATAL("Passed invalid handle to texture target when beginning rendering. Null is used, and will likely cause a failure.");
            } else {
                colour_datas = &state->textures[colour_targets[0].handle_index].data;
            }
        } else {
            colour_datas = p_frame_data->allocator.allocate(sizeof(struct texture_internal_data*) * colour_target_count);
            for (u32 i = 0; i < colour_target_count; ++i) {
                if (k_handle_is_invalid(colour_targets[i])) {
                    KFATAL("Passed invalid handle to texture target when beginning rendering. Null is used, and will likely cause a failure.");
                    colour_datas[i] = 0;
                } else {
                    colour_datas[i] = state->textures[colour_targets[i].handle_index].data;
                }
            }
        }
    }

    struct texture_internal_data* depth_data = 0;
    if (!k_handle_is_invalid(depth_stencil_target)) {
        depth_data = state->textures[depth_stencil_target.handle_index].data;
    }
    state->backend->begin_rendering(state->backend, p_frame_data, render_area, colour_target_count, colour_datas, depth_data, depth_stencil_layer);

    // Dynamic state needs to be reapplied here in case the backend needs it.
    reapply_dynamic_state(state, &state->dynamic_state);
}

void renderer_end_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    state->backend->end_rendering(state->backend, p_frame_data);
}

void renderer_set_stencil_compare_mask(u32 compare_mask) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->dynamic_state.stencil_compare_mask = compare_mask;
    state_ptr->backend->set_stencil_compare_mask(state_ptr->backend, compare_mask);
}

void renderer_set_stencil_write_mask(u32 write_mask) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_stencil_write_mask(state_ptr->backend, write_mask);
}

/* b8 renderer_texture_resources_acquire(struct renderer_system_state* state, const char* name, texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, texture_flag_bits flags, k_handle* out_renderer_texture_handle) {
    if (!state) {
        return false;
    }

    if (!state->textures) {
        state->textures = darray_create(texture_lookup);
    }

    // TODO: Upon backend creation, setup a large contiguous array of some configured amount of these, and retrieve from there.
    struct texture_internal_data* data = kallocate(state->backend->texture_internal_data_size, MEMORY_TAG_RENDERER);
    b8 success;
    if (flags & TEXTURE_FLAG_IS_WRAPPED) {
        // If the texure is considered "wrapped" (i.e. internal resources are created somwhere else,
        // such as swapchain images), then don't reach out to the backend to create resources. Just
        // count it as a success and proceed to get a handle.
        success = true;
    } else {
        success = state->backend->texture_resources_acquire(state->backend, data, name, type, width, height, channel_count, mip_levels, array_size, flags);
    }

    // Only insert into the lookup table on success.
    if (success) {
        u32 texture_count = darray_length(state->textures);
        for (u32 i = 0; i < texture_count; ++i) {
            texture_lookup* lookup = &state->textures[i];
            if (lookup->uniqueid == INVALID_ID_U64) {
                // Found a free "slot", use it.
                k_handle new_handle = k_handle_create(i);
                lookup->uniqueid = new_handle.unique_id.uniqueid;
                lookup->data = data;
                *out_renderer_texture_handle = new_handle;
                return success;
            }
        }

        // No free "slots", add one.
        texture_lookup new_lookup = {0};
        k_handle new_handle = k_handle_create(texture_count);
        new_lookup.uniqueid = new_handle.unique_id.uniqueid;
        new_lookup.data = data;
        darray_push(state->textures, new_lookup);
        *out_renderer_texture_handle = new_handle;
    } else {
        KERROR("Failed to acquire texture resources. See logs for details.");
        kfree(data, state->backend->texture_internal_data_size, MEMORY_TAG_RENDERER);
    }
    return success;
} */

b8 renderer_kresource_texture_resources_acquire(struct renderer_system_state* state, kname name, kresource_texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, kresource_texture_flag_bits flags, k_handle* out_renderer_texture_handle) {
    if (!state) {
        return false;
    }

    if (!state->textures) {
        state->textures = darray_create(texture_lookup);
    }

    struct texture_internal_data* data = kallocate(state->backend->texture_internal_data_size, MEMORY_TAG_RENDERER);
    b8 success;
    if (flags & KRESOURCE_TEXTURE_FLAG_IS_WRAPPED) {
        // If the texure is considered "wrapped" (i.e. internal resources are created somwhere else,
        // such as swapchain images), then don't reach out to the backend to create resources. Just
        // count it as a success and proceed to get a handle.
        success = true;
    } else {
        success = state->backend->texture_resources_acquire(state->backend, data, kname_string_get(name), type, width, height, channel_count, mip_levels, array_size, flags);
    }

    // Only insert into the lookup table on success.
    if (success) {
        u32 texture_count = darray_length(state->textures);
        for (u32 i = 0; i < texture_count; ++i) {
            texture_lookup* lookup = &state->textures[i];
            if (lookup->uniqueid == INVALID_ID_U64) {
                // Found a free "slot", use it.
                k_handle new_handle = k_handle_create(i);
                lookup->uniqueid = new_handle.unique_id.uniqueid;
                lookup->data = data;
                *out_renderer_texture_handle = new_handle;
                return success;
            }
        }

        // No free "slots", add one.
        texture_lookup new_lookup = {0};
        k_handle new_handle = k_handle_create(texture_count);
        new_lookup.uniqueid = new_handle.unique_id.uniqueid;
        new_lookup.data = data;
        darray_push(state->textures, new_lookup);
        *out_renderer_texture_handle = new_handle;
    } else {
        KERROR("Failed to acquire texture resources. See logs for details.");
        kfree(data, state->backend->texture_internal_data_size, MEMORY_TAG_RENDERER);
    }
    return success;
}

void renderer_texture_resources_release(struct renderer_system_state* state, k_handle* renderer_texture_handle) {
    if (state && !k_handle_is_invalid(*renderer_texture_handle)) {
        texture_lookup* lookup = &state->textures[renderer_texture_handle->handle_index];
        if (lookup->uniqueid != renderer_texture_handle->unique_id.uniqueid) {
            KWARN("Stale handle passed while trying to release renderer texture resources.");
            return;
        }
        state->backend->texture_resources_release(state->backend, lookup->data);
        kfree(lookup->data, state->backend->texture_internal_data_size, MEMORY_TAG_RENDERER);
        lookup->data = 0;
        lookup->uniqueid = INVALID_ID_U64;
        *renderer_texture_handle = k_handle_invalid();
    }
}

struct texture_internal_data* renderer_texture_resources_get(struct renderer_system_state* state, k_handle renderer_texture_handle) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        texture_lookup* lookup = &state->textures[renderer_texture_handle.handle_index];
        if (lookup->uniqueid != renderer_texture_handle.unique_id.uniqueid) {
            KWARN("Stale handle passed while trying to get renderer texture resources. Nothing will be returned");
            return 0;
        }
        return lookup->data;
    }
    return 0;
}

b8 renderer_texture_write_data(struct renderer_system_state* state, k_handle renderer_texture_handle, u32 offset, u32 size, const u8* pixels) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        struct texture_internal_data* data = state->textures[renderer_texture_handle.handle_index].data;
        b8 include_in_frame_workload = true;
        b8 result = state->backend->texture_write_data(state->backend, data, offset, size, pixels, include_in_frame_workload);
        if (!include_in_frame_workload) {
            // TODO: update generation?
        }
        return result;
    }
    return false;
}

b8 renderer_texture_read_data(struct renderer_system_state* state, k_handle renderer_texture_handle, u32 offset, u32 size, u8** out_pixels) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        struct texture_internal_data* data = state->textures[renderer_texture_handle.handle_index].data;
        return state->backend->texture_read_data(state->backend, data, offset, size, out_pixels);
    }
    return false;
}

b8 renderer_texture_read_pixel(struct renderer_system_state* state, k_handle renderer_texture_handle, u32 x, u32 y, u8** out_rgba) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        struct texture_internal_data* data = state->textures[renderer_texture_handle.handle_index].data;
        return state->backend->texture_read_pixel(state->backend, data, x, y, out_rgba);
    }
    return false;
}

b8 renderer_texture_resize(struct renderer_system_state* state, k_handle renderer_texture_handle, u32 new_width, u32 new_height) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        struct texture_internal_data* data = state->textures[renderer_texture_handle.handle_index].data;
        return state->backend->texture_resize(state->backend, data, new_width, new_height);
    }
    return false;
}

struct texture_internal_data* renderer_texture_internal_get(struct renderer_system_state* state, k_handle renderer_texture_handle) {
    if (state && !k_handle_is_invalid(renderer_texture_handle)) {
        return state->textures[renderer_texture_handle.handle_index].data;
    }
    return 0;
}

renderbuffer* renderer_renderbuffer_get(renderbuffer_type type) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    switch (type) {
    case RENDERBUFFER_TYPE_VERTEX:
        return &state_ptr->geometry_vertex_buffer;
    case RENDERBUFFER_TYPE_INDEX:
        return &state_ptr->geometry_index_buffer;
    default:
        KERROR("Unsupported buffer type %u", type);
        return 0;
    }
}

b8 renderer_geometry_upload(kgeometry* g) {
    if (!g) {
        KERROR("renderer_geometry_upload requires a valid pointer to geometry.");
        return false;
    }
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;

    b8 is_reupload = g->generation != INVALID_ID_U16;
    u64 vertex_size = (u64)(g->vertex_element_size * g->vertex_count);
    u64 vertex_offset = 0;
    u64 index_size = (u64)(g->index_element_size * g->index_count);
    u64 index_offset = 0;
    // Vertex data.
    if (!is_reupload) {
        // Allocate space in the buffer.
        if (!renderer_renderbuffer_allocate(&state_ptr->geometry_vertex_buffer, vertex_size, &g->vertex_buffer_offset)) {
            KERROR("vulkan_renderer_geometry_upload failed to allocate from the vertex buffer!");
            return false;
        }
    }

    // Load the data.
    // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
    if (!renderer_renderbuffer_load_range(&state_ptr->geometry_vertex_buffer, g->vertex_buffer_offset + vertex_offset, vertex_size, g->vertices + vertex_offset, false)) {
        KERROR("vulkan_renderer_geometry_upload failed to upload to the vertex buffer!");
        return false;
    }

    // Index data, if applicable
    if (index_size) {
        if (!is_reupload) {
            // Allocate space in the buffer.
            if (!renderer_renderbuffer_allocate(&state_ptr->geometry_index_buffer, index_size, &g->index_buffer_offset)) {
                KERROR("vulkan_renderer_geometry_upload failed to allocate from the index buffer!");
                return false;
            }
        }

        // Load the data.
        // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
        if (!renderer_renderbuffer_load_range(&state_ptr->geometry_index_buffer, g->index_buffer_offset + index_offset, index_size, g->indices + index_offset, false)) {
            KERROR("vulkan_renderer_geometry_upload failed to upload to the index buffer!");
            return false;
        }
    }

    g->generation++;

    return true;
}

void renderer_geometry_vertex_update(geometry* g, u32 offset, u32 vertex_count, void* vertices, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Load the data.
    u32 size = g->vertex_element_size * vertex_count;
    if (!renderer_renderbuffer_load_range(&state_ptr->geometry_vertex_buffer, g->vertex_buffer_offset + offset, size, vertices + offset, include_in_frame_workload)) {
        KERROR("vulkan_renderer_geometry_vertex_update failed to upload to the vertex buffer!");
    }
}

void renderer_geometry_destroy(geometry* g) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;

    if (g->generation != INVALID_ID_U16) {
        // Free vertex data
        u64 vertex_data_size = g->vertex_element_size * g->vertex_count;
        if (vertex_data_size) {
            if (!renderer_renderbuffer_free(&state_ptr->geometry_vertex_buffer, vertex_data_size, g->vertex_buffer_offset)) {
                KERROR("vulkan_renderer_destroy_geometry failed to free vertex buffer range.");
            }
        }

        // Free index data, if applicable
        u64 index_data_size = g->index_element_size * g->index_count;
        if (index_data_size) {
            if (!renderer_renderbuffer_free(&state_ptr->geometry_index_buffer, index_data_size, g->index_buffer_offset)) {
                KERROR("vulkan_renderer_destroy_geometry failed to free index buffer range.");
            }
        }

        g->generation = INVALID_ID_U16;
    }

    if (g->vertices) {
        kfree(g->vertices, g->vertex_element_size * g->vertex_count, MEMORY_TAG_RENDERER);
    }
    if (g->indices) {
        kfree(g->indices, g->index_element_size * g->index_count, MEMORY_TAG_RENDERER);
    }
}

void renderer_geometry_draw(geometry_render_data* data) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    b8 includes_index_data = data->index_count > 0;
    if (!renderer_renderbuffer_draw(&state_ptr->geometry_vertex_buffer, data->vertex_buffer_offset, data->vertex_count, includes_index_data)) {
        KERROR("vulkan_renderer_draw_geometry failed to draw vertex buffer;");
        return;
    }

    if (includes_index_data) {
        if (!renderer_renderbuffer_draw(&state_ptr->geometry_index_buffer, data->index_buffer_offset, data->index_count, !includes_index_data)) {
            KERROR("vulkan_renderer_draw_geometry failed to draw index buffer;");
            return;
        }
    }
}

void renderer_clear_colour_set(struct renderer_system_state* state, vec4 colour) {
    if (state) {
        state->backend->clear_colour_set(state->backend, colour);
    }
}

void renderer_clear_depth_set(struct renderer_system_state* state, f32 depth) {
    if (state) {
        state->backend->clear_depth_set(state->backend, depth);
    }
}

void renderer_clear_stencil_set(struct renderer_system_state* state, u32 stencil) {
    if (state) {
        state->backend->clear_stencil_set(state->backend, stencil);
    }
}

b8 renderer_clear_colour(struct renderer_system_state* state, k_handle texture_handle) {
    if (state && !k_handle_is_invalid(texture_handle)) {
        struct texture_internal_data* data = state->textures[texture_handle.handle_index].data;
        state->backend->clear_colour(state->backend, data);
        return true;
    }

    KERROR("renderer_clear_colour_texture requires a valid handle to a texture. Nothing was done.");
    return false;
}

b8 renderer_clear_depth_stencil(struct renderer_system_state* state, k_handle texture_handle) {
    if (state && !k_handle_is_invalid(texture_handle)) {
        struct texture_internal_data* data = state->textures[texture_handle.handle_index].data;
        state->backend->clear_depth_stencil(state->backend, data);
        return true;
    }

    KERROR("renderer_clear_depth_stencil requires a valid handle to a texture. Nothing was done.");
    return false;
}

void renderer_colour_texture_prepare_for_present(struct renderer_system_state* state, k_handle texture_handle) {
    if (state && !k_handle_is_invalid(texture_handle)) {
        struct texture_internal_data* data = state->textures[texture_handle.handle_index].data;
        state->backend->colour_texture_prepare_for_present(state->backend, data);
        return;
    }

    KERROR("renderer_colour_texture_prepare_for_present requires a valid handle to a texture. Nothing was done.");
}

void renderer_texture_prepare_for_sampling(struct renderer_system_state* state, k_handle texture_handle, texture_flag_bits flags) {
    if (state && !k_handle_is_invalid(texture_handle)) {
        struct texture_internal_data* data = state->textures[texture_handle.handle_index].data;
        state->backend->texture_prepare_for_sampling(state->backend, data, flags);
        return;
    }

    KERROR("renderer_texture_prepare_for_sampling requires a valid handle to a texture. Nothing was done.");
}

b8 renderer_shader_create(struct renderer_system_state* state, shader* s, const shader_config* config) {

    // Get the uniform counts.
    s->global_uniform_count = 0;
    // Number of samplers in the shader, per frame. NOT the number of descriptors needed (i.e could be an array).
    s->global_uniform_sampler_count = 0;
    s->global_sampler_indices = darray_create(u32);
    s->instance_uniform_count = 0;
    // Number of samplers in the shader, per instance, per frame. NOT the number of descriptors needed (i.e could be an array).
    s->instance_uniform_sampler_count = 0;
    s->instance_sampler_indices = darray_create(u32);
    s->local_uniform_count = 0;

    s->shader_stage_count = config->stage_count;

    // Examine the uniforms and determine scope as well as a count of samplers.
    u32 total_count = darray_length(config->uniforms);
    for (u32 i = 0; i < total_count; ++i) {
        switch (config->uniforms[i].scope) {
        case SHADER_SCOPE_GLOBAL:
            if (uniform_type_is_sampler(config->uniforms[i].type)) {
                s->global_uniform_sampler_count++;
                darray_push(s->global_sampler_indices, i);
            } else {
                s->global_uniform_count++;
            }
            break;
        case SHADER_SCOPE_INSTANCE:
            if (uniform_type_is_sampler(config->uniforms[i].type)) {
                s->instance_uniform_sampler_count++;
                darray_push(s->instance_sampler_indices, i);
            } else {
                s->instance_uniform_count++;
            }
            break;
        case SHADER_SCOPE_LOCAL:
            s->local_uniform_count++;
            break;
        }
    }

    // Examine shader stages and load shader source as required. This source is
    // then fed to the backend renderer, which stands up any shader program resources
    // as required.
    // TODO: Implement #include directives here at this level so it's handled the same
    // regardless of what backend is being used.

    s->stage_configs = kallocate(sizeof(shader_stage_config) * config->stage_count, MEMORY_TAG_ARRAY);

#ifdef _DEBUG
    // NOTE: Only watch module files for debug builds.
    s->module_watch_ids = kallocate(sizeof(u32) * config->stage_count, MEMORY_TAG_ARRAY);
#endif
    // Each stage.
    for (u8 i = 0; i < config->stage_count; ++i) {
        s->stage_configs[i].stage = config->stage_configs[i].stage;
        s->stage_configs[i].filename = string_duplicate(config->stage_configs[i].filename);
        // Read the resource.
        resource text_resource;
        if (!resource_system_load(s->stage_configs[i].filename, RESOURCE_TYPE_TEXT, 0, &text_resource)) {
            KERROR("Unable to read shader file: %s.", s->stage_configs[i].filename);
            return false;
        }
        // Take a copy of the source and length, then release the resource.
        s->stage_configs[i].source_length = text_resource.data_size;
        s->stage_configs[i].source = string_duplicate(text_resource.data);
        // TODO: Implement recursive #include directives here at this level so it's handled the same
        // regardless of what backend is being used.
        // This should recursively replace #includes with the file content in-place and adjust the source
        // length along the way.

#ifdef _DEBUG
        // Allow shader hot-reloading in debug builds.
        if (!platform_watch_file(text_resource.full_path, &s->module_watch_ids[i])) {
            // If this fails, warn about it but there's no need to crash over it.
            KWARN("Failed to watch shader source file '%s'.", text_resource.full_path);
        }

#endif
        // Release the resource as it isn't needed anymore at this point.
        resource_system_unload(&text_resource);
    }

    return state->backend->shader_create(state->backend, s, config);
}

void renderer_shader_destroy(struct renderer_system_state* state, shader* s) {
#ifdef _DEBUG
    if (s->module_watch_ids) {
        // Unwatch the shader files.
        for (u8 i = 0; i < s->shader_stage_count; ++i) {
            platform_unwatch_file(s->module_watch_ids[i]);
        }

        state->backend->shader_destroy(state->backend, s);
    }
#endif
}

b8 renderer_shader_initialize(struct renderer_system_state* state, shader* s) {
    return state->backend->shader_initialize(state->backend, s);
}

b8 renderer_shader_reload(struct renderer_system_state* state, struct shader* s) {

    // Examine shader stages and load shader source as required. This source is
    // then fed to the backend renderer, which stands up any shader program resources
    // as required.
    // TODO: Implement #include directives here at this level so it's handled the same
    // regardless of what backend is being used.

    // Make a copy of the stage configs in case a file fails to load.
    b8 has_error = false;
    shader_stage_config* new_stage_configs = kallocate(sizeof(shader_stage_config) * s->shader_stage_count, MEMORY_TAG_ARRAY);
    for (u8 i = 0; i < s->shader_stage_count; ++i) {
        // Read the resource.
        resource text_resource;
        if (!resource_system_load(s->stage_configs[i].filename, RESOURCE_TYPE_TEXT, 0, &text_resource)) {
            KERROR("Unable to read shader file: %s.", s->stage_configs[i].filename);
            has_error = true;
            break;
        }

        // Free the old source.
        if (s->stage_configs[i].source) {
            string_free(s->stage_configs[i].source);
        }

        // Take a copy of the source and length, then release the resource.
        new_stage_configs[i].source_length = text_resource.data_size;
        new_stage_configs[i].source = string_duplicate(text_resource.data);
        // TODO: Implement recursive #include directives here at this level so it's handled the same
        // regardless of what backend is being used.
        // This should recursively replace #includes with the file content in-place and adjust the source
        // length along the way.

        // Release the resource as it isn't needed anymore at this point.
        resource_system_unload(&text_resource);
    }

    for (u8 i = 0; i < s->shader_stage_count; ++i) {
        if (has_error) {
            if (new_stage_configs[i].source) {
                string_free(new_stage_configs[i].source);
            }
        } else {
            s->stage_configs[i].source = new_stage_configs[i].source;
            s->stage_configs[i].source_length = new_stage_configs[i].source_length;
        }
    }
    kfree(new_stage_configs, sizeof(shader_stage_config) * s->shader_stage_count, MEMORY_TAG_ARRAY);
    if (has_error) {
        return false;
    }

    return state->backend->shader_reload(state->backend, s);
}

b8 renderer_shader_use(struct renderer_system_state* state, shader* s) {
    return state->backend->shader_use(state->backend, s);
}

b8 renderer_shader_set_wireframe(struct renderer_system_state* state, shader* s, b8 wireframe_enabled) {
    // Ensure that this shader has the ability to go wireframe before changing.
    if (!state->backend->shader_supports_wireframe(state->backend, s)) {
        // Not supported, don't enable. Bleat about it.
        KWARN("Shader does not support wireframe mode: '%s'.", s->name);
        return false;
    }
    s->is_wireframe = wireframe_enabled;
    return true;
}

b8 renderer_shader_apply_globals(struct renderer_system_state* state, shader* s) {
    return state->backend->shader_apply_globals(state->backend, s, state->frame_number);
}

b8 renderer_shader_apply_instance(struct renderer_system_state* state, shader* s) {
    return state->backend->shader_apply_instance(state->backend, s, state->frame_number);
}

b8 renderer_shader_apply_local(struct renderer_system_state* state, shader* s) {
    return state->backend->shader_apply_local(state->backend, s, state->frame_number);
}

b8 renderer_shader_instance_resources_acquire(struct renderer_system_state* state, struct shader* s, const shader_instance_resource_config* config, u32* out_instance_id) {
    return state->backend->shader_instance_resources_acquire(state->backend, s, config, out_instance_id);
}

b8 renderer_shader_instance_resources_release(struct renderer_system_state* state, shader* s, u32 instance_id) {
    return state->backend->shader_instance_resources_release(state->backend, s, instance_id);
}

b8 renderer_shader_local_resources_acquire(struct renderer_system_state* state, struct shader* s, const shader_instance_resource_config* config, u32* out_local_id) {
    return state->backend->shader_local_resources_acquire(state->backend, s, config, out_local_id);
}

b8 renderer_shader_local_resources_release(struct renderer_system_state* state, struct shader* s, u32 local_id) {
    return state->backend->shader_local_resources_release(state->backend, s, local_id);
}

shader_uniform* renderer_shader_uniform_get_by_location(shader* s, u16 location) {
    if (!s) {
        return 0;
    }
    return &s->uniforms[location];
}

shader_uniform* renderer_shader_uniform_get(shader* s, const char* name) {
    if (!s || !name) {
        return 0;
    }

    u16 uniform_index;
    if (!hashtable_get(&s->uniform_lookup, name, &uniform_index)) {
        KERROR("Shader '%s' does not contain a uniform named '%s'.", s->name, name);
        return false;
    }

    return &s->uniforms[uniform_index];
}

b8 renderer_shader_uniform_set(struct renderer_system_state* state, shader* s, shader_uniform* uniform, u32 array_index, const void* value) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->shader_uniform_set(state_ptr->backend, s, uniform, array_index, value);
}

b8 renderer_kresource_texture_map_resources_acquire(struct renderer_system_state* state, struct kresource_texture_map* map) {
    return state->backend->kresource_texture_map_resources_acquire(state->backend, map);
}

void renderer_kresource_texture_map_resources_release(struct renderer_system_state* state, struct kresource_texture_map* map) {
    state->backend->kresource_texture_map_resources_release(state->backend, map);
}

b8 renderer_is_multithreaded(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->is_multithreaded(state_ptr->backend);
}

b8 renderer_flag_enabled_get(renderer_config_flags flag) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->flag_enabled_get(state_ptr->backend, flag);
}

void renderer_flag_enabled_set(renderer_config_flags flag, b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->flag_enabled_set(state_ptr->backend, flag, enabled);
}

b8 renderer_renderbuffer_create(const char* name, renderbuffer_type type, u64 total_size, renderbuffer_track_type track_type, renderbuffer* out_buffer) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (!out_buffer) {
        KERROR("renderer_renderbuffer_create requires a valid pointer to hold the created buffer.");
        return false;
    }

    kzero_memory(out_buffer, sizeof(renderbuffer));

    out_buffer->type = type;
    out_buffer->total_size = total_size;
    if (name) {
        out_buffer->name = string_duplicate(name);
    } else {
        out_buffer->name = string_format("renderbuffer_%s", "unnamed");
    }

    out_buffer->track_type = track_type;

    // Create the freelist, if needed.
    if (track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, 0, 0);
        out_buffer->freelist_block = kallocate(out_buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        freelist_create(total_size, &out_buffer->freelist_memory_requirement, out_buffer->freelist_block, &out_buffer->buffer_freelist);
    } else if (track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
        out_buffer->offset = 0;
    }

    // Create the internal buffer from the backend.
    if (!state_ptr->backend->renderbuffer_internal_create(state_ptr->backend, out_buffer)) {
        KFATAL("Unable to create backing buffer for renderbuffer. Application cannot continue.");
        return false;
    }

    return true;
}

void renderer_renderbuffer_destroy(renderbuffer* buffer) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (buffer) {
        if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
            freelist_destroy(&buffer->buffer_freelist);
            kfree(buffer->freelist_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
            buffer->freelist_memory_requirement = 0;
        } else if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
            buffer->offset = 0;
        }

        if (buffer->name) {
            u32 length = string_length(buffer->name);
            kfree(buffer->name, length + 1, MEMORY_TAG_STRING);
            buffer->name = 0;
        }

        // Free up the backend resources.
        state_ptr->backend->renderbuffer_internal_destroy(state_ptr->backend, buffer);
        buffer->internal_data = 0;
    }
}

b8 renderer_renderbuffer_bind(renderbuffer* buffer, u64 offset) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (!buffer) {
        KERROR("renderer_renderbuffer_bind requires a valid pointer to a buffer.");
        return false;
    }

    return state_ptr->backend->renderbuffer_bind(state_ptr->backend, buffer, offset);
}

b8 renderer_renderbuffer_unbind(renderbuffer* buffer) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_unbind(state_ptr->backend, buffer);
}

void* renderer_renderbuffer_map_memory(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_map_memory(state_ptr->backend, buffer, offset, size);
}

void renderer_renderbuffer_unmap_memory(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->renderbuffer_unmap_memory(state_ptr->backend, buffer, offset, size);
}

b8 renderer_renderbuffer_flush(renderbuffer* buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_flush(state_ptr->backend, buffer, offset, size);
}

b8 renderer_renderbuffer_read(renderbuffer* buffer, u64 offset, u64 size, void** out_memory) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_read(state_ptr->backend, buffer, offset, size, out_memory);
}

b8 renderer_renderbuffer_resize(renderbuffer* buffer, u64 new_total_size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Sanity check.
    if (new_total_size <= buffer->total_size) {
        KERROR("renderer_renderbuffer_resize requires that new size be larger than the old. Not doing this could lead to data loss.");
        return false;
    }

    if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
        // Resize the freelist first, if used.
        u64 new_memory_requirement = 0;
        freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, 0, 0, 0);
        void* new_block = kallocate(new_memory_requirement, MEMORY_TAG_RENDERER);
        void* old_block = 0;
        if (!freelist_resize(&buffer->buffer_freelist, &new_memory_requirement, new_block, new_total_size, &old_block)) {
            KERROR("renderer_renderbuffer_resize failed to resize internal free list.");
            kfree(new_block, new_memory_requirement, MEMORY_TAG_RENDERER);
            return false;
        }

        // Clean up the old memory, then assign the new properties over.
        kfree(old_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        buffer->freelist_memory_requirement = new_memory_requirement;
        buffer->freelist_block = new_block;
    }

    b8 result = state_ptr->backend->renderbuffer_resize(state_ptr->backend, buffer, new_total_size);
    if (result) {
        buffer->total_size = new_total_size;
    } else {
        KERROR("Failed to resize internal renderbuffer resources.");
    }
    return result;
}

b8 renderer_renderbuffer_allocate(renderbuffer* buffer, u64 size, u64* out_offset) {
    if (!buffer || !size || !out_offset) {
        KERROR("renderer_renderbuffer_allocate requires valid buffer, a nonzero size and valid pointer to hold offset.");
        return false;
    }

    if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_NONE) {
        KWARN("renderer_renderbuffer_allocate called on a buffer not using freelists. Offset will not be valid. Call renderer_renderbuffer_load_range instead.");
        *out_offset = 0;
        return true;
    } else if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
        *out_offset = buffer->offset;
        buffer->offset += size;
        return true;
    }

    return freelist_allocate_block(&buffer->buffer_freelist, size, out_offset);
}

b8 renderer_renderbuffer_free(renderbuffer* buffer, u64 size, u64 offset) {
    if (!buffer || !size) {
        KERROR("renderer_renderbuffer_free requires valid buffer and a nonzero size.");
        return false;
    }

    if (buffer->track_type != RENDERBUFFER_TRACK_TYPE_FREELIST) {
        KWARN("renderer_render_buffer_free called on a buffer not using freelists. Nothing was done.");
        return true;
    }
    return freelist_free_block(&buffer->buffer_freelist, size, offset);
}

b8 renderer_renderbuffer_clear(renderbuffer* buffer, b8 zero_memory) {
    if (!buffer) {
        KERROR("renderer_renderbuffer_clear requires valid buffer and a nonzero size.");
        return false;
    }

    if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
        freelist_clear(&buffer->buffer_freelist);
    } else if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
        buffer->offset = 0;
    }

    if (zero_memory) {
        // TODO: zero memory
        KFATAL("TODO: Zero memory");
        return false;
    }

    return true;
}

b8 renderer_renderbuffer_load_range(renderbuffer* buffer, u64 offset, u64 size, const void* data, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_load_range(state_ptr->backend, buffer, offset, size, data, include_in_frame_workload);
}

b8 renderer_renderbuffer_copy_range(renderbuffer* source, u64 source_offset, renderbuffer* dest, u64 dest_offset, u64 size, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_copy_range(state_ptr->backend, source, source_offset, dest, dest_offset, size, include_in_frame_workload);
}

b8 renderer_renderbuffer_draw(renderbuffer* buffer, u64 offset, u32 element_count, b8 bind_only) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_draw(state_ptr->backend, buffer, offset, element_count, bind_only);
}

void renderer_active_viewport_set(viewport* v) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->active_viewport = v;

    // rect_2d viewport_rect = (vec4){v->rect.x, v->rect.height - v->rect.y, v->rect.width, -v->rect.height};
    rect_2d viewport_rect = (vec4){v->rect.x, v->rect.y + v->rect.height, v->rect.width, -v->rect.height};
    state_ptr->backend->viewport_set(state_ptr->backend, viewport_rect);

    rect_2d scissor_rect = (vec4){v->rect.x, v->rect.y, v->rect.width, v->rect.height};
    state_ptr->backend->scissor_set(state_ptr->backend, scissor_rect);
}

viewport* renderer_active_viewport_get(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->active_viewport;
}

void renderer_wait_for_idle(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->wait_for_idle(state_ptr->backend);
}

b8 renderer_pcf_enabled(struct renderer_system_state* state) {
    if (!state) {
        return false;
    }
    return state->use_pcf;
}

static void reapply_dynamic_state(renderer_system_state* state, const renderer_dynamic_state* dynamic_state) {
    renderer_set_depth_test_enabled(dynamic_state->depth_test_enabled);
    renderer_set_depth_write_enabled(dynamic_state->depth_write_enabled);

    renderer_set_stencil_test_enabled(dynamic_state->stencil_test_enabled);
    renderer_set_stencil_reference(dynamic_state->stencil_reference);
    renderer_set_stencil_write_mask(dynamic_state->stencil_write_mask);
    renderer_set_stencil_compare_mask(dynamic_state->stencil_compare_mask);
    renderer_set_stencil_op(
        dynamic_state->fail_op,
        dynamic_state->pass_op,
        dynamic_state->depth_fail_op,
        dynamic_state->compare_op);
    renderer_winding_set(dynamic_state->winding);
    renderer_viewport_set(dynamic_state->viewport);
    renderer_scissor_set(dynamic_state->scissor);
}
