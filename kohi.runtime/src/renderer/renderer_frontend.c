#include "renderer_frontend.h"

#include "containers/darray.h"
#include "containers/freelist.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/frame_data.h"
#include "core/kvar.h"
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/geometry.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "platform/platform.h"
#include "renderer/renderer_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/plugin_system.h"
#include "systems/texture_system.h"

/**
 * @brief Represents a queued renderbuffer deletion.
 */
typedef struct renderbuffer_queued_deletion {
    /** @brief The number of frames remaining until the deletion occurs. */
    u8 frames_until_delete;
    /** @brief The range to be deleted. Considered a "free" slot if range's values are 0. */
    krange range;
} renderbuffer_queued_deletion;

typedef struct krenderbuffer_data {
    /** @brief The name of the buffer. */
    kname name;
    /** @brief The type of buffer, which typically determines its use. */
    renderbuffer_type type;
    /** @brief The total size of the buffer in bytes. */
    u64 total_size;
    /** @brief indicates the allocation tracking type. */
    renderbuffer_track_type track_type;
    /** @brief The amount of memory required to store the freelist. 0 if not used. */
    u64 freelist_memory_requirement;
    /** @brief The buffer freelist, if used. */
    freelist buffer_freelist;
    /** @brief The freelist memory block, if needed. */
    void* freelist_block;
    /** @brief Contains internal data for the renderer-API-specific buffer. */
    void* internal_data;
    /** @brief The byte offset used for linear tracking. */
    u64 offset;

    /**
     * @brief Queue of ranges to be deleted in this buffer. This is to ensure that the
     * data isn't being used before being marked as free and potentially overwritten.
     * Used for all buffer types, including those without tracking. Zeroed out if the
     * queue is cleared.
     */
    renderbuffer_queued_deletion* delete_queue;
} krenderbuffer_data;

