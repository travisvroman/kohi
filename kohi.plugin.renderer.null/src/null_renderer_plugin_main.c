#include "null_renderer_plugin_main.h"

#include <logger.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <strings/kname.h>

#include "kohi.plugin.renderer.null_version.h"
#include "renderer/renderer_types.h"

// A CPU-side stand-in for a GPU buffer. Memory is allocated on first use, because large
// vertex and index buffers are often never read back and would otherwise waste memory.
typedef struct null_renderbuffer {
	b8 in_use;
	u64 size;
	u8 *data;
} null_renderbuffer;

// Tracks which instances of one shader binding set are taken.
typedef struct null_binding_set {
	u32 max_instance_count;
	b8 *instance_in_use;
} null_binding_set;

typedef struct null_shader {
	b8 in_use;
	shader_flags flags;
	u8 binding_set_count;
	null_binding_set *binding_sets;
} null_shader;

typedef struct null_renderer_state {
	renderer_config_flags flags;

	u16 max_shader_count;
	null_shader *shaders;

	u32 renderbuffer_capacity;
	null_renderbuffer *renderbuffers;

	u32 sampler_count;
	kname *sampler_names;
} null_renderer_state;

static null_renderer_state *get_state (const renderer_backend_interface *backend) {
	return (null_renderer_state *)backend->internal_context;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

static b8 null_initialize (renderer_backend_interface *backend, const renderer_backend_config *config) {
	backend->internal_context_size = sizeof(null_renderer_state);
	backend->internal_context = kallocate(backend->internal_context_size, MEMORY_TAG_RENDERER);

	null_renderer_state *state = get_state(backend);
	state->flags = config->flags;
	state->max_shader_count = config->max_shader_count;
	state->shaders = KALLOC_TYPE_CARRAY(null_shader, state->max_shader_count);

	KINFO("Null renderer backend initialized. Nothing will be drawn.");
	return true;
}

static void free_shader (null_shader *shader) {
	for (u8 i = 0; i < shader->binding_set_count; ++i) {
		if (shader->binding_sets[i].instance_in_use) {
			kfree(shader->binding_sets[i].instance_in_use);
		}
	}
	if (shader->binding_sets) {
		kfree(shader->binding_sets);
	}
	kzero_memory(shader, sizeof(null_shader));
}

static void null_shutdown (renderer_backend_interface *backend) {
	null_renderer_state *state = get_state(backend);
	if (!state) {
		return;
	}

	for (u16 i = 0; i < state->max_shader_count; ++i) {
		if (state->shaders[i].in_use) {
			free_shader(&state->shaders[i]);
		}
	}
	kfree(state->shaders);

	for (u32 i = 0; i < state->renderbuffer_capacity; ++i) {
		if (state->renderbuffers[i].data) {
			kfree(state->renderbuffers[i].data);
		}
	}
	if (state->renderbuffers) {
		kfree(state->renderbuffers);
	}
	if (state->sampler_names) {
		kfree(state->sampler_names);
	}

	kfree(backend->internal_context);
	backend->internal_context = KNULL;
	backend->internal_context_size = 0;
}

// ---------------------------------------------------------------------------
// Windows and frames: nothing to present, so every step simply succeeds.
// ---------------------------------------------------------------------------

static b8 null_window_create (renderer_backend_interface *backend, struct kwindow *window) { return true; }
static void null_window_destroy (renderer_backend_interface *backend, struct kwindow *window) {}
static void null_window_resized (renderer_backend_interface *backend, const struct kwindow *window) {}

static b8 null_frame_prepare (renderer_backend_interface *backend, struct frame_data *p_frame_data) { return true; }
static b8 null_frame_prepare_window_surface (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data) { return true; }
static b8 null_frame_commands_begin (renderer_backend_interface *backend, struct frame_data *p_frame_data) { return true; }
static b8 null_frame_commands_end (renderer_backend_interface *backend, struct frame_data *p_frame_data) { return true; }
static b8 null_frame_submit (renderer_backend_interface *backend, struct frame_data *p_frame_data) { return true; }
static b8 null_frame_present (renderer_backend_interface *backend, struct kwindow *window, struct frame_data *p_frame_data) { return true; }
static void null_wait_for_idle (renderer_backend_interface *backend) {}

static void null_begin_debug_label (renderer_backend_interface *backend, const char *label_text, vec4 colour) {}
static void null_end_debug_label (renderer_backend_interface *backend) {}

// ---------------------------------------------------------------------------
// Pipeline state and draw commands: accepted and ignored.
// ---------------------------------------------------------------------------

static void null_viewport_set (renderer_backend_interface *backend, rect_2di rect) {}
static void null_viewport_reset (renderer_backend_interface *backend) {}
static void null_scissor_set (renderer_backend_interface *backend, rect_2di rect) {}
static void null_scissor_reset (renderer_backend_interface *backend) {}
static void null_winding_set (renderer_backend_interface *backend, renderer_winding winding) {}
static void null_cull_mode_set (renderer_backend_interface *backend, renderer_cull_mode cull_mode) {}
static void null_set_stencil_test_enabled (renderer_backend_interface *backend, b8 enabled) {}
static void null_set_depth_test_enabled (renderer_backend_interface *backend, b8 enabled) {}
static void null_set_depth_write_enabled (renderer_backend_interface *backend, b8 enabled) {}
static void null_set_depth_bias (renderer_backend_interface *backend, f32 constant_factor, f32 clamp, f32 slope_factor) {}
static void null_set_depth_bias_enabled (renderer_backend_interface *backend, b8 enabled) {}
static void null_set_stencil_reference (renderer_backend_interface *backend, u32 reference) {}
static void null_set_stencil_op (renderer_backend_interface *backend, renderer_stencil_op fail_op, renderer_stencil_op pass_op, renderer_stencil_op depth_fail_op, renderer_compare_op compare_op) {}
static void null_set_stencil_compare_mask (renderer_backend_interface *backend, u32 compare_mask) {}
static void null_set_stencil_write_mask (renderer_backend_interface *backend, u32 write_mask) {}

static void null_begin_rendering (renderer_backend_interface *backend, struct frame_data *p_frame_data, rect_2di render_area, u32 colour_target_count, ktexture *colour_targets, ktexture depth_stencil_target, u32 depth_stencil_layer) {}
static void null_end_rendering (renderer_backend_interface *backend, struct frame_data *p_frame_data) {}

static void null_clear_colour_set (renderer_backend_interface *backend, vec4 clear_colour) {}
static void null_clear_depth_set (renderer_backend_interface *backend, f32 depth) {}
static void null_clear_stencil_set (renderer_backend_interface *backend, u32 stencil) {}
static void null_clear_colour (renderer_backend_interface *backend, ktexture t) {}
static void null_clear_depth_stencil (renderer_backend_interface *backend, ktexture t) {}

// ---------------------------------------------------------------------------
// Textures: tracked by the frontend; the null backend stores no pixels.
// ---------------------------------------------------------------------------

static void null_colour_texture_prepare_for_present (renderer_backend_interface *backend, ktexture t) {}
static void null_texture_prepare_for_sampling (renderer_backend_interface *backend, ktexture t, ktexture_flag_bits flags) {}

static b8 null_texture_resources_acquire (renderer_backend_interface *backend, ktexture t, const char *name, ktexture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, ktexture_flag_bits flags) { return true; }
static void null_texture_resources_release (renderer_backend_interface *backend, ktexture t) {}
static b8 null_texture_resize (renderer_backend_interface *backend, ktexture t, u32 new_width, u32 new_height) { return true; }
static b8 null_texture_write_data (renderer_backend_interface *backend, ktexture t, u32 bpp, u32 px_x, u32 px_y, i32 layer, u32 width, u32 height, const u8 *pixels, b8 defer_to_next_frame) { return true; }

static b8 null_texture_read_data (renderer_backend_interface *backend, ktexture t, u32 offset, u32 size, u8 **out_pixels) {
	if (!out_pixels || !*out_pixels) {
		return false;
	}
	kzero_memory(*out_pixels, size);
	return true;
}

static b8 null_texture_read_pixel (renderer_backend_interface *backend, ktexture t, u32 x, u32 y, u8 **out_rgba) {
	if (!out_rgba || !*out_rgba) {
		return false;
	}
	kzero_memory(*out_rgba, 4);
	return true;
}

static b8 null_texture_blit_2d (renderer_backend_interface *backend, ktexture source, ktexture target, vec2 source_offset, vec2 source_size, vec2 target_offset, vec2 target_size) { return true; }

// ---------------------------------------------------------------------------
// Shaders: nothing is compiled, but binding set instances are handed out and
// reclaimed properly so that callers hitting instance limits behave as usual.
// ---------------------------------------------------------------------------

static null_shader *get_shader (renderer_backend_interface *backend, kshader shader) {
	null_renderer_state *state = get_state(backend);
	if (shader >= state->max_shader_count || !state->shaders[shader].in_use) {
		KERROR("Null renderer: invalid shader handle %u.", shader);
		return KNULL;
	}
	return &state->shaders[shader];
}

static null_binding_set *get_binding_set (renderer_backend_interface *backend, kshader shader, u8 binding_set) {
	null_shader *s = get_shader(backend, shader);
	if (!s || binding_set >= s->binding_set_count) {
		return KNULL;
	}
	return &s->binding_sets[binding_set];
}

static b8 null_shader_create (
	renderer_backend_interface *backend,
	kshader shader,
	kname name,
	shader_flags flags,
	primitive_topology_type_bits topology_types,
	primitive_topology_type default_type,
	u8 colour_attachment_count,
	kpixel_format *colour_attachment_formats,
	kpixel_format depth_attachment_format,
	kpixel_format stencil_attachment_format,
	u8 pipeline_count,
	shader_pipeline_config *pipelines,
	u8 binding_set_count,
	const shader_binding_set_config *binding_sets) {

	null_renderer_state *state = get_state(backend);
	if (shader >= state->max_shader_count) {
		KERROR("Null renderer: shader handle %u is out of range (max %u).", shader, state->max_shader_count);
		return false;
	}

	null_shader *s = &state->shaders[shader];
	if (s->in_use) {
		free_shader(s);
	}

	s->in_use = true;
	s->flags = flags;
	s->binding_set_count = binding_set_count;
	if (binding_set_count) {
		s->binding_sets = KALLOC_TYPE_CARRAY(null_binding_set, binding_set_count);
		for (u8 i = 0; i < binding_set_count; ++i) {
			u32 max_instances = binding_sets[i].max_instance_count;
			s->binding_sets[i].max_instance_count = max_instances;
			if (max_instances) {
				s->binding_sets[i].instance_in_use = KALLOC_TYPE_CARRAY(b8, max_instances);
			}
		}
	}

	return true;
}

static void null_shader_destroy (renderer_backend_interface *backend, kshader shader) {
	null_shader *s = get_shader(backend, shader);
	if (s) {
		free_shader(s);
	}
}

static b8 null_shader_reload (renderer_backend_interface *backend, kshader shader, u8 pipeline_count, shader_pipeline_config *pipelines) { return true; }
static b8 null_shader_use (renderer_backend_interface *backend, kshader shader, u8 vertex_layout_index) { return true; }
static b8 null_shader_use_with_topology (renderer_backend_interface *backend, kshader shader, primitive_topology_type type, u8 vertex_layout_index) { return true; }
static b8 null_shader_supports_wireframe (const renderer_backend_interface *backend, kshader shader) { return false; }

static b8 null_shader_flag_get (const renderer_backend_interface *backend, kshader shader, shader_flags flag) {
	null_shader *s = get_shader((renderer_backend_interface *)backend, shader);
	return s ? FLAG_GET(s->flags, flag) : false;
}

static void null_shader_flag_set (renderer_backend_interface *backend, kshader shader, shader_flags flag, b8 enabled) {
	null_shader *s = get_shader(backend, shader);
	if (s) {
		FLAG_SET(s->flags, flag, enabled);
	}
}

static u32 null_shader_acquire_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set) {
	null_binding_set *set = get_binding_set(backend, shader, binding_set);
	if (!set) {
		return INVALID_ID;
	}
	for (u32 i = 0; i < set->max_instance_count; ++i) {
		if (!set->instance_in_use[i]) {
			set->instance_in_use[i] = true;
			return i;
		}
	}
	KERROR("Null renderer: shader %u binding set %u has no free instances (max %u).", shader, binding_set, set->max_instance_count);
	return INVALID_ID;
}

