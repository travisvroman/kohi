#include "kui_system.h"

#include <containers/darray.h>
#include <core/engine.h>
#include <core/event.h>
#include <core/input.h>
#include <core_resource_types.h>
#include <debug/kassert.h>
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
#include <strings/kname.h>
#include <strings/kstring.h>
#include <systems/font_system.h>
#include <systems/kshader_system.h>
#include <systems/ktransform_system.h>
#include <systems/texture_system.h>
#include <utils/kcolour.h>
#include <utils/ksort.h>

#include "assets/kasset_types.h"
#include "controls/checkbox_control.h"
#include "controls/image_box_control.h"
#include "controls/kui_button.h"
#include "controls/kui_label.h"
#include "controls/kui_panel.h"
#include "controls/kui_scrollable.h"
#include "controls/kui_textbox.h"
#include "controls/kui_tree_item.h"
#include "kohi.plugin.ui.kui_version.h"
#include "kui_defines.h"
#include "kui_types.h"
#include "parsers/kson_parser.h"
#include "renderer/kui_renderer.h"
#include "systems/asset_system.h"

static b8 kui_base_internal_mouse_down (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_up (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_click (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_over (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_out (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_move (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_wheel (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_drag_begin (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_drag (kui_state *state, kui_control self, struct kui_mouse_event event);
static b8 kui_base_internal_mouse_drag_end (kui_state *state, kui_control self, struct kui_mouse_event event);

static b8 control_and_ancestors_active_r (kui_state *state, const kui_base_control *control);
static b8 control_and_ancestors_visible_r (kui_state *state, const kui_base_control *control);
static b8 control_and_ancestors_active_and_visible_r (kui_state *state, const kui_base_control *control);
static i32 kui_control_depth_compare_desc (void *a, void *b, void *context);
static b8 control_event_intersects (kui_state *typed_state, kui_control control, kui_mouse_event evt);
static b8 kui_system_mouse_down (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_system_mouse_up (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_system_click (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_system_mouse_move (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_system_mouse_wheel (u16 code, void *sender, void *listener_inst, event_context context);
static b8 kui_system_drag (u16 code, void *sender, void *listener_inst, event_context context);

static void register_control (kui_state *state, kui_control control);
static void unregister_control (kui_state *state, kui_control control);

static kui_control encode_handle (kui_control_type type, u16 type_index);
static b8 decode_handle (kui_control handle, kui_control_type *out_type, u16 *out_type_index);
static kui_base_control *get_base (kui_state *state, kui_control control);

static kui_control create_handle (kui_state *state, kui_control_type type);
static void release_handle (kui_state *state, kui_control *handle);

static b8 parse_atlas_config (const char *config_source, kui_atlas_config *out_config);

b8 kui_system_initialize (u64 *memory_requirement, kui_state *state, kui_system_config *config) {
	if (!memory_requirement) {
		KERROR("kui_system_initialize requires a valid pointer to memory_requirement.");
		return false;
	}

	// Memory layout: struct + active control array + inactive_control_array
	u64 struct_requirement = sizeof(kui_state);
	*memory_requirement = struct_requirement;

	if (!state) {
		return true;
	}

	state->renderer = engine_systems_get()->renderer_system;
	state->font_system = engine_systems_get()->font_system;

	state->focused_base_colour = KCOLOUR4_WHITE;
	state->unfocused_base_colour = KCOLOUR4_WHITE;

	kasset_text *atlas_asset = asset_system_request_text_from_package_sync(engine_systems_get()->asset_state, PACKAGE_NAME_KUI, KUI_DEFAULT_ATLAS_ASSET_NAME);
	b8 asset_parse_result = parse_atlas_config(atlas_asset->content, &state->atlas);
	asset_system_release_text(engine_systems_get()->asset_state, atlas_asset);

	if (!asset_parse_result) {
		KERROR("Failed to parse atlas config!");
		return false;
	}

	// Get the shader and the global binding id.
	state->shader = kshader_system_get(kname_create(KUI_SHADER_NAME), kname_create(PACKAGE_NAME_KUI));
	// Acquire binding set resources for this control.
	state->shader_set0_binding_instance_id = INVALID_ID;
	state->shader_set0_binding_instance_id = kshader_acquire_binding_set_instance(state->shader, 0);
	KASSERT(state->shader_set0_binding_instance_id != INVALID_ID);

	state->config = *config;
	state->active_controls = darray_create(kui_control);
	state->inactive_controls = darray_create(kui_control);

	state->base_controls = darray_create(kui_base_control);
	state->panel_controls = darray_create(kui_panel_control);
	state->label_controls = darray_create(kui_label_control);
	state->button_controls = darray_create(kui_button_control);
	state->textbox_controls = darray_create(kui_textbox_control);
	state->tree_item_controls = darray_create(kui_tree_item_control);
	state->scrollable_controls = darray_create(kui_scrollable_control);
	state->image_box_controls = darray_create(kui_image_box_control);
	state->checkbox_controls = darray_create(kui_checkbox_control);
	state->frame_controls = darray_create(kui_frame_control);

	state->root = kui_base_control_create(state, "__ROOT__", KUI_CONTROL_TYPE_BASE);

	// Atlas texture.
	state->atlas_texture = texture_acquire_from_package_sync(
		state->atlas.image_asset_name,
		state->atlas.image_asset_package_name);
	if (state->atlas_texture == INVALID_KTEXTURE) {
		KERROR("Failed to request atlas texture for standard UI.");
		state->atlas_texture = texture_acquire_sync(kname_create(DEFAULT_TEXTURE_NAME));
	}
	texture_dimensions_get(state->atlas_texture, &state->atlas_texture_size.x, &state->atlas_texture_size.y);

	// Listen for input events.
	event_register(EVENT_CODE_BUTTON_CLICKED, state, kui_system_click);
	event_register(EVENT_CODE_MOUSE_MOVED, state, kui_system_mouse_move);
	event_register(EVENT_CODE_MOUSE_DRAG_BEGIN, state, kui_system_drag);
	event_register(EVENT_CODE_MOUSE_DRAGGED, state, kui_system_drag);
	event_register(EVENT_CODE_MOUSE_DRAG_END, state, kui_system_drag);
	event_register(EVENT_CODE_BUTTON_PRESSED, state, kui_system_mouse_down);
	event_register(EVENT_CODE_BUTTON_RELEASED, state, kui_system_mouse_up);
	event_register(EVENT_CODE_MOUSE_WHEEL, state, kui_system_mouse_wheel);

	state->vertex_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_VERTEX_STANDARD));
	state->index_buffer = renderer_renderbuffer_get(state->renderer, kname_create(KRENDERBUFFER_NAME_INDEX_STANDARD));

	state->focused = INVALID_KUI_CONTROL;

	state->running = true;

	KDEBUG("Initialized standard UI system (%s).", KVERSION);

	return true;
}

void kui_system_shutdown (kui_state *state) {
	if (state) {

		state->running = false;

		// Stop listening for input events.
		event_unregister(EVENT_CODE_BUTTON_CLICKED, state, kui_system_click);
		event_unregister(EVENT_CODE_MOUSE_MOVED, state, kui_system_mouse_move);
		event_unregister(EVENT_CODE_MOUSE_DRAG_BEGIN, state, kui_system_drag);
		event_unregister(EVENT_CODE_MOUSE_DRAGGED, state, kui_system_drag);
		event_unregister(EVENT_CODE_MOUSE_DRAG_END, state, kui_system_drag);
		event_unregister(EVENT_CODE_BUTTON_PRESSED, state, kui_system_mouse_down);
		event_unregister(EVENT_CODE_BUTTON_RELEASED, state, kui_system_mouse_up);
		event_unregister(EVENT_CODE_MOUSE_WHEEL, state, kui_system_mouse_wheel);

		// Unload all controls by type
		{
			u32 len = darray_length(state->base_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->base_controls[i].handle.val != INVALID_KUI_CONTROL.val) {
					kui_base_control_destroy(state, &state->base_controls[i].handle);
				}
			}
			darray_destroy(state->base_controls);
		}

		{
			u32 len = darray_length(state->panel_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->panel_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_panel_control_destroy(state, &state->panel_controls[i].base.handle);
				}
			}
			darray_destroy(state->panel_controls);
		}

		{
			u32 len = darray_length(state->label_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->label_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_label_control_destroy(state, &state->label_controls[i].base.handle);
				}
			}
			darray_destroy(state->label_controls);
		}

		{
			u32 len = darray_length(state->button_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->button_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_button_control_destroy(state, &state->button_controls[i].base.handle);
				}
			}
			darray_destroy(state->button_controls);
		}

		{
			u32 len = darray_length(state->textbox_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->textbox_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_textbox_control_destroy(state, &state->textbox_controls[i].base.handle);
				}
			}
			darray_destroy(state->textbox_controls);
		}

		{
			u32 len = darray_length(state->tree_item_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->tree_item_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_tree_item_control_destroy(state, &state->tree_item_controls[i].base.handle);
				}
			}
			darray_destroy(state->tree_item_controls);
		}

		{
			u32 len = darray_length(state->scrollable_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->scrollable_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_scrollable_control_destroy(state, &state->scrollable_controls[i].base.handle);
				}
			}
			darray_destroy(state->scrollable_controls);
		}

		{
			u32 len = darray_length(state->image_box_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->image_box_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_image_box_control_destroy(state, &state->image_box_controls[i].base.handle);
				}
			}
			darray_destroy(state->image_box_controls);
		}

		{
			u32 len = darray_length(state->checkbox_controls);
			for (u32 i = 0; i < len; ++i) {
				if (state->checkbox_controls[i].base.handle.val != INVALID_KUI_CONTROL.val) {
					kui_checkbox_control_destroy(state, &state->checkbox_controls[i].base.handle);
				}
			}
			darray_destroy(state->checkbox_controls);
		}

		darray_destroy(state->inactive_controls);
		state->inactive_controls = KNULL;

		// Unload and destroy active controls.
		darray_destroy(state->active_controls);
		state->active_controls = KNULL;

		// Release texture for UI atlas.
		if (state->atlas_texture) {
			texture_release(state->atlas_texture);
			state->atlas_texture = INVALID_KTEXTURE;
		}
	}
}

b8 kui_system_update (kui_state *state, struct frame_data *p_frame_data) {
	if (!state || !state->running) {
		return false;
	}

	u32 len = darray_length(state->active_controls);
	for (u32 i = 0; i < len; ++i) {
		kui_control handle = state->active_controls[i];
		kui_base_control *c = get_base(state, handle);
		c->update(state, handle, p_frame_data);
	}

	return true;
}

b8 kui_system_render (kui_state *state, kui_control root, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (!state || !state->running) {
		return false;
	}

	if (root.val == INVALID_KUI_CONTROL.val) {
		root = state->root;
	}

	render_data->ui_atlas = state->atlas_texture;
	render_data->shader_set0_binding_instance_id = state->shader_set0_binding_instance_id;

	kui_base_control *base = get_base(state, root);
	if (!base) {
		return false;
	}

	// If there is a clipping mask, insert the begin renderable here.
	if (base->clipping_area.type != KUI_CLIP_TYPE_NONE) {
		kui_renderable clip_begin_renderable;
		if (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) {
			clip_begin_renderable = (kui_renderable){
				.type = KUI_RENDERABLE_TYPE_STENCIL_CLIP_BEGIN,
				.render_data = base->clipping_area.stencil_data.render_data};
		} else {
			clip_begin_renderable = (kui_renderable){
				.type = KUI_RENDERABLE_TYPE_SCISSOR_CLIP_BEGIN,
				.clipping_area = base->clipping_area.scissor_data.clipping_area};
		}
		darray_push(render_data->renderables, clip_begin_renderable);
	}

	if (base->render) {
		if (!base->render(state, root, p_frame_data, render_data)) {
			KERROR("Root element failed to render. See logs for more details");
			return false;
		}
	}

	if (base->children) {
		u32 length = darray_length(base->children);
		for (u32 i = 0; i < length; ++i) {
			kui_control handle = base->children[i];
			kui_base_control *c = get_base(state, handle);
			if (!FLAG_GET(c->flags, KUI_CONTROL_FLAG_VISIBLE_BIT)) {
				continue;
			}
			if (!kui_system_render(state, handle, p_frame_data, render_data)) {
				KERROR("Child element failed to render. See logs for more details");
				return false;
			}
		}
	}

	// If there is a clipping mask, insert the end renderable here.
	if (base->clipping_area.type != KUI_CLIP_TYPE_NONE) {
		kui_renderable clip_end_renderable = {
			.type = (base->clipping_area.type == KUI_CLIP_TYPE_STENCIL) ? KUI_RENDERABLE_TYPE_STENCIL_CLIP_END : KUI_RENDERABLE_TYPE_SCISSOR_CLIP_END};
		darray_push(render_data->renderables, clip_end_renderable);
	}

	return true;
}

kui_base_control *kui_system_get_base (kui_state *state, kui_control control) {
	return get_base(state, control);
}

b8 toggle_active (kui_state *state, kui_control control) {
	if (!state) {
		return false;
	}

	kui_base_control *base = get_base(state, control);
	b8 control_is_active = FLAG_GET(base->flags, KUI_CONTROL_FLAG_ACTIVE_BIT);

	kui_control **src_array = control_is_active ? &state->active_controls : &state->inactive_controls;
	u32 src_count = darray_length(*src_array);
	kui_control **dst_array = control_is_active ? &state->inactive_controls : &state->active_controls;
	u32 dst_count = darray_length(*dst_array);
	for (u32 i = 0; i < src_count; ++i) {
		if ((*src_array)[i].val == control.val) {
			darray_pop_at(*src_array, i, KNULL);
			darray_push(*dst_array, control);

			FLAG_SET(base->flags, KUI_CONTROL_FLAG_ACTIVE_BIT, !control_is_active);
			if (base->active_changed) {
				base->active_changed(state, base->handle, !control_is_active);
			}
			return true;
		}
	}

	// Check the destination and see if it's already there (i.e. it doesn't need an update)
	for (u32 i = 0; i < dst_count; ++i) {
		if ((*dst_array)[i].val == control.val) {
			KFATAL("%s - Control already in the appropriate array for its active state. This should never happen.", __FUNCTION__);
			return false;
		}
	}

	KFATAL("Unable to find control to update active on, maybe control is not registered?");
	return false;
}

static void fix_child_levels_r (kui_state *state, kui_control parent) {
	if (parent.val != INVALID_KUI_CONTROL.val) {
		kui_base_control *parent_base = get_base(state, parent);
		u32 len = parent_base->children ? darray_length(parent_base->children) : 0;
		for (u32 i = 0; i < len; ++i) {
			kui_base_control *child_base = get_base(state, parent_base->children[i]);
			child_base->depth = parent_base->depth + 1;

			// Ensure the clips children/clipped by parent flag is set.
			if (FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT) || FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT)) {
				FLAG_SET(child_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT, true);
			}

			fix_child_levels_r(state, parent_base->children[i]);
		}
	}
}