typedef struct renderer_system_state {
    /** @brief The current frame number. Rolls over about every 18 minutes at 60FPS. */
    u16 frame_number;
    /** @brief The viewport information for the given window. */
    struct viewport* active_viewport;

    /** @brief The actual loaded plugin obtained from the plugin system. */
    kruntime_plugin* backend_plugin;
    /** @brief The interface to the backend plugin. This is a cold-cast from backend_plugin->plugin_state. */
    renderer_backend_interface* backend;

    /** @brief The number of render targets. Typically lines up with the amount of swapchain images.
     *  NOTE: Standardizing the rule here that all windows should have the same number here.
     */
    u8 render_target_count;

    /** @brief The object vertex buffer, used to hold geometry vertices. */
    krenderbuffer geometry_vertex_buffer;
    /** @brief The object index buffer, used to hold geometry indices. */
    krenderbuffer geometry_index_buffer;
    /** @brief The global material storage buffer, used to hold global data needed in many places (i.e lights, transforms, materials, skinning data, etc.). */
    krenderbuffer global_material_storage_buffer;

    // Darray of created renderbuffers.
    krenderbuffer_data* renderbuffers;

    // Renderer options.

    /** @brief Use PCF filtering */
    b8 use_pcf;

    /** @brief Generic samplers. */
    ksampler_backend generic_samplers[SHADER_GENERIC_SAMPLER_COUNT];

    /** @brief Default textures. Registered from the texture system. */
    ktexture_backend default_textures[RENDERER_DEFAULT_TEXTURE_COUNT];
} renderer_system_state;

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

    if (!kson_object_property_value_get_bool(&tree.root, "triple_buffering_enabled", &out_config->triple_buffering_enabled)) {
        // Default to false if not defined.
        out_config->triple_buffering_enabled = false;
    }

    if (!kson_object_property_value_get_bool(&tree.root, "require_discrete_gpu", &out_config->require_discrete_gpu)) {
        // Default to true if not defined.
        out_config->require_discrete_gpu = false;
    }

    i64 max_shader_count = 0;
    if (!kson_object_property_value_get_int(&tree.root, "max_shader_count", &max_shader_count)) {
        max_shader_count = 1024;
    }

    out_config->max_shader_count = max_shader_count;

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
    renderer_config.use_triple_buffering = config->triple_buffering_enabled;
    renderer_config.max_shader_count = config->max_shader_count;
    renderer_config.require_discrete_gpu = config->require_discrete_gpu;
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

    // Create "generic" samplers for reuse WITH anisotropy.
    // NOTE: This should probably be configurable instead of just maxing out anisotropy
    f32 max_aniotropy = renderer_max_anisotropy_get();
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_REPEAT] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_REPEAT"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_REPEAT, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_MIRRORED_REPEAT, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_CLAMP] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_CLAMP"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_CLAMP_TO_EDGE, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_CLAMP_TO_BORDER, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_REPEAT] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_REPEAT"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_REPEAT, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_MIRRORED_REPEAT, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_CLAMP] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_CLAMP"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_CLAMP_TO_EDGE, max_aniotropy);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_CLAMP_TO_BORDER, max_aniotropy);

    // Same as above, but variants WITHOUT anisotropy. Used for sampling depth textures, for example.
    // This is required since AMD cards tend to not like anisotropy when sampling depth textures.
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_REPEAT, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_REPEAT_MIRRORED_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_MIRRORED_REPEAT, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_CLAMP_TO_EDGE, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_LINEAR_CLAMP_BORDER_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_LINEAR, TEXTURE_REPEAT_CLAMP_TO_BORDER, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_REPEAT, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_REPEAT_MIRRORED_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_MIRRORED_REPEAT, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_CLAMP_TO_EDGE, 0);
    state->generic_samplers[SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER_NO_ANISOTROPY] = renderer_sampler_acquire(state, kname_create("SHADER_GENERIC_SAMPLER_NEAREST_CLAMP_BORDER_NO_ANISOTROPY"), TEXTURE_FILTER_MODE_NEAREST, TEXTURE_REPEAT_CLAMP_TO_BORDER, 0);

    // Invalidate default texture handles, the should be registered from the texture system via renderer_default_texture_register().
    for (u32 i = 0; i < RENDERER_DEFAULT_TEXTURE_COUNT; ++i) {
        state->default_textures[i] = KTEXTURE_BACKEND_INVALID;
    }

    // Renderbuffer setup.
    state->renderbuffers = darray_create(krenderbuffer_data);

    // Geometry vertex buffer
    // TODO: make this configurable.
    const u64 vertex_buffer_size = sizeof(vertex_3d) * 20 * 1024 * 1024;
    state->geometry_vertex_buffer = renderer_renderbuffer_create(kname_create(KRENDERBUFFER_NAME_GLOBAL_VERTEX), RENDERBUFFER_TYPE_VERTEX, vertex_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST);
    if (state->geometry_vertex_buffer == KRENDERBUFFER_INVALID) {
        KERROR("Error creating vertex buffer.");
        return false;
    }

    // Geometry index buffer
    // TODO: Make this configurable.
    const u64 index_buffer_size = sizeof(u32) * 100 * 1024 * 1024;
    state->geometry_index_buffer = renderer_renderbuffer_create(kname_create(KRENDERBUFFER_NAME_GLOBAL_INDEX), RENDERBUFFER_TYPE_INDEX, index_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST);
    if (state->geometry_index_buffer == KRENDERBUFFER_INVALID) {
        KERROR("Error creating index buffer.");
        return false;
    }

    // Global material storage buffer
    // TODO: Make this configurable.
    const u64 storage_buffer_size = MEBIBYTES(256);
    state->global_material_storage_buffer = renderer_renderbuffer_create(kname_create(KRENDERBUFFER_NAME_GLOBAL_MATERIALS), RENDERBUFFER_TYPE_STORAGE, storage_buffer_size, RENDERBUFFER_TRACK_TYPE_FREELIST);
    if (state->global_material_storage_buffer == KRENDERBUFFER_INVALID) {
        KERROR("Error creating global storage buffer.");
        return false;
    }
    KDEBUG("Created global storage buffer.");

    return true;
}

void renderer_system_shutdown(renderer_system_state* state) {
    if (state) {
        renderer_system_state* typed_state = (renderer_system_state*)state;

        // renderer_wait_for_idle();

        // Destroy buffers.
        renderer_renderbuffer_destroy(typed_state->geometry_vertex_buffer);
        renderer_renderbuffer_destroy(typed_state->geometry_index_buffer);
        renderer_renderbuffer_destroy(typed_state->global_material_storage_buffer);

        // Destroy generic samplers.
        for (u32 i = 0; i < SHADER_GENERIC_SAMPLER_COUNT; ++i) {
            renderer_sampler_release(state, &state->generic_samplers[i]);
        }

        // Shutdown the plugin
        typed_state->backend->shutdown(typed_state->backend);
    }
}