static void null_shader_release_binding_set_instance (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id) {
	null_binding_set *set = get_binding_set(backend, shader, binding_set);
	if (set && instance_id < set->max_instance_count) {
		set->instance_in_use[instance_id] = false;
	}
}

static u32 null_shader_binding_set_get_max_instance_count (renderer_backend_interface *backend, kshader shader, u8 binding_set) {
	null_binding_set *set = get_binding_set(backend, shader, binding_set);
	return set ? set->max_instance_count : 0;
}

static b8 null_shader_set_immediate_data (renderer_backend_interface *backend, kshader shader, const void *data, u8 size) { return true; }
static b8 null_shader_set_binding_data (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u64 offset, void *data, u64 size) { return true; }
static b8 null_shader_set_binding_texture (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ktexture texture) { return true; }
static b8 null_shader_set_binding_sampler (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id, u8 binding_index, u8 array_index, ksampler_backend sampler) { return true; }
static b8 null_shader_apply_binding_set (renderer_backend_interface *backend, kshader shader, u8 binding_set, u32 instance_id) { return true; }

// ---------------------------------------------------------------------------
// Samplers: handles are handed out in order and only their names are kept.
// ---------------------------------------------------------------------------

static ksampler_backend null_sampler_acquire (renderer_backend_interface *backend, kname name, texture_filter filter, texture_repeat repeat, f32 anisotropy) {
	null_renderer_state *state = get_state(backend);
	if (state->sampler_count >= KSAMPLER_BACKEND_INVALID) {
		KERROR("Null renderer: out of sampler handles.");
		return KSAMPLER_BACKEND_INVALID;
	}
	state->sampler_names = KREALLOC_TYPE_CARRAY(state->sampler_names, kname, state->sampler_count + 1);
	state->sampler_names[state->sampler_count] = name;
	return (ksampler_backend)state->sampler_count++;
}