b8 kui_system_control_add_child (kui_state *state, kui_control parent, kui_control child) {
	KASSERT(parent.val != child.val);
	if (child.val == INVALID_KUI_CONTROL.val) {
		return false;
	}

	if (parent.val == INVALID_KUI_CONTROL.val) {
		parent = state->root;
	}

	kui_base_control *parent_base = get_base(state, parent);
	kui_base_control *child_base = get_base(state, child);

	if (!parent_base->children) {
		parent_base->children = darray_create(kui_control);
	}

	if (child_base->parent.val != INVALID_KUI_CONTROL.val) {
		if (!kui_system_control_remove_child(state, child_base->parent, child)) {
			KERROR("Failed to remove child from parent before reparenting.");
			return false;
		}
	}

	darray_push(parent_base->children, child);
	child_base->parent = parent;
	child_base->depth = parent_base->depth + 1;
	ktransform_parent_set(child_base->ktransform, parent_base->ktransform);

	// If parent either clips children or is clipped by a parent, set the clipped by parent flag.
	if (FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT) || FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT)) {
		FLAG_SET(child_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT, true);
	}

	fix_child_levels_r(state, child);

	return true;
}

b8 kui_system_control_remove_child (kui_state *state, kui_control parent, kui_control child) {
	if (parent.val == INVALID_KUI_CONTROL.val || child.val == INVALID_KUI_CONTROL.val) {
		return false;
	}

	kui_base_control *parent_base = get_base(state, parent);
	kui_base_control *child_base = get_base(state, child);
	if (!parent_base || !child_base) {
		return false;
	}

	if (!parent_base->children) {
		KERROR("Cannot remove a child from a parent which has no children.");
		false;
	}

	// Unset clipping flags.
	FLAG_SET(child_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT, false);
	FLAG_SET(child_base->flags, KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT, false);

	u32 child_count = darray_length(parent_base->children);
	for (u32 i = 0; i < child_count; ++i) {
		if (parent_base->children[i].val == child.val) {
			kui_control popped;
			darray_pop_at(parent_base->children, i, &popped);
			ktransform_parent_set(parent_base->ktransform, KTRANSFORM_INVALID);
			child_base->parent = INVALID_KUI_CONTROL;
			child_base->depth = 0;

			return true;
		}
	}

	KERROR("Unable to remove child which is not a child of given parent.");
	return false;
}