u16 renderer_system_frame_number_get(struct renderer_system_state* state) {
    if (!state) {
        return INVALID_ID_U16;
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

    // Create backend resources (i.e swapchain, surface, images, etc.).
    if (!state->backend->window_create(state->backend, window)) {
        KERROR("Renderer backend failed to create resources for new window. See logs for details.");
        return false;
    }

    // Request writeable images that are the size of the window. These are used as render targets and
    // are later blitted to swapchain images.
    {
        ktexture_load_options options = {
            .type = KTEXTURE_TYPE_2D,
            .is_writeable = true,
            .format = KPIXEL_FORMAT_RGBA8,
            .width = window->width,
            .height = window->height,
            .multiframe_buffering = true,
            .name = kname_create("__window_colourbuffer_texture__")};
        window->renderer_state->colourbuffer = texture_acquire_with_options_sync(options);
    }
    {
        ktexture_load_options options = {
            .type = KTEXTURE_TYPE_2D,
            .is_depth = true,
            .is_stencil = true,
            .is_writeable = true,
            .format = KPIXEL_FORMAT_RGBA8,
            .width = window->width,
            .height = window->height,
            .multiframe_buffering = true,
            .name = kname_create("__window_depthbuffer_texture__")};
        window->renderer_state->depthbuffer = texture_acquire_with_options_sync(options);
    }

    return true;
}

void renderer_on_window_destroyed(struct renderer_system_state* state, struct kwindow* window) {
    if (window) {

        // Destroy on backend first.
        state->backend->window_destroy(state->backend, window);

        // Release colour/depth buffers.
        texture_release(window->renderer_state->colourbuffer);
        texture_release(window->renderer_state->depthbuffer);
    }
}

void renderer_on_window_resized(struct renderer_system_state* state, const struct kwindow* window) {
    state->backend->window_resized(state->backend, window);

    b8 texture_system_initialized = engine_systems_get()->texture_system != 0;

    // Also recreate colour/depth buffers.
    if (window->renderer_state->colourbuffer) {
        if (!texture_resize(window->renderer_state->colourbuffer, window->width, window->height, texture_system_initialized)) {
            KERROR("Failed to resize window colour buffer texture on window resize.");
            return;
        }
    }

    if (window->renderer_state->depthbuffer) {
        if (!texture_resize(window->renderer_state->depthbuffer, window->width, window->height, texture_system_initialized)) {
            KERROR("Failed to resize window depth buffer texture on window resize.");
            return;
        }
    }
}

void renderer_begin_debug_label(const char* label_text, vec3 colour) {
#if KOHI_DEBUG
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (state_ptr) {
        state_ptr->backend->begin_debug_label(state_ptr->backend, label_text, colour);
    }
#endif
}

void renderer_end_debug_label(void) {
#if KOHI_DEBUG
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
    // Before the frame starts, check registered renderbuffers to see if deletes are needed.
    u32 registered_renderbuffer_count = darray_length(state->renderbuffers);
    for (u32 i = 0; i < registered_renderbuffer_count; ++i) {
        krenderbuffer_data* pr = &state->renderbuffers[i];
        if (pr) {
            u32 delete_count = pr->delete_queue ? darray_length(pr->delete_queue) : 0;
            for (u32 d = 0; d < delete_count; ++d) {
                renderbuffer_queued_deletion* q = &pr->delete_queue[d];
                if (q->frames_until_delete > 0) {
                    // If there are wait frames, decrement it and check again next frame.
                    q->frames_until_delete--;
                } else {
                    // If the frame wait count is 0, then it may be up for deletion or may be empty,
                    // depending on the range. Only ranges with a size are considered.
                    if (q->range.size > 0) {
                        // If there is a size, then this needs deletion. If there isn't, it's already deleted.
                        switch (pr->track_type) {
                        case RENDERBUFFER_TRACK_TYPE_FREELIST: {
                            if (!freelist_free_block(&pr->buffer_freelist, q->range.size, q->range.offset)) {
                                // If this fails, something may be wrong. Throw an error and skip for now.
                                // If the issue persists, it's likely something went terribly wrong.
                                KERROR("Failed to free from renderbuffer freelist. See logs for details.");

                                // Put the frame count back first.
                                q->frames_until_delete++;
                                continue;
                            }
                        } break;
                        case RENDERBUFFER_TRACK_TYPE_NONE:
                        case RENDERBUFFER_TRACK_TYPE_LINEAR:
                            // NOTE: nothing to do for these types, but placing them here anyways so if this changes,
                            // all the other bits of logic around this don't also have to change.
                            break;
                        }

                        // Reset the entry. This makes it a "free" slot which can be taken in a future frame.
                        q->range.size = 0;
                        q->range.offset = 0;
                    }
                }
            }
        }
    }

    // Now actually begin the command list in the renderer backend.
    b8 result = state->backend->frame_commands_begin(state->backend, p_frame_data);

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

void renderer_viewport_set(rect_2di rect) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->viewport_set(state_ptr->backend, rect);
}

void renderer_viewport_reset(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->viewport_reset(state_ptr->backend);
}

void renderer_scissor_set(rect_2di rect) {
    if (rect.width == 0 || rect.height == 0) {
        KERROR("%s: width/height should not be zero");
    }
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->scissor_set(state_ptr->backend, rect);
}

void renderer_scissor_reset(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->scissor_reset(state_ptr->backend);
}

void renderer_winding_set(renderer_winding winding) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->winding_set(state_ptr->backend, winding);
}