static void null_sampler_release (renderer_backend_interface *backend, ksampler_backend *sampler) {
	if (sampler) {
		*sampler = KSAMPLER_BACKEND_INVALID;
	}
}

static b8 null_sampler_refresh (renderer_backend_interface *backend, ksampler_backend *sampler, texture_filter filter, texture_repeat repeat, f32 anisotropy, u32 mip_levels) { return true; }

static kname null_sampler_name_get (renderer_backend_interface *backend, ksampler_backend sampler) {
	null_renderer_state *state = get_state(backend);
	return sampler < state->sampler_count ? state->sampler_names[sampler] : INVALID_KNAME;
}

// ---------------------------------------------------------------------------
// Renderer settings.
// ---------------------------------------------------------------------------

static b8 null_is_multithreaded (renderer_backend_interface *backend) { return false; }

static b8 null_flag_enabled_get (renderer_backend_interface *backend, renderer_config_flags flag) {
	return FLAG_GET(get_state(backend)->flags, flag);
}

static void null_flag_enabled_set (renderer_backend_interface *backend, renderer_config_flags flag, b8 enabled) {
	null_renderer_state *state = get_state(backend);
	FLAG_SET(state->flags, flag, enabled);
}

static f32 null_max_anisotropy_get (renderer_backend_interface *backend) { return 16.0f; }