static void clear_focus (kui_state *state) {
	if (state->focused.val != INVALID_KUI_CONTROL.val) {
		kui_base_control *focused_base = get_base(state, state->focused);
		if (focused_base->on_unfocus) {
			focused_base->on_unfocus(state, state->focused);
		}
	}
	state->focused = INVALID_KUI_CONTROL;
}

void kui_system_focus_control (kui_state *state, kui_control control) {
	if (control.val == INVALID_KUI_CONTROL.val) {
		clear_focus(state);
		return;
	}
	kui_base_control *base = get_base(state, control);
	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_FOCUSABLE_BIT)) {
		// Clear current focus.
		clear_focus(state);

		// Only focus new control if it's active and visible.
		if (kui_control_is_visible(state, control) && kui_control_is_active(state, control)) {
			state->focused = control;
			if (base->on_focus) {
				base->on_focus(state, state->focused);
			}
		}
	} else {
		// Clear focus if the control isn't focusable
		clear_focus(state);
	}
}

b8 kui_system_is_control_focused (const kui_state *state, const kui_control control) {
	return state->focused.val == control.val;
}

kui_control kui_base_control_create (kui_state *state, const char *name, kui_control_type type) {

	kui_control handle = create_handle(state, type);

	kui_base_control *out_control = get_base(state, handle);

	out_control->parent = INVALID_KUI_CONTROL;
	out_control->type = type;

	// Set all controls to visible by default.
	FLAG_SET(out_control->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, true);
	// Activate all controls by default.
	FLAG_SET(out_control->flags, KUI_CONTROL_FLAG_ACTIVE_BIT, true);

	// Mouse can interact by default.
	FLAG_SET(out_control->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT, true);
	out_control->depth = 0;

	// Assign function pointers.
	out_control->destroy = kui_base_control_destroy;
	out_control->update = kui_base_control_update;
	out_control->render = kui_base_control_render;

	out_control->name = string_duplicate(name);

	out_control->ktransform = ktransform_create(0);

	// Hook up default internal events. These can be overridden as needed by specialized controls.
	out_control->internal_mouse_down = kui_base_internal_mouse_down;
	out_control->internal_mouse_up = kui_base_internal_mouse_up;
	out_control->internal_click = kui_base_internal_click;
	out_control->internal_mouse_over = kui_base_internal_mouse_over;
	out_control->internal_mouse_out = kui_base_internal_mouse_out;
	out_control->internal_mouse_move = kui_base_internal_mouse_move;
	out_control->internal_mouse_wheel = kui_base_internal_mouse_wheel;
	out_control->internal_mouse_drag_begin = kui_base_internal_mouse_drag_begin;
	out_control->internal_mouse_drag = kui_base_internal_mouse_drag;
	out_control->internal_mouse_drag_end = kui_base_internal_mouse_drag_end;

	register_control(state, handle);

	out_control->handle = handle;

	return handle;
}

void kui_base_control_destroy (kui_state *state, kui_control *self) {
	kui_base_control *base = get_base(state, *self);
	if (!base) {
		KWARN("base not found for release");
		return;
	}

	if (base->name) {
		string_free(base->name);
		base->name = KNULL;
	}

	// Don't recurse if shutting down.
	if (state->running) {
		unregister_control(state, *self);

		if (base->parent.val != INVALID_KUI_CONTROL.val) {
			kui_system_control_remove_child(state, base->parent, *self);
		}

		ktransform_destroy(&base->ktransform);

		u32 len = darray_length(base->children);
		for (u32 i = 0; i < len; ++i) {
			kui_control child_handle = base->children[i];
			kui_base_control *child = get_base(state, child_handle);
			child->parent = INVALID_KUI_CONTROL;
			if (child->destroy) {
				child->destroy(state, &child_handle);
			}
		}
	}

	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_USER_DATA_FREE_ON_DESTROY_BIT)) {
		if (base->user_data && base->user_data_size) {
			kfree(base->user_data, base->user_data_size, base->user_data_memory_tag);
		}
	}

	darray_destroy(base->children);
	base->children = KNULL;

	if (state->running) {
		release_handle(state, self);
	}
}