void renderer_cull_mode_set(renderer_cull_mode cull_mode) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->cull_mode_set(state_ptr->backend, cull_mode);
}

void renderer_set_stencil_test_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_stencil_test_enabled(state_ptr->backend, enabled);
}

void renderer_set_stencil_reference(u32 reference) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_stencil_reference(state_ptr->backend, reference);
}

void renderer_set_depth_test_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_depth_test_enabled(state_ptr->backend, enabled);
}

void renderer_set_depth_write_enabled(b8 enabled) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Cache dynamic state.
    state_ptr->backend->set_depth_write_enabled(state_ptr->backend, enabled);
}

void renderer_set_stencil_op(renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Cache dynamic state.
    state_ptr->backend->set_stencil_op(state_ptr->backend, fail_op, pass_op, depth_fail_op, compare_op);
}

void renderer_begin_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data, rect_2di render_area, u32 colour_target_count, ktexture_backend* colour_targets, ktexture_backend depth_stencil_target, u32 depth_stencil_layer) {
    KASSERT_MSG(render_area.width != 0 && render_area.height != 0, "renderer_begin_rendering must have a width and height.");

// Verify handles in debug builds, but not release.
#ifdef KOHI_DEBUG
    // If colour targets are used, none should be invalid.
    if (colour_target_count) {
        for (u32 i = 0; i < colour_target_count; ++i) {
            if (colour_targets[i] == KTEXTURE_BACKEND_INVALID) {
                KFATAL("Passed invalid handle to texture target (index=%u) when beginning rendering. Null is used, and will likely cause a failure.", i);
            }
        }
    }
#endif

    state->backend->begin_rendering(state->backend, p_frame_data, render_area, colour_target_count, colour_targets, depth_stencil_target, depth_stencil_layer);
}

void renderer_end_rendering(struct renderer_system_state* state, struct frame_data* p_frame_data) {
    state->backend->end_rendering(state->backend, p_frame_data);
}

void renderer_set_stencil_compare_mask(u32 compare_mask) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_stencil_compare_mask(state_ptr->backend, compare_mask);
}

void renderer_set_stencil_write_mask(u32 write_mask) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->set_stencil_write_mask(state_ptr->backend, write_mask);
}

b8 renderer_texture_resources_acquire(struct renderer_system_state* state, kname name, ktexture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, ktexture_flag_bits flags, ktexture_backend* out_renderer_texture_handle) {
    if (!state) {
        return false;
    }

    if (!out_renderer_texture_handle) {
        KERROR("%s - requires a valid pointer to a handle.", __FUNCTION__);
        return false;
    }

    if (!width || !height) {
        KERROR("Unable to acquire renderer resources for a texture with invalid dimensions; width (%u) and height (%u) must both be nonzero.", width, height);
        return false;
    }

    *out_renderer_texture_handle = KTEXTURE_BACKEND_INVALID;

    if (!state->backend->texture_resources_acquire(state->backend, kname_string_get(name), type, width, height, channel_count, mip_levels, array_size, flags, out_renderer_texture_handle)) {
        KERROR("Failed to acquire texture resources. See logs for details.");
        return false;
    }
    return true;
}

void renderer_texture_resources_release(struct renderer_system_state* state, ktexture_backend* renderer_texture_handle) {
    if (state && *renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->backend->texture_resources_release(state->backend, renderer_texture_handle);
    }
}

b8 renderer_texture_write_data(struct renderer_system_state* state, ktexture_backend renderer_texture_handle, u32 offset, u32 size, const u8* pixels) {
    if (state && renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        b8 include_in_frame_workload = (state->frame_number > 0); // FIXME: Perhaps it's time to move this to its own queue.
        b8 result = state->backend->texture_write_data(state->backend, renderer_texture_handle, offset, size, pixels, include_in_frame_workload);
        if (!include_in_frame_workload) {
            // TODO: update generation?
        }
        return result;
    }
    return false;
}

b8 renderer_texture_read_data(struct renderer_system_state* state, ktexture_backend renderer_texture_handle, u32 offset, u32 size, u8** out_pixels) {
    if (state && renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        return state->backend->texture_read_data(state->backend, renderer_texture_handle, offset, size, out_pixels);
    }
    return false;
}

b8 renderer_texture_read_pixel(struct renderer_system_state* state, ktexture_backend renderer_texture_handle, u32 x, u32 y, u8** out_rgba) {
    if (state && renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        return state->backend->texture_read_pixel(state->backend, renderer_texture_handle, x, y, out_rgba);
    }
    return false;
}

void renderer_default_texture_register(struct renderer_system_state* state, renderer_default_texture default_texture, ktexture_backend renderer_texture_handle) {
    if (state && renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->default_textures[default_texture] = renderer_texture_handle;
    }
}