// ---------------------------------------------------------------------------
// Renderbuffers: backed by CPU memory so engine systems can write to and read
// from them exactly as they would with a GPU backend.
// ---------------------------------------------------------------------------

static null_renderbuffer *get_renderbuffer (renderer_backend_interface *backend, krenderbuffer handle) {
	null_renderer_state *state = get_state(backend);
	if (handle == KRENDERBUFFER_INVALID || handle >= state->renderbuffer_capacity || !state->renderbuffers[handle].in_use) {
		KERROR("Null renderer: invalid renderbuffer handle %u.", handle);
		return KNULL;
	}
	return &state->renderbuffers[handle];
}

// Returns the buffer's memory, allocating it on first use.
static u8 *renderbuffer_memory (null_renderbuffer *buffer) {
	if (!buffer->data && buffer->size) {
		buffer->data = kallocate(buffer->size, MEMORY_TAG_RENDERER);
	}
	return buffer->data;
}

static b8 range_is_valid (const null_renderbuffer *buffer, u64 offset, u64 size) {
	if (offset + size > buffer->size) {
		KERROR("Null renderer: range [%llu, %llu) is outside of a renderbuffer of size %llu.", offset, offset + size, buffer->size);
		return false;
	}
	return true;
}

static b8 null_renderbuffer_create (renderer_backend_interface *backend, kname name, u64 size, renderbuffer_type type, renderbuffer_flags flags, krenderbuffer handle) {
	null_renderer_state *state = get_state(backend);
	if (handle >= state->renderbuffer_capacity) {
		// Grow to fit the handle. Handles are small, dense indices assigned by the frontend.
		u32 new_capacity = KMAX(handle + 1u, state->renderbuffer_capacity * 2u);
		state->renderbuffers = KREALLOC_TYPE_CARRAY(state->renderbuffers, null_renderbuffer, new_capacity);
		kzero_memory(&state->renderbuffers[state->renderbuffer_capacity], sizeof(null_renderbuffer) * (new_capacity - state->renderbuffer_capacity));
		state->renderbuffer_capacity = new_capacity;
	}

	null_renderbuffer *buffer = &state->renderbuffers[handle];
	if (buffer->data) {
		kfree(buffer->data);
	}
	buffer->in_use = true;
	buffer->size = size;
	buffer->data = KNULL;
	return true;
}