void kui_control_destroy_all_children (kui_state *state, kui_control control) {
	kui_base_control *base = get_base(state, control);

	u32 len = darray_length(base->children);
	for (u32 i = 0; i < len; ++i) {
		kui_control child_handle = base->children[i];
		kui_base_control *child = get_base(state, child_handle);
		child->parent = INVALID_KUI_CONTROL;
		if (child->destroy) {
			child->destroy(state, &child_handle);
		}
	}
	darray_clear(base->children);
}

b8 kui_base_control_update (kui_state *state, kui_control self, struct frame_data *p_frame_data) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return false;
	}

	return true;
}
b8 kui_base_control_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return false;
	}

	return true;
}

b8 kui_control_is_active (kui_state *state, kui_control self) {
	kui_base_control *base = get_base(state, self);
	return control_and_ancestors_active_r(state, base);
}

b8 kui_control_is_visible (kui_state *state, kui_control self) {
	kui_base_control *base = get_base(state, self);
	return control_and_ancestors_visible_r(state, base);
}

void kui_control_set_is_visible (kui_state *state, kui_control self, b8 is_visible) {
	kui_base_control *base = get_base(state, self);
	FLAG_SET(base->flags, KUI_CONTROL_FLAG_VISIBLE_BIT, is_visible);
	/* KTRACE("Control '%s' set to %s.", base->name, is_visible ? "visible" : "invisible"); */
}
void kui_control_set_is_active (kui_state *state, kui_control self, b8 is_active) {
	kui_base_control *base = get_base(state, self);
	b8 control_is_active = FLAG_GET(base->flags, KUI_CONTROL_FLAG_ACTIVE_BIT);
	if (is_active != control_is_active) {
		toggle_active(state, self);
	}
	/* KTRACE("Control '%s' set to %s.", base->name, is_active ? "active" : "inactive"); */
}

b8 kui_control_get_flag (kui_state *state, kui_control self, kui_control_flag_bits flag) {
	kui_base_control *base = get_base(state, self);
	return FLAG_GET(base->flags, (u32)flag);
}
void kui_control_set_flag (kui_state *state, kui_control self, kui_control_flag_bits flag, b8 enabled) {
	kui_base_control *base = get_base(state, self);
	FLAG_SET(base->flags, flag, enabled);
}

void kui_control_set_user_data (kui_state *state, kui_control self, u32 data_size, void *data, b8 free_on_destroy, memory_tag tag) {
	kui_base_control *base = get_base(state, self);
	FLAG_SET(base->flags, KUI_CONTROL_FLAG_USER_DATA_FREE_ON_DESTROY_BIT, free_on_destroy);
	base->user_data = data;
	base->user_data_size = data_size;
	base->user_data_memory_tag = tag;
}
void *kui_control_get_user_data (kui_state *state, kui_control self) {
	kui_base_control *base = get_base(state, self);
	return base->user_data;
}

void kui_control_set_on_click (kui_state *state, kui_control self, PFN_mouse_event_callback on_click_callback) {
	kui_base_control *base = get_base(state, self);
	base->on_click = on_click_callback;
}

void kui_control_set_on_key (kui_state *state, kui_control self, PFN_keyboard_event_callback on_key_callback) {
	kui_base_control *base = get_base(state, self);
	base->on_key = on_key_callback;
}

void kui_control_position_set (kui_state *state, kui_control self, vec3 position) {
	kui_base_control *base = get_base(state, self);
	if (base) {
		ktransform_position_set(base->ktransform, position);
	}
}

vec3 kui_control_position_get (kui_state *state, kui_control self) {
	kui_base_control *base = get_base(state, self);
	if (base) {
		return ktransform_position_get(base->ktransform);
	}
	KWARN("%s - invalid control, returning zero position.", __FUNCTION__);
	return vec3_zero();
}

static b8 kui_base_internal_mouse_down (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_down ? base->on_mouse_down(state, self, event) : false;
}

static b8 kui_base_internal_mouse_up (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_up ? base->on_mouse_up(state, self, event) : false;
}

static b8 kui_base_internal_click (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_FOCUSABLE_BIT)) {
		if (state->focused.val != self.val) {
			kui_system_focus_control(state, self);
		}
	} else {
		// Something else was clicked, unfocus.
		kui_system_focus_control(state, INVALID_KUI_CONTROL);
	}

	// Block event propagation by default. User events can override this.
	return base->on_click ? base->on_click(state, self, event) : false;
}

static b8 kui_base_internal_mouse_over (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_over ? base->on_mouse_over(state, self, event) : false;
}

static b8 kui_base_internal_mouse_out (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Allow event propagation by default. User events can override this.
	return base->on_mouse_out ? base->on_mouse_out(state, self, event) : true;
}

static b8 kui_base_internal_mouse_move (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_move ? base->on_mouse_move(state, self, event) : false;
}

static b8 kui_base_internal_mouse_wheel (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Allow event propagation by default. User events can override this.
	return base->on_mouse_wheel ? base->on_mouse_wheel(state, self, event) : true;
}

static b8 kui_base_internal_mouse_drag_begin (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	FLAG_SET(base->flags, KUI_CONTROL_FLAG_IS_DRAGGING_BIT, true);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_drag_begin ? base->on_mouse_drag_begin(state, self, event) : false;
}

static b8 kui_base_internal_mouse_drag (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_drag ? base->on_mouse_drag(state, self, event) : false;
}

static b8 kui_base_internal_mouse_drag_end (kui_state *state, kui_control self, struct kui_mouse_event event) {
	if (self.val == INVALID_KUI_CONTROL.val) {
		return true;
	}
	kui_base_control *base = get_base(state, self);

	FLAG_SET(base->flags, KUI_CONTROL_FLAG_IS_DRAGGING_BIT, false);

	// Block event propagation by default. User events can override this.
	return base->on_mouse_drag_end ? base->on_mouse_drag_end(state, self, event) : false;
}

static b8 control_and_ancestors_active_r (kui_state *state, const kui_base_control *control) {
	if (!FLAG_GET(control->flags, KUI_CONTROL_FLAG_ACTIVE_BIT)) {
		return false;
	}

	if (control->parent.val != INVALID_KUI_CONTROL.val) {
		kui_base_control *parent_base = get_base(state, control->parent);
		return control_and_ancestors_active_r(state, parent_base);
	}

	return true;
}

static b8 control_and_ancestors_visible_r (kui_state *state, const kui_base_control *control) {
	if (!FLAG_GET(control->flags, KUI_CONTROL_FLAG_VISIBLE_BIT)) {
		return false;
	}

	if (control->parent.val != INVALID_KUI_CONTROL.val) {
		kui_base_control *parent_base = get_base(state, control->parent);
		return control_and_ancestors_visible_r(state, parent_base);
	}

	return true;
}

static kui_control control_get_clipping_parent_r (kui_state *state, const kui_base_control *control) {
	if (control->parent.val != INVALID_KUI_CONTROL.val) {
		kui_base_control *parent_base = get_base(state, control->parent);
		if (FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPS_CHILDREN_BIT)) {
			// This is the clipping control.
			return control->parent;
		}
		// If this control is also clipped by a parent, try further up.
		if (FLAG_GET(parent_base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT)) {
			return control_get_clipping_parent_r(state, parent_base);
		}
	}

	// Control has no parent, thus no clipping parent.
	return INVALID_KUI_CONTROL;
}