ktexture_backend renderer_default_texture_get(struct renderer_system_state* state, renderer_default_texture default_texture) {
    if (state) {
        return state->default_textures[default_texture];
    }

    return KTEXTURE_BACKEND_INVALID;
}

b8 renderer_texture_resize(struct renderer_system_state* state, ktexture_backend renderer_texture_handle, u32 new_width, u32 new_height) {
    if (state && renderer_texture_handle != KTEXTURE_BACKEND_INVALID) {
        return state->backend->texture_resize(state->backend, renderer_texture_handle, new_width, new_height);
    }
    return false;
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
        if (!renderer_renderbuffer_allocate(state_ptr->geometry_vertex_buffer, vertex_size, &g->vertex_buffer_offset)) {
            KERROR("vulkan_renderer_geometry_upload failed to allocate from the vertex buffer!");
            return false;
        }
    }

    // Load the data.
    // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
    if (!renderer_renderbuffer_load_range(state_ptr->geometry_vertex_buffer, g->vertex_buffer_offset + vertex_offset, vertex_size, g->vertices + vertex_offset, false)) {
        KERROR("vulkan_renderer_geometry_upload failed to upload to the vertex buffer!");
        return false;
    }

    // Index data, if applicable
    if (index_size) {
        if (!is_reupload) {
            // Allocate space in the buffer.
            if (!renderer_renderbuffer_allocate(state_ptr->geometry_index_buffer, index_size, &g->index_buffer_offset)) {
                KERROR("vulkan_renderer_geometry_upload failed to allocate from the index buffer!");
                return false;
            }
        }

        // Load the data.
        // TODO: Passing false here produces a queue wait and should be offloaded to another queue.
        if (!renderer_renderbuffer_load_range(state_ptr->geometry_index_buffer, g->index_buffer_offset + index_offset, index_size, g->indices + index_offset, false)) {
            KERROR("vulkan_renderer_geometry_upload failed to upload to the index buffer!");
            return false;
        }
    }

    g->generation++;

    return true;
}

void renderer_geometry_vertex_update(kgeometry* g, u32 offset, u32 vertex_count, void* vertices, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    // Load the data.
    u32 size = g->vertex_element_size * vertex_count;
    if (!renderer_renderbuffer_load_range(state_ptr->geometry_vertex_buffer, g->vertex_buffer_offset + offset, size, vertices + offset, include_in_frame_workload)) {
        KERROR("vulkan_renderer_geometry_vertex_update failed to upload to the vertex buffer!");
    }
}

void renderer_geometry_destroy(kgeometry* g) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;

    if (g->generation != INVALID_ID_U16) {
        // Free vertex data
        u64 vertex_data_size = g->vertex_element_size * g->vertex_count;
        if (vertex_data_size) {
            if (!renderer_renderbuffer_free(state_ptr->geometry_vertex_buffer, vertex_data_size, g->vertex_buffer_offset)) {
                KERROR("vulkan_renderer_destroy_geometry failed to free vertex buffer range.");
            }
        }

        // Free index data, if applicable
        u64 index_data_size = g->index_element_size * g->index_count;
        if (index_data_size) {
            if (!renderer_renderbuffer_free(state_ptr->geometry_index_buffer, index_data_size, g->index_buffer_offset)) {
                KERROR("vulkan_renderer_destroy_geometry failed to free index buffer range.");
            }
        }

        // Setting this to invalidid effectively marks the geometry as "not setup".
        g->generation = INVALID_ID_U16;
        g->vertex_buffer_offset = INVALID_ID_U64;
        g->index_buffer_offset = INVALID_ID_U64;
    }
}