static void null_renderbuffer_destroy (renderer_backend_interface *backend, krenderbuffer handle) {
	null_renderbuffer *buffer = get_renderbuffer(backend, handle);
	if (buffer) {
		if (buffer->data) {
			kfree(buffer->data);
		}
		kzero_memory(buffer, sizeof(null_renderbuffer));
	}
}

static b8 null_renderbuffer_bind (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 binding_index) { return true; }
static b8 null_renderbuffer_unbind (renderer_backend_interface *backend, krenderbuffer handle) { return true; }
static void null_renderbuffer_map_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) {}
static void null_renderbuffer_unmap_memory (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) {}
static b8 null_renderbuffer_flush (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size) { return true; }

static void *null_renderbuffer_get_mapped_memory (renderer_backend_interface *backend, krenderbuffer handle) {
	null_renderbuffer *buffer = get_renderbuffer(backend, handle);
	return buffer ? renderbuffer_memory(buffer) : KNULL;
}

static b8 null_renderbuffer_read (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size, void **out_memory) {
	null_renderbuffer *buffer = get_renderbuffer(backend, handle);
	if (!buffer || !out_memory || !*out_memory || !range_is_valid(buffer, offset, size)) {
		return false;
	}
	kcopy_memory(*out_memory, renderbuffer_memory(buffer) + offset, size);
	return true;
}

static b8 null_renderbuffer_resize (renderer_backend_interface *backend, krenderbuffer handle, u64 new_total_size) {
	null_renderbuffer *buffer = get_renderbuffer(backend, handle);
	if (!buffer) {
		return false;
	}
	if (buffer->data) {
		u8 *new_data = kallocate(new_total_size, MEMORY_TAG_RENDERER);
		kcopy_memory(new_data, buffer->data, KMIN(buffer->size, new_total_size));
		kfree(buffer->data);
		buffer->data = new_data;
	}
	buffer->size = new_total_size;
	return true;
}

static b8 null_renderbuffer_load_range (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u64 size, const void *data, b8 include_in_frame_workload) {
	null_renderbuffer *buffer = get_renderbuffer(backend, handle);
	if (!buffer || !data || !range_is_valid(buffer, offset, size)) {
		return false;
	}
	kcopy_memory(renderbuffer_memory(buffer) + offset, data, size);
	return true;
}

static b8 null_renderbuffer_copy_range (renderer_backend_interface *backend, krenderbuffer source, u64 source_offset, krenderbuffer dest, u64 dest_offset, u64 size, b8 include_in_frame_workload) {
	null_renderbuffer *src = get_renderbuffer(backend, source);
	null_renderbuffer *dst = get_renderbuffer(backend, dest);
	if (!src || !dst || !range_is_valid(src, source_offset, size) || !range_is_valid(dst, dest_offset, size)) {
		return false;
	}
	kcopy_memory(renderbuffer_memory(dst) + dest_offset, renderbuffer_memory(src) + source_offset, size);
	return true;
}