static b8 control_and_ancestors_active_and_visible_r (kui_state *state, const kui_base_control *control) {
	return control_and_ancestors_active_r(state, control) && control_and_ancestors_visible_r(state, control);
}

static i32 kui_control_depth_compare_desc (void *a, void *b, void *context) {
	kui_state *state = context;
	kui_base_control *a_typed = get_base(state, *(kui_control *)a);
	kui_base_control *b_typed = get_base(state, *(kui_control *)b);
	if (a_typed->depth > b_typed->depth) {
		return 1;
	} else if (a_typed->depth < b_typed->depth) {
		return -1;
	}
	return 0;
}

static b8 control_event_intersects (kui_state *typed_state, kui_control control, kui_mouse_event evt) {
	kui_base_control *base = get_base(typed_state, control);

	// Check if control is active and visible. This should check recursively upward to make sure any
	// disabled/invisible parent controls are taken into account.
	if (!control_and_ancestors_active_and_visible_r(typed_state, base)) {
		// Skip ones that aren't.
		return false;
	}

	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_CLIPPED_BY_PARENT_BIT)) {
		// Get the clipping parent control, and get its bounds, transformed as below, and perform
		// that test first. If that passes, then do below, otherwise boot.
		kui_control clipping_parent = control_get_clipping_parent_r(typed_state, base);
		if (clipping_parent.val != INVALID_KUI_CONTROL.val) {
			kui_base_control *parent = get_base(typed_state, clipping_parent);
			mat4 parent_model = ktransform_world_get(parent->ktransform);
			mat4 parent_inv = mat4_inverse(parent_model);
			vec3 parent_transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, parent_inv);

			/* KTRACE("Got clipping parent, checking contains..."); */

			if (!rect_2d_contains_point(parent->bounds, (vec2){parent_transformed_evt.x, parent_transformed_evt.y})) {
				/* KTRACE("Click not contained in clipping parent, booting."); */
				// Not inside the parent's bounds, boot.
				return false;
			}
		}
	}

	mat4 model = ktransform_world_get(base->ktransform);
	mat4 inv = mat4_inverse(model);
	vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);

	return rect_2d_contains_point(base->bounds, (vec2){transformed_evt.x, transformed_evt.y});
}

static b8 kui_system_mouse_down (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.mouse_button = (mouse_buttons)context.data.i16[4],
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (base->internal_mouse_down) {
			if (!base->internal_mouse_down(typed_state, control, evt)) {
				/* KTRACE("ui mouse down on '%s', blocking propagation.", base->name); */
				block_propagation = true;
				// If propagation is blocked, don't look any further.
				break;
			}
		}
	}
	darray_destroy(intersecting_controls);

	/* KTRACE("ui mouse down, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If no control was hit, make sure there is no focused control.
	if (!hit_count) {
		kui_system_focus_control(typed_state, INVALID_KUI_CONTROL);
	}

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 kui_system_mouse_up (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.mouse_button = (mouse_buttons)context.data.i16[4],
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (base->internal_mouse_up) {
			if (!base->internal_mouse_up(typed_state, control, evt)) {
				block_propagation = true;
				// If propagation is blocked, don't look any further.
				break;
			}
		}
	}
	darray_destroy(intersecting_controls);

	/* KTRACE("ui mouse up, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If no control was hit, make sure there is no focused control.
	if (!hit_count) {
		kui_system_focus_control(typed_state, INVALID_KUI_CONTROL);
	}

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 kui_system_click (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.mouse_button = (mouse_buttons)context.data.i16[4],
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (base->internal_click) {
			if (!base->internal_click(typed_state, control, evt)) {
				block_propagation = true;
				// If propagation is blocked, don't look any further.
				break;
			}
		}
	}
	darray_destroy(intersecting_controls);

	/* KTRACE("ui mouse click, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If no control was hit, make sure there is no focused control.
	if (!hit_count) {
		kui_system_focus_control(typed_state, INVALID_KUI_CONTROL);
	}

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 kui_system_mouse_move (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.mouse_button = (mouse_buttons)context.data.i16[4],
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	kui_control *non_intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		} else {
			darray_push(non_intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_HOVERED_BIT) && base->internal_mouse_over) {
			FLAG_SET(base->flags, KUI_CONTROL_FLAG_HOVERED_BIT, true);
			if (!base->internal_mouse_over(typed_state, control, evt)) {
				block_propagation = true;
			}
		}

		if (base->internal_mouse_move) {
			if (!base->internal_mouse_move(typed_state, control, evt)) {
				block_propagation = true;
			}
		}

		/* if (block_propagation) {
			// If propagation is blocked, don't look any further.
			break;
		} */
	}
	darray_destroy(intersecting_controls);

	// Outside functions don't block propagation... for now.
	u32 non_hit_count = darray_length(non_intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), non_intersecting_controls, 0, non_hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < non_hit_count; ++i) {
		kui_control control = non_intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_HOVERED_BIT) && base->internal_mouse_out) {
			FLAG_SET(base->flags, KUI_CONTROL_FLAG_HOVERED_BIT, false);
			if (!base->internal_mouse_out(typed_state, control, evt)) {
				block_propagation = true;
				// If propagation is blocked, don't look any further.
				/* break; */
			}
		}
	}
	darray_destroy(non_intersecting_controls);

	/* KTRACE("ui mouse move, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 kui_system_mouse_wheel (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
		.delta_z = context.data.i8[8],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);

		if (!base->internal_mouse_wheel(typed_state, control, evt)) {
			block_propagation = true;
			// If propagation is blocked, don't look any further.
			break;
		}
	}
	darray_destroy(intersecting_controls);

	/* KTRACE("ui mouse wheel, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static b8 kui_system_drag (u16 code, void *sender, void *listener_inst, event_context context) {
	kui_state *typed_state = (kui_state *)listener_inst;
	b8 block_propagation = false;
	kui_mouse_event evt = {
		.mouse_button = (mouse_buttons)context.data.i16[4],
		.x = context.data.i16[0],
		.y = context.data.i16[1],
		.delta_x = context.data.i16[2],
		.delta_y = context.data.i16[3],
	};

	// Active, visible controls that the event intersects.
	kui_control *intersecting_controls = darray_create(kui_control);
	kui_control *non_intersecting_controls = darray_create(kui_control);
	u32 active_control_count = darray_length(typed_state->active_controls);
	for (u32 i = 0; i < active_control_count; ++i) {
		kui_control control = typed_state->active_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}
		if (control_event_intersects(typed_state, control, evt)) {
			darray_push(intersecting_controls, control);
		} else {
			darray_push(non_intersecting_controls, control);
		}
	}

	// Sort and get the topmost elements first.
	u32 hit_count = darray_length(intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), intersecting_controls, 0, hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < hit_count; ++i) {
		kui_control control = intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (code == EVENT_CODE_MOUSE_DRAG_BEGIN) {
			// Drag begin must start within the control.
			if (base->internal_mouse_drag_begin) {

				mat4 model = ktransform_world_get(base->ktransform);
				mat4 inv = mat4_inverse(model);
				vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
				evt.local_x = transformed_evt.x;
				evt.local_y = transformed_evt.y;

				if (!base->internal_mouse_drag_begin(typed_state, control, evt)) {
					block_propagation = true;
				}
			}
		} else if (code == EVENT_CODE_MOUSE_DRAGGED) {
			// Drag event can occur inside or outside the control.

			mat4 model = ktransform_world_get(base->ktransform);
			mat4 inv = mat4_inverse(model);
			vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
			evt.local_x = transformed_evt.x;
			evt.local_y = transformed_evt.y;

			if (base->internal_mouse_drag) {
				if (!base->internal_mouse_drag(typed_state, control, evt)) {
					block_propagation = true;
				}
			}
		} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
			// Drag end event can occur inside or outside the control.

			mat4 model = ktransform_world_get(base->ktransform);
			mat4 inv = mat4_inverse(model);
			vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
			evt.local_x = transformed_evt.x;
			evt.local_y = transformed_evt.y;

			if (base->internal_mouse_drag_end) {
				if (!base->internal_mouse_drag_end(typed_state, control, evt)) {
					block_propagation = true;
				}
			}
		}

		if (block_propagation) {
			// If propagation is blocked, don't look any further.
			break;
		}
	}
	darray_destroy(intersecting_controls);

	// Outside functions don't block propagation... for now.
	u32 non_hit_count = darray_length(non_intersecting_controls);
	kquick_sort_with_context(sizeof(kui_control), non_intersecting_controls, 0, non_hit_count - 1, kui_control_depth_compare_desc, typed_state);
	for (u32 i = 0; i < non_hit_count; ++i) {
		kui_control control = non_intersecting_controls[i];
		kui_base_control *base = get_base(typed_state, control);
		if (!FLAG_GET(base->flags, KUI_CONTROL_FLAG_CAN_MOUSE_INTERACT_BIT)) {
			continue; // Skip controls that are turned off for mouse interactions.
		}

		if (code == EVENT_CODE_MOUSE_DRAGGED) {
			// Drag event can occur inside or outside the control.
			if (base->internal_mouse_drag) {
				// if (!base->internal_mouse_drag(typed_state, control, evt)) {
				// 	block_propagation = true;
				// }
			}
		} else if (code == EVENT_CODE_MOUSE_DRAG_END) {
			// Drag end event can occur inside or outside the control.
			if (base->internal_mouse_drag_end) {
				// if (!base->internal_mouse_drag_end(typed_state, control, evt)) {
				// 	block_propagation = true;
				// }
			}
		}

		if (block_propagation) {
			// If propagation is blocked, don't look any further.
			break;
		}
	}
	darray_destroy(non_intersecting_controls);

	/* KTRACE("ui mouse drag, block_propagation = %s", block_propagation ? "yes" : "no"); */

	// If no control was hit, make sure there is no focused control.
	if (!hit_count) {
		kui_system_focus_control(typed_state, INVALID_KUI_CONTROL);
	}

	// If a control was hit, block the event from going any futher.
	return block_propagation;
}