void renderer_geometry_draw(geometry_render_data* data) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    b8 includes_index_data = data->index_count > 0;
    if (!renderer_renderbuffer_draw(state_ptr->geometry_vertex_buffer, data->vertex_buffer_offset, data->vertex_count, includes_index_data)) {
        KERROR("vulkan_renderer_draw_geometry failed to draw vertex buffer;");
        return;
    }

    if (includes_index_data) {
        if (!renderer_renderbuffer_draw(state_ptr->geometry_index_buffer, data->index_buffer_offset, data->index_count, !includes_index_data)) {
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

b8 renderer_clear_colour(struct renderer_system_state* state, ktexture_backend texture_handle) {
    if (state && texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->backend->clear_colour(state->backend, texture_handle);
        return true;
    }

    KERROR("renderer_clear_colour_texture requires a valid handle to a texture. Nothing was done.");
    return false;
}

b8 renderer_clear_depth_stencil(struct renderer_system_state* state, ktexture_backend texture_handle) {
    if (state && texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->backend->clear_depth_stencil(state->backend, texture_handle);
        return true;
    }

    KERROR("renderer_clear_depth_stencil requires a valid handle to a texture. Nothing was done.");
    return false;
}

void renderer_colour_texture_prepare_for_present(struct renderer_system_state* state, ktexture_backend texture_handle) {
    if (state && texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->backend->colour_texture_prepare_for_present(state->backend, texture_handle);
        return;
    }

    KERROR("renderer_colour_texture_prepare_for_present requires a valid handle to a texture. Nothing was done.");
}

void renderer_texture_prepare_for_sampling(struct renderer_system_state* state, ktexture_backend texture_handle, ktexture_flag_bits flags) {
    if (state && texture_handle != KTEXTURE_BACKEND_INVALID) {
        state->backend->texture_prepare_for_sampling(state->backend, texture_handle, flags);
        return;
    }

    KERROR("renderer_texture_prepare_for_sampling requires a valid handle to a texture. Nothing was done.");
}

b8 renderer_shader_create(struct renderer_system_state* state, kshader shader, kname name, shader_flags flags, u32 topology_types, face_cull_mode cull_mode, u32 stage_count, shader_stage* stages, kname* stage_names, const char** stage_sources, u32 max_groups, u32 max_draw_ids, u32 attribute_count, const shader_attribute* attributes, u32 uniform_count, const shader_uniform* d_uniforms) {
    return state->backend->shader_create(state->backend, shader, name, flags, topology_types, cull_mode, stage_count, stages, stage_names, stage_sources, max_groups, max_draw_ids, attribute_count, attributes, uniform_count, d_uniforms);
}

void renderer_shader_destroy(struct renderer_system_state* state, kshader shader) {
    state->backend->shader_destroy(state->backend, shader);
}

b8 renderer_shader_reload(struct renderer_system_state* state, kshader shader, u32 stage_count, shader_stage* stages, kname* names, const char** sources) {
    return state->backend->shader_reload(state->backend, shader, stage_count, stages, names, sources);
}

b8 renderer_shader_use(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_use(state->backend, shader);
}

b8 renderer_shader_supports_wireframe(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_supports_wireframe(state->backend, shader);
}

b8 renderer_shader_flag_get(struct renderer_system_state* state, kshader shader, shader_flags flag) {
    return state->backend->shader_flag_get(state->backend, shader, flag);
}

void renderer_shader_flag_set(struct renderer_system_state* state, kshader shader, shader_flags flag, b8 enabled) {
    state->backend->shader_flag_set(state->backend, shader, flag, enabled);
}

b8 renderer_shader_bind_per_frame(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_bind_per_frame(state->backend, shader);
}

b8 renderer_shader_bind_per_group(struct renderer_system_state* state, kshader shader, u32 group_id) {
    return state->backend->shader_bind_per_group(state->backend, shader, group_id);
}

b8 renderer_shader_bind_per_draw(struct renderer_system_state* state, kshader shader, u32 draw_id) {
    return state->backend->shader_bind_per_draw(state->backend, shader, draw_id);
}

b8 renderer_shader_apply_per_frame(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_apply_per_frame(state->backend, shader, state->frame_number);
}

b8 renderer_shader_apply_per_group(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_apply_per_group(state->backend, shader, state->frame_number);
}

b8 renderer_shader_apply_per_draw(struct renderer_system_state* state, kshader shader) {
    return state->backend->shader_apply_per_draw(state->backend, shader, state->frame_number);
}

b8 renderer_shader_per_group_resources_acquire(struct renderer_system_state* state, kshader shader, u32* out_group_id) {
    return state->backend->shader_per_group_resources_acquire(state->backend, shader, out_group_id);
}

b8 renderer_shader_per_group_resources_release(struct renderer_system_state* state, kshader shader, u32 group_id) {
    return state->backend->shader_per_group_resources_release(state->backend, shader, group_id);
}

b8 renderer_shader_per_draw_resources_acquire(struct renderer_system_state* state, kshader shader, u32* out_draw_id) {
    return state->backend->shader_per_draw_resources_acquire(state->backend, shader, out_draw_id);
}

b8 renderer_shader_per_draw_resources_release(struct renderer_system_state* state, kshader shader, u32 draw_id) {
    return state->backend->shader_per_draw_resources_release(state->backend, shader, draw_id);
}

b8 renderer_shader_uniform_set(struct renderer_system_state* state, kshader shader, shader_uniform* uniform, u32 array_index, const void* value) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->shader_uniform_set(state_ptr->backend, shader, uniform, array_index, value);
}

ksampler_backend renderer_generic_sampler_get(struct renderer_system_state* state, shader_generic_sampler sampler) {
    if (!state || sampler == SHADER_GENERIC_SAMPLER_COUNT) {
        KERROR("No state or invalid sampler passed, ya dingus!");
        return KSAMPLER_BACKEND_INVALID;
    }
    return state->generic_samplers[sampler];
}

ksampler_backend renderer_sampler_acquire(struct renderer_system_state* state, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy) {
    return state->backend->sampler_acquire(state->backend, name, filter, repeat, anisotropy);
}

void renderer_sampler_release(struct renderer_system_state* state, ksampler_backend* sampler) {
    state->backend->sampler_release(state->backend, sampler);
}

b8 renderer_sampler_refresh(struct renderer_system_state* state, ksampler_backend* sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels) {
    return state->backend->sampler_refresh(state->backend, sampler, filter, repeat, anisotropy, mip_levels);
}

kname renderer_sampler_name_get(struct renderer_system_state* state, ksampler_backend sampler) {
    return state->backend->sampler_name_get(state->backend, sampler);
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

f32 renderer_max_anisotropy_get(void) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->max_anisotropy_get(state_ptr->backend);
}

krenderbuffer renderer_renderbuffer_create(kname name, renderbuffer_type type, u64 total_size, renderbuffer_track_type track_type) {
    renderer_system_state* state = engine_systems_get()->renderer_system;

    // Look for a free slot or create a new one.
    krenderbuffer out_handle = KRENDERBUFFER_INVALID;
    krenderbuffer_data* out_buffer = 0;
    u16 len = state->renderbuffers ? darray_length(state->renderbuffers) : 0;
    for (u16 i = 0; i < len; ++i) {
        if (state->renderbuffers[i].type == RENDERBUFFER_TYPE_UNKNOWN) {
            out_handle = i;
            break;
        }
    }
    if (out_handle == KRENDERBUFFER_INVALID) {
        darray_push(state->renderbuffers, (krenderbuffer_data){0});
        out_handle = len;
    }
    out_buffer = &state->renderbuffers[out_handle];

    kzero_memory(out_buffer, sizeof(krenderbuffer));

    out_buffer->type = type;
    out_buffer->total_size = total_size;
    out_buffer->name = name;
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
    if (!state->backend->renderbuffer_internal_create(state->backend, name, total_size, type, out_handle)) {
        KFATAL("Unable to create backing buffer for renderbuffer. Application cannot continue.");
        return false;
    }

    // Setup the deletion queue.
    out_buffer->delete_queue = darray_reserve(renderbuffer_queued_deletion, 20);

    return out_handle;
}

void renderer_renderbuffer_destroy(krenderbuffer handle) {
    renderer_system_state* state = engine_systems_get()->renderer_system;
    if (handle == KRENDERBUFFER_INVALID) {
        KERROR("%s - Called with invalid handle.", __FUNCTION__);
        return;
    }

    krenderbuffer_data* buffer = &state->renderbuffers[handle];
    KTRACE("Unregistering renderbuffer '%s'.", kname_string_get(buffer->name));
    // Just setting the array entry's type to unknown removes it from registration.
    buffer->type = RENDERBUFFER_TYPE_UNKNOWN;

    if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
        freelist_destroy(&buffer->buffer_freelist);
        kfree(buffer->freelist_block, buffer->freelist_memory_requirement, MEMORY_TAG_RENDERER);
        buffer->freelist_memory_requirement = 0;
    } else if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
        buffer->offset = 0;
    }

    buffer->name = 0;

    // Cleanup the deletion queue.
    darray_destroy(buffer->delete_queue);
    buffer->delete_queue = 0;

    // Free up the backend resources.
    state->backend->renderbuffer_internal_destroy(state->backend, handle);
}