static b8 null_renderbuffer_draw (renderer_backend_interface *backend, krenderbuffer handle, u64 offset, u32 element_count, u32 binding_index, b8 bind_only) { return true; }

// ---------------------------------------------------------------------------
// GPU profiling: there is no GPU, so every event takes zero time.
// ---------------------------------------------------------------------------

static void null_gpu_profiler_initialize (renderer_backend_interface *backend, struct kgpu_profiler *profiler) {}
static void null_gpu_profiler_destroy (renderer_backend_interface *backend, struct kgpu_profiler *profiler) {}
static void null_gpu_profiler_begin_frame (renderer_backend_interface *backend, struct kgpu_profiler *profiler) {}
static void null_gpu_profiler_end_frame (renderer_backend_interface *backend, struct kgpu_profiler *profiler) {}
static void null_gpu_profiler_begin_event (renderer_backend_interface *backend, u32 begin_id) {}
static void null_gpu_profiler_end_event (renderer_backend_interface *backend, u32 end_id) {}

static void null_gpu_profiler_query_timestamps (renderer_backend_interface *backend, u32 begin_id, u64 *out_start, u64 *out_end) {
	*out_start = 0;
	*out_end = 0;
}

#if KOHI_DEBUG
static void null_debug_pump_brakes (renderer_backend_interface *backend) {}
#endif

// ---------------------------------------------------------------------------
// Plugin entry points
// ---------------------------------------------------------------------------