static void register_control (kui_state *state, kui_control control) {
	state->total_control_count++;

	kui_base_control *base = get_base(state, control);
	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_ACTIVE_BIT)) {
		darray_push(state->active_controls, control);
	} else {
		darray_push(state->inactive_controls, control);
	}
}

static void unregister_control (kui_state *state, kui_control control) {
	if (!state->running) {
		return;
	}

	state->total_control_count--;

	kui_base_control *base = get_base(state, control);
	if (FLAG_GET(base->flags, KUI_CONTROL_FLAG_ACTIVE_BIT)) {
		u32 len = darray_length(state->active_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->active_controls[i].val == control.val) {
				darray_pop_at(state->active_controls, i, KNULL);
				return;
			}
		}
	} else {
		u32 len = darray_length(state->inactive_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->inactive_controls[i].val == control.val) {
				darray_pop_at(state->inactive_controls, i, KNULL);
				return;
			}
		}
	}
}

static kui_control encode_handle (kui_control_type type, u16 type_index) {
	return (kui_control){PACK_U32_U16S((u16)type, type_index)};
}
static b8 decode_handle (kui_control handle, kui_control_type *out_type, u16 *out_type_index) {
	if (handle.val == INVALID_KUI_CONTROL.val) {
		return false;
	}
	UNPACK_U32_U16S(handle.val, *out_type, *out_type_index);
	return true;
}

static kui_base_control *get_base (kui_state *state, kui_control control) {
	kui_control_type type;
	u16 type_index;
	u32 len = 0;
	if (!decode_handle(control, &type, &type_index)) {
		return KNULL;
	}

	kui_base_control *base = KNULL;
	switch (type) {
	case KUI_CONTROL_TYPE_BASE:
		len = darray_length(state->base_controls);
		base = type_index < len ? &state->base_controls[type_index] : KNULL;
		break;
	case KUI_CONTROL_TYPE_PANEL:
		len = darray_length(state->panel_controls);
		base = type_index < len ? &state->panel_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_LABEL:
		len = darray_length(state->label_controls);
		base = type_index < len ? &state->label_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_BUTTON:
		len = darray_length(state->button_controls);
		base = type_index < len ? &state->button_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_TEXTBOX: {
		len = darray_length(state->textbox_controls);
		base = type_index < len ? &state->textbox_controls[type_index].base : KNULL;
	} break;
	case KUI_CONTROL_TYPE_TREE_ITEM:
		len = darray_length(state->tree_item_controls);
		base = type_index < len ? &state->tree_item_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_SCROLLABLE:
		len = darray_length(state->scrollable_controls);
		base = type_index < len ? &state->scrollable_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_IMAGE_BOX:
		len = darray_length(state->image_box_controls);
		base = type_index < len ? &state->image_box_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_CHECKBOX:
		len = darray_length(state->checkbox_controls);
		base = type_index < len ? &state->checkbox_controls[type_index].base : KNULL;
		break;
	case KUI_CONTROL_TYPE_FRAME:
		len = darray_length(state->frame_controls);
		base = type_index < len ? &state->frame_controls[type_index].base : KNULL;
		break;
	// TODO: user type support
	case KUI_CONTROL_TYPE_MAX:
	case KUI_CONTROL_TYPE_NONE:
		return KNULL;
	}

	return base;
}