b8 renderer_renderbuffer_bind(krenderbuffer buffer, u64 offset) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    if (buffer == KRENDERBUFFER_INVALID) {
        KERROR("renderer_renderbuffer_bind requires a valid buffer.");
        return false;
    }

    return state_ptr->backend->renderbuffer_bind(state_ptr->backend, buffer, offset);
}

b8 renderer_renderbuffer_unbind(krenderbuffer buffer) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_unbind(state_ptr->backend, buffer);
}

void* renderer_renderbuffer_map_memory(krenderbuffer buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_map_memory(state_ptr->backend, buffer, offset, size);
}

void renderer_renderbuffer_unmap_memory(krenderbuffer buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    state_ptr->backend->renderbuffer_unmap_memory(state_ptr->backend, buffer, offset, size);
}

b8 renderer_renderbuffer_flush(krenderbuffer buffer, u64 offset, u64 size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_flush(state_ptr->backend, buffer, offset, size);
}

b8 renderer_renderbuffer_read(krenderbuffer buffer, u64 offset, u64 size, void** out_memory) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_read(state_ptr->backend, buffer, offset, size, out_memory);
}

b8 renderer_renderbuffer_resize(krenderbuffer handle, u64 new_total_size) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    krenderbuffer_data* buffer = &state_ptr->renderbuffers[handle];

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

    b8 result = state_ptr->backend->renderbuffer_resize(state_ptr->backend, handle, new_total_size);
    if (result) {
        buffer->total_size = new_total_size;
    } else {
        KERROR("Failed to resize internal renderbuffer resources.");
    }
    return result;
}