b8 kohi_plugin_renderer_null_create (kruntime_plugin *out_plugin) {
	// The renderer frontend owns shutdown order, so don't let the plugin system unload this.
	out_plugin->block_auto_unload = true;

	out_plugin->plugin_state_size = sizeof(renderer_backend_interface);
	out_plugin->plugin_state = kallocate(out_plugin->plugin_state_size, MEMORY_TAG_RENDERER);

	renderer_backend_interface *backend = out_plugin->plugin_state;

	backend->initialize = null_initialize;
	backend->shutdown = null_shutdown;
	backend->begin_debug_label = null_begin_debug_label;
	backend->end_debug_label = null_end_debug_label;
	backend->window_create = null_window_create;
	backend->window_destroy = null_window_destroy;
	backend->window_resized = null_window_resized;
	backend->frame_prepare = null_frame_prepare;
	backend->frame_prepare_window_surface = null_frame_prepare_window_surface;
	backend->frame_commands_begin = null_frame_commands_begin;
	backend->frame_commands_end = null_frame_commands_end;
	backend->frame_submit = null_frame_submit;
	backend->frame_present = null_frame_present;

	backend->viewport_set = null_viewport_set;
	backend->viewport_reset = null_viewport_reset;
	backend->scissor_set = null_scissor_set;
	backend->scissor_reset = null_scissor_reset;

	backend->clear_depth_set = null_clear_depth_set;
	backend->clear_colour_set = null_clear_colour_set;
	backend->clear_stencil_set = null_clear_stencil_set;
	backend->clear_colour = null_clear_colour;
	backend->clear_depth_stencil = null_clear_depth_stencil;
	backend->colour_texture_prepare_for_present = null_colour_texture_prepare_for_present;
	backend->texture_prepare_for_sampling = null_texture_prepare_for_sampling;

	backend->winding_set = null_winding_set;
	backend->cull_mode_set = null_cull_mode_set;
	backend->set_stencil_test_enabled = null_set_stencil_test_enabled;
	backend->set_depth_test_enabled = null_set_depth_test_enabled;
	backend->set_depth_write_enabled = null_set_depth_write_enabled;
	backend->set_depth_bias = null_set_depth_bias;
	backend->set_depth_bias_enabled = null_set_depth_bias_enabled;
	backend->set_stencil_reference = null_set_stencil_reference;
	backend->set_stencil_op = null_set_stencil_op;
	backend->set_stencil_compare_mask = null_set_stencil_compare_mask;
	backend->set_stencil_write_mask = null_set_stencil_write_mask;

	backend->begin_rendering = null_begin_rendering;
	backend->end_rendering = null_end_rendering;

	backend->texture_resources_acquire = null_texture_resources_acquire;
	backend->texture_resources_release = null_texture_resources_release;
	backend->texture_resize = null_texture_resize;
	backend->texture_write_data = null_texture_write_data;
	backend->texture_read_data = null_texture_read_data;
	backend->texture_read_pixel = null_texture_read_pixel;
	backend->texture_blit_2d = null_texture_blit_2d;

	backend->sampler_acquire = null_sampler_acquire;
	backend->sampler_release = null_sampler_release;
	backend->sampler_refresh = null_sampler_refresh;
	backend->sampler_name_get = null_sampler_name_get;

	backend->shader_create = null_shader_create;
	backend->shader_destroy = null_shader_destroy;
	backend->shader_reload = null_shader_reload;
	backend->shader_use = null_shader_use;
	backend->shader_use_with_topology = null_shader_use_with_topology;
	backend->shader_supports_wireframe = null_shader_supports_wireframe;
	backend->shader_flag_get = null_shader_flag_get;
	backend->shader_flag_set = null_shader_flag_set;
	backend->shader_acquire_binding_set_instance = null_shader_acquire_binding_set_instance;
	backend->shader_release_binding_set_instance = null_shader_release_binding_set_instance;
	backend->shader_binding_set_get_max_instance_count = null_shader_binding_set_get_max_instance_count;
	backend->shader_set_immediate_data = null_shader_set_immediate_data;
	backend->shader_set_binding_data = null_shader_set_binding_data;
	backend->shader_set_binding_texture = null_shader_set_binding_texture;
	backend->shader_set_binding_sampler = null_shader_set_binding_sampler;
	backend->shader_apply_binding_set = null_shader_apply_binding_set;

	backend->is_multithreaded = null_is_multithreaded;
	backend->flag_enabled_get = null_flag_enabled_get;
	backend->flag_enabled_set = null_flag_enabled_set;
	backend->max_anisotropy_get = null_max_anisotropy_get;

	backend->renderbuffer_create = null_renderbuffer_create;
	backend->renderbuffer_destroy = null_renderbuffer_destroy;
	backend->renderbuffer_bind = null_renderbuffer_bind;
	backend->renderbuffer_unbind = null_renderbuffer_unbind;
	backend->renderbuffer_map_memory = null_renderbuffer_map_memory;
	backend->renderbuffer_unmap_memory = null_renderbuffer_unmap_memory;
	backend->renderbuffer_get_mapped_memory = null_renderbuffer_get_mapped_memory;
	backend->renderbuffer_flush = null_renderbuffer_flush;
	backend->renderbuffer_read = null_renderbuffer_read;
	backend->renderbuffer_resize = null_renderbuffer_resize;
	backend->renderbuffer_load_range = null_renderbuffer_load_range;
	backend->renderbuffer_copy_range = null_renderbuffer_copy_range;
	backend->renderbuffer_draw = null_renderbuffer_draw;
	backend->wait_for_idle = null_wait_for_idle;

	backend->gpu_profiler_initialize = null_gpu_profiler_initialize;
	backend->gpu_profiler_destroy = null_gpu_profiler_destroy;
	backend->gpu_profiler_begin_frame = null_gpu_profiler_begin_frame;
	backend->gpu_profiler_end_frame = null_gpu_profiler_end_frame;
	backend->gpu_profiler_begin_event = null_gpu_profiler_begin_event;
	backend->gpu_profiler_end_event = null_gpu_profiler_end_event;
	backend->gpu_profiler_query_timestamps = null_gpu_profiler_query_timestamps;
#if KOHI_DEBUG
	backend->debug_pump_brakes = null_debug_pump_brakes;
#endif

	KINFO("Null Renderer Plugin Creation successful (%s).", KVERSION);

	return true;
}

void kohi_plugin_renderer_null_destroy (kruntime_plugin *plugin) {
	// NOTE: The backend frees its own state in shutdown, mirroring the Vulkan plugin.
}