static kui_control create_handle (kui_state *state, kui_control_type type) {
	u16 type_index = INVALID_ID_U16;
	u16 len = 0;
	kui_base_control *base = KNULL;
	switch (type) {
	case KUI_CONTROL_TYPE_BASE:
		len = darray_length(state->base_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->base_controls[i].type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->base_controls, (kui_base_control){0});
		}
		base = &state->base_controls[type_index];
		break;
	case KUI_CONTROL_TYPE_PANEL:
		len = darray_length(state->panel_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->panel_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->panel_controls, (kui_panel_control){0});
		}
		base = &state->panel_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_LABEL:
		len = darray_length(state->label_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->label_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->label_controls, (kui_label_control){0});
		}
		base = &state->label_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_BUTTON:
		len = darray_length(state->button_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->button_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->button_controls, (kui_button_control){0});
		}
		base = &state->button_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_TEXTBOX:
		len = darray_length(state->textbox_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->textbox_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->textbox_controls, (kui_textbox_control){0});
		}
		base = &state->textbox_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_TREE_ITEM:
		len = darray_length(state->tree_item_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->tree_item_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->tree_item_controls, (kui_tree_item_control){0});
		}
		base = &state->tree_item_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_SCROLLABLE:
		len = darray_length(state->scrollable_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->scrollable_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->scrollable_controls, (kui_scrollable_control){0});
		}
		base = &state->scrollable_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_IMAGE_BOX:
		len = darray_length(state->image_box_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->image_box_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->image_box_controls, (kui_image_box_control){0});
		}
		base = &state->image_box_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_CHECKBOX:
		len = darray_length(state->checkbox_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->checkbox_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->checkbox_controls, (kui_checkbox_control){0});
		}
		base = &state->checkbox_controls[type_index].base;
		break;
	case KUI_CONTROL_TYPE_FRAME:
		len = darray_length(state->frame_controls);
		for (u32 i = 0; i < len; ++i) {
			if (state->frame_controls[i].base.type == KUI_CONTROL_TYPE_NONE) {
				type_index = i;
				break;
			}
		}
		if (type_index == INVALID_ID_U16) {
			type_index = len;
			darray_push(state->frame_controls, (kui_frame_control){0});
		}
		base = &state->frame_controls[type_index].base;
		break;
		// TODO: user type support
	case KUI_CONTROL_TYPE_MAX:
	case KUI_CONTROL_TYPE_NONE:
		return INVALID_KUI_CONTROL;
	}

	base->type = type;

	return encode_handle(type, type_index);
}

static void release_handle (kui_state *state, kui_control *handle) {
	kui_control_type type;
	u16 type_index;
	kui_base_control *base = KNULL;
	if (decode_handle(*handle, &type, &type_index)) {
		switch (type) {
		case KUI_CONTROL_TYPE_BASE:
			kzero_memory(&state->base_controls[type_index], sizeof(kui_base_control));
			base = &state->base_controls[type_index];
			break;
		case KUI_CONTROL_TYPE_PANEL:
			kzero_memory(&state->panel_controls[type_index], sizeof(kui_panel_control));
			base = &state->panel_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_LABEL:
			kzero_memory(&state->label_controls[type_index], sizeof(kui_label_control));
			base = &state->label_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_BUTTON:
			kzero_memory(&state->button_controls[type_index], sizeof(kui_button_control));
			base = &state->button_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_TEXTBOX:
			kzero_memory(&state->textbox_controls[type_index], sizeof(kui_textbox_control));
			base = &state->textbox_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_TREE_ITEM:
			kzero_memory(&state->tree_item_controls[type_index], sizeof(kui_tree_item_control));
			base = &state->tree_item_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_SCROLLABLE:
			kzero_memory(&state->scrollable_controls[type_index], sizeof(kui_scrollable_control));
			base = &state->scrollable_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_IMAGE_BOX:
			kzero_memory(&state->image_box_controls[type_index], sizeof(kui_image_box_control));
			base = &state->image_box_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_CHECKBOX:
			kzero_memory(&state->checkbox_controls[type_index], sizeof(kui_checkbox_control));
			base = &state->checkbox_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_FRAME:
			kzero_memory(&state->frame_controls[type_index], sizeof(kui_frame_control));
			base = &state->frame_controls[type_index].base;
			break;
		case KUI_CONTROL_TYPE_MAX:
		case KUI_CONTROL_TYPE_NONE:
			// TODO: user type support
			break; // do nothing
		}

		base->handle = INVALID_KUI_CONTROL;
		base->parent = INVALID_KUI_CONTROL;
		base->ktransform = KTRANSFORM_INVALID;
	}
	*handle = INVALID_KUI_CONTROL;
}