b8 renderer_renderbuffer_allocate(krenderbuffer handle, u64 size, u64* out_offset) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    krenderbuffer_data* buffer = &state_ptr->renderbuffers[handle];
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

b8 renderer_renderbuffer_free(krenderbuffer handle, u64 size, u64 offset) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    krenderbuffer_data* buffer = &state_ptr->renderbuffers[handle];
    if (!buffer || !size) {
        KERROR("renderer_renderbuffer_free requires valid buffer and a nonzero size.");
        return false;
    }

    // Validate the deletion before doing anything.
    if (size < 1) {
        KERROR("renderer_renderbuffer_free(): Free failed because size was provided as 0 - nothing will be done. (size=%llu, offset=%llu, renderbuffer_total_size=%llu)", size, offset, buffer->total_size);
        return false;
    }

    if (offset >= buffer->total_size || size > buffer->total_size || (offset + size) > buffer->total_size) {
        KERROR("renderer_renderbuffer_free(): Free failed because size or offset is out of range - nothing will be done. (size=%llu, offset=%llu, renderbuffer_total_size=%llu)", size, offset, buffer->total_size);
        return false;
    }

    // NOTE: Don't actually perform the free, register it for deletion on a later frame.

    // Start by searching for a free slot.
    u32 delete_count = darray_length(buffer->delete_queue);
    for (u32 i = 0; i < delete_count; ++i) {
        renderbuffer_queued_deletion* deletion = &buffer->delete_queue[i];
        if (!deletion->range.size) {
            // Found one, use it.
            deletion->frames_until_delete = RENDERER_MAX_FRAME_COUNT;
            deletion->range.offset = offset;
            deletion->range.size = size;
            return true;
        }
    }

    // If one wasn't found, create and push a new entry.
    renderbuffer_queued_deletion new_deletion = {0};
    new_deletion.frames_until_delete = RENDERER_MAX_FRAME_COUNT;
    new_deletion.range.offset = offset;
    new_deletion.range.size = size;
    darray_push(buffer->delete_queue, new_deletion);

    return true;
}

b8 renderer_renderbuffer_clear(krenderbuffer handle, b8 zero_memory) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    krenderbuffer_data* buffer = &state_ptr->renderbuffers[handle];
    if (!buffer) {
        KERROR("renderer_renderbuffer_clear requires valid buffer and a nonzero size.");
        return false;
    }

    if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_FREELIST) {
        freelist_clear(&buffer->buffer_freelist);
    } else if (buffer->track_type == RENDERBUFFER_TRACK_TYPE_LINEAR) {
        buffer->offset = 0;
    }

    // Clear the queued deletions.
    darray_clear(buffer->delete_queue);

    if (zero_memory) {
        // TODO: zero memory
        KFATAL("TODO: Zero memory");
        return false;
    }

    return true;
}

b8 renderer_renderbuffer_load_range(krenderbuffer buffer, u64 offset, u64 size, const void* data, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_load_range(state_ptr->backend, buffer, offset, size, data, include_in_frame_workload);
}

b8 renderer_renderbuffer_copy_range(krenderbuffer source, u64 source_offset, krenderbuffer dest, u64 dest_offset, u64 size, b8 include_in_frame_workload) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_copy_range(state_ptr->backend, source, source_offset, dest, dest_offset, size, include_in_frame_workload);
}

b8 renderer_renderbuffer_draw(krenderbuffer buffer, u64 offset, u32 element_count, b8 bind_only) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    return state_ptr->backend->renderbuffer_draw(state_ptr->backend, buffer, offset, element_count, bind_only);
}

krenderbuffer renderer_renderbuffer_get(kname name) {
    renderer_system_state* state_ptr = engine_systems_get()->renderer_system;
    u16 len = darray_length(state_ptr->renderbuffers);
    for (u16 i = 0; i < len; ++i) {
        if (state_ptr->renderbuffers[i].name == name) {
            return (krenderbuffer)i;
        }
    }
    KERROR("Renderbuffer named '%s' not found. Returning KRENDERBUFFER_INVALID.", kname_string_get(name));
    return KRENDERBUFFER_INVALID;
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

u16 renderer_max_bound_texture_count_get(struct renderer_system_state* state) {
    // NOTE: while the backend could allow for more, most "non-bindless" APIs have a limit of 16.
    return 16;
}

u16 renderer_max_bound_sampler_count_get(struct renderer_system_state* state) {
    // NOTE: while the backend could allow for more, most "non-bindless" APIs have a limit of 16.
    return 16;
}