static b8 parse_atlas_config (const char *config_source, kui_atlas_config *out_config) {
	kson_tree tree = {0};
	if (!kson_tree_from_string(config_source, &tree)) {
		KERROR("%s - parsing failed.", __FUNCTION__);
		return false;
	}

	b8 success = false;
	const char *name_str = KNULL;

	if (!kson_object_property_value_get_string_as_kname(&tree.root, "image_asset_name", &out_config->image_asset_name)) {
		KERROR("%s - Parse failed: required property 'image_asset_name' missing.", __FUNCTION__);
		goto parse_atlas_config_cleanup;
	}

	if (!kson_object_property_value_get_string_as_kname(&tree.root, "image_asset_package_name", &out_config->image_asset_package_name)) {
		KERROR("%s - Parse failed: required property 'image_asset_package_name' missing.", __FUNCTION__);
		goto parse_atlas_config_cleanup;
	}

	if (!kson_object_property_value_get_string_as_kname(&tree.root, "image_asset_package_name", &out_config->image_asset_package_name)) {
		KERROR("%s - Parse failed: required property 'image_asset_package_name' missing.", __FUNCTION__);
		goto parse_atlas_config_cleanup;
	}

	kson_array controls_array;
	if (!kson_object_property_value_get_array(&tree.root, "controls", &controls_array)) {
		KERROR("%s - Parse failed: required property 'controls' missing.", __FUNCTION__);
		goto parse_atlas_config_cleanup;
	}

	u32 type_count = 0;
	kson_array_element_count_get(&controls_array, &type_count);
	for (u32 i = 0; i < type_count; ++i) {
		kson_object control_obj;
		kson_array_element_value_get_object(&controls_array, i, &control_obj);

		if (!kson_object_property_value_get_string(&control_obj, "type", &name_str)) {
			KERROR("%s - Parse failed: required property 'type' missing from controls[%u].", __FUNCTION__, i);
			goto parse_atlas_config_cleanup;
		}

		if (strings_equali(name_str, "panel")) {
			// Process panel properties
			kson_object_property_value_get_extents_2d(&control_obj, "extents", &out_config->panel.extents);
		} else if (strings_equali(name_str, "button")) {
			// Process button properties
			kson_object modes_obj;
			if (!kson_object_property_value_get_object(&control_obj, "modes", &modes_obj)) {
				KERROR("%s - Required property 'modes' found from controls[%u].", __FUNCTION__, i);
				goto parse_atlas_config_cleanup;
			}

			// Normal
			kson_object normal_obj;
			kson_object_property_value_get_object(&modes_obj, "normal", &normal_obj);
			kson_object_property_value_get_extents_2d(&normal_obj, "extents", &out_config->button.normal.extents);
			kson_object_property_value_get_vec2(&normal_obj, "corner_size", &out_config->button.normal.corner_size);
			kson_object_property_value_get_vec2(&normal_obj, "corner_px_size", &out_config->button.normal.corner_px_size);

			// Hover
			kson_object hover_obj;
			kson_object_property_value_get_object(&modes_obj, "hover", &hover_obj);
			kson_object_property_value_get_extents_2d(&hover_obj, "extents", &out_config->button.hover.extents);
			kson_object_property_value_get_vec2(&hover_obj, "corner_size", &out_config->button.hover.corner_size);
			kson_object_property_value_get_vec2(&hover_obj, "corner_px_size", &out_config->button.hover.corner_px_size);

			// Pressed
			kson_object pressed_obj;
			kson_object_property_value_get_object(&modes_obj, "pressed", &pressed_obj);
			kson_object_property_value_get_extents_2d(&pressed_obj, "extents", &out_config->button.pressed.extents);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_size", &out_config->button.pressed.corner_size);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_px_size", &out_config->button.pressed.corner_px_size);

		} else if (strings_equali(name_str, "button_uparrow")) {
			// Process button_uparrow properties
			kson_object modes_obj;
			if (!kson_object_property_value_get_object(&control_obj, "modes", &modes_obj)) {
				KERROR("%s - Required property 'modes' found from controls[%u].", __FUNCTION__, i);
				goto parse_atlas_config_cleanup;
			}

			// Normal
			kson_object normal_obj;
			kson_object_property_value_get_object(&modes_obj, "normal", &normal_obj);
			kson_object_property_value_get_extents_2d(&normal_obj, "extents", &out_config->button_uparrow.normal.extents);
			kson_object_property_value_get_vec2(&normal_obj, "corner_size", &out_config->button_uparrow.normal.corner_size);
			kson_object_property_value_get_vec2(&normal_obj, "corner_px_size", &out_config->button_uparrow.normal.corner_px_size);

			// Hover
			kson_object hover_obj;
			kson_object_property_value_get_object(&modes_obj, "hover", &hover_obj);
			kson_object_property_value_get_extents_2d(&hover_obj, "extents", &out_config->button_uparrow.hover.extents);
			kson_object_property_value_get_vec2(&hover_obj, "corner_size", &out_config->button_uparrow.hover.corner_size);
			kson_object_property_value_get_vec2(&hover_obj, "corner_px_size", &out_config->button_uparrow.hover.corner_px_size);

			// Pressed
			kson_object pressed_obj;
			kson_object_property_value_get_object(&modes_obj, "pressed", &pressed_obj);
			kson_object_property_value_get_extents_2d(&pressed_obj, "extents", &out_config->button_uparrow.pressed.extents);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_size", &out_config->button_uparrow.pressed.corner_size);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_px_size", &out_config->button_uparrow.pressed.corner_px_size);

		} else if (strings_equali(name_str, "button_downarrow")) {
			// Process button_downarrow properties
			kson_object modes_obj;
			if (!kson_object_property_value_get_object(&control_obj, "modes", &modes_obj)) {
				KERROR("%s - Required property 'modes' found from controls[%u].", __FUNCTION__, i);
				goto parse_atlas_config_cleanup;
			}

			// Normal
			kson_object normal_obj;
			kson_object_property_value_get_object(&modes_obj, "normal", &normal_obj);
			kson_object_property_value_get_extents_2d(&normal_obj, "extents", &out_config->button_downarrow.normal.extents);
			kson_object_property_value_get_vec2(&normal_obj, "corner_size", &out_config->button_downarrow.normal.corner_size);
			kson_object_property_value_get_vec2(&normal_obj, "corner_px_size", &out_config->button_downarrow.normal.corner_px_size);

			// Hover
			kson_object hover_obj;
			kson_object_property_value_get_object(&modes_obj, "hover", &hover_obj);
			kson_object_property_value_get_extents_2d(&hover_obj, "extents", &out_config->button_downarrow.hover.extents);
			kson_object_property_value_get_vec2(&hover_obj, "corner_size", &out_config->button_downarrow.hover.corner_size);
			kson_object_property_value_get_vec2(&hover_obj, "corner_px_size", &out_config->button_downarrow.hover.corner_px_size);

			// Pressed
			kson_object pressed_obj;
			kson_object_property_value_get_object(&modes_obj, "pressed", &pressed_obj);
			kson_object_property_value_get_extents_2d(&pressed_obj, "extents", &out_config->button_downarrow.pressed.extents);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_size", &out_config->button_downarrow.pressed.corner_size);
			kson_object_property_value_get_vec2(&pressed_obj, "corner_px_size", &out_config->button_downarrow.pressed.corner_px_size);

		} else if (strings_equali(name_str, "textbox")) {
			kson_object modes_obj;
			if (!kson_object_property_value_get_object(&control_obj, "modes", &modes_obj)) {
				KERROR("%s - Required property 'modes' found from controls[%u].", __FUNCTION__, i);
				goto parse_atlas_config_cleanup;
			}

			// Normal
			kson_object normal_obj;
			kson_object_property_value_get_object(&modes_obj, "normal", &normal_obj);
			kson_object_property_value_get_extents_2d(&normal_obj, "extents", &out_config->textbox.normal.extents);
			kson_object_property_value_get_vec2(&normal_obj, "corner_size", &out_config->textbox.normal.corner_size);
			kson_object_property_value_get_vec2(&normal_obj, "corner_px_size", &out_config->textbox.normal.corner_px_size);

			// Hover
			kson_object focused_obj;
			kson_object_property_value_get_object(&modes_obj, "focused", &focused_obj);
			kson_object_property_value_get_extents_2d(&focused_obj, "extents", &out_config->textbox.focused.extents);
			kson_object_property_value_get_vec2(&focused_obj, "corner_size", &out_config->textbox.focused.corner_size);
			kson_object_property_value_get_vec2(&focused_obj, "corner_px_size", &out_config->textbox.focused.corner_px_size);
		} else if (strings_equali(name_str, "frame")) {
			kson_object_property_value_get_extents_2d(&control_obj, "extents", &out_config->frame.extents);
			kson_object_property_value_get_vec2(&control_obj, "corner_size", &out_config->frame.corner_size);
			kson_object_property_value_get_vec2(&control_obj, "corner_px_size", &out_config->frame.corner_px_size);
		} else if (strings_equali(name_str, "scrollbar")) {

			// bg nine-slice
			kson_object_property_value_get_extents_2d(&control_obj, "bg_extents", &out_config->scrollbar.extents);
			kson_object_property_value_get_vec2(&control_obj, "bg_corner_size", &out_config->scrollbar.corner_size);
			kson_object_property_value_get_vec2(&control_obj, "bg_corner_px_size", &out_config->scrollbar.corner_px_size);
		} else if (strings_equali(name_str, "checkbox")) {
			kson_object_property_value_get_rect_2di(&control_obj, "enabled_unchecked_rect", &out_config->checkbox.enabled_unchecked_rect);
			kson_object_property_value_get_rect_2di(&control_obj, "enabled_checked_rect", &out_config->checkbox.enabled_checked_rect);
			kson_object_property_value_get_rect_2di(&control_obj, "disabled_unchecked_rect", &out_config->checkbox.disabled_unchecked_rect);
			kson_object_property_value_get_rect_2di(&control_obj, "disabled_checked_rect", &out_config->checkbox.disabled_checked_rect);

			vec2 size = vec2_zero();
			kson_object_property_value_get_vec2(&control_obj, "image_box_size", &size);
			out_config->checkbox.image_box_size = (vec2i){.x = size.x, .y = size.y};
		} else {
			KERROR("%s - Unknown type '%s' found from controls[%u]. It will be ignored", __FUNCTION__, name_str, i);
		}

		string_free(name_str);
		name_str = KNULL;
	}

	success = true;
parse_atlas_config_cleanup:

	string_free(name_str);
	kson_tree_cleanup(&tree);

	return success;
}
