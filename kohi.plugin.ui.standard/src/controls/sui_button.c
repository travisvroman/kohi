#include "sui_button.h"

#include <containers/darray.h>
#include <core/systems_manager.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/shader_system.h>

#include "renderer/nine_slice.h"
#include "standard_ui_defines.h"
#include "standard_ui_system.h"
#include "strings/kname.h"

static void sui_button_control_render_frame_prepare(standard_ui_state* state, struct sui_control* self, const struct frame_data* p_frame_data);

b8 sui_button_control_create(standard_ui_state* state, const char* name, struct sui_control* out_control) {
    if (!sui_base_control_create(state, name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_button_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_button_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->size = (vec2i){200, 50};
    typed_data->colour = vec4_one();

    // Assign function pointers.
    out_control->destroy = sui_button_control_destroy;
    out_control->load = sui_button_control_load;
    out_control->unload = sui_button_control_unload;
    out_control->update = sui_button_control_update;
    out_control->render_prepare = sui_button_control_render_frame_prepare;
    out_control->render = sui_button_control_render;

    out_control->internal_mouse_down = sui_button_on_mouse_down;
    out_control->internal_mouse_up = sui_button_on_mouse_up;
    out_control->internal_mouse_out = sui_button_on_mouse_out;
    out_control->internal_mouse_over = sui_button_on_mouse_over;

    out_control->name = string_duplicate(name);
    return true;
}

void sui_button_control_destroy(standard_ui_state* state, struct sui_control* self) {
    sui_base_control_destroy(state, self);
}

b8 sui_button_control_height_set(standard_ui_state* state, struct sui_control* self, i32 height) {
    if (!self) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    typed_data->size.y = height;
    typed_data->nslice.size.y = height;

    self->bounds.height = height;

    nine_slice_update(&typed_data->nslice, 0);

    return true;
}

b8 sui_button_control_load(standard_ui_state* state, struct sui_control* self) {
    if (!sui_base_control_load(state, self)) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;

    // HACK: TODO: remove hardcoded stuff.
    /* vec2i atlas_size = (vec2i){typed_state->ui_atlas.texture->width, typed_state->ui_atlas.texture->height}; */
    vec2i atlas_size = (vec2i){512, 512};
    vec2i atlas_min = (vec2i){151, 12};
    vec2i atlas_max = (vec2i){158, 19};
    vec2i corner_px_size = (vec2i){3, 3};
    vec2i corner_size = (vec2i){10, 10};
    if (!nine_slice_create(self->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice)) {
        KERROR("Failed to generate nine slice.");
        return false;
    }

    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = typed_data->size.x;
    self->bounds.height = typed_data->size.y;

    khandle sui_shader = shader_system_get(kname_create(STANDARD_UI_SHADER_NAME), kname_create(PACKAGE_NAME_STANDARD_UI));
    // Acquire group resources for this control.
    if (!shader_system_shader_group_acquire(sui_shader, &typed_data->group_id)) {
        KFATAL("Unable to acquire shader group resources for button.");
        return false;
    }
    typed_data->group_generation = INVALID_ID_U16;

    // Also acquire per-draw resources.
    if (!shader_system_shader_per_draw_acquire(sui_shader, &typed_data->draw_id)) {
        KFATAL("Unable to acquire shader per-draw resources for button.");
        return false;
    }
    typed_data->draw_generation = INVALID_ID_U16;

    return true;
}

void sui_button_control_unload(standard_ui_state* state, struct sui_control* self) {
    //
}

b8 sui_button_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(state, self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

static void sui_button_control_render_frame_prepare(standard_ui_state* state, struct sui_control* self, const struct frame_data* p_frame_data) {
    if (self) {
        sui_button_internal_data* internal_data = self->internal_data;
        nine_slice_render_frame_prepare(&internal_data->nslice, p_frame_data);
    }
}

b8 sui_button_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    if (typed_data->nslice.vertex_data.elements) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.vertex_count = typed_data->nslice.vertex_data.element_count;
        renderable.render_data.vertex_element_size = typed_data->nslice.vertex_data.element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->nslice.vertex_data.buffer_offset;
        renderable.render_data.index_count = typed_data->nslice.index_data.element_count;
        renderable.render_data.index_element_size = typed_data->nslice.index_data.element_size;
        renderable.render_data.index_buffer_offset = typed_data->nslice.index_data.buffer_offset;
        renderable.render_data.model = xform_world_get(self->xform);
        renderable.render_data.diffuse_colour = vec4_one(); // white. TODO: pull from object properties.

        renderable.group_id = &typed_data->group_id;
        renderable.per_draw_id = &typed_data->draw_id;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}

void sui_button_on_mouse_out(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        sui_button_internal_data* typed_data = self->internal_data;
        typed_data->nslice.atlas_px_min.x = 151;
        typed_data->nslice.atlas_px_min.y = 12;
        typed_data->nslice.atlas_px_max.x = 158;
        typed_data->nslice.atlas_px_max.y = 19;
        nine_slice_update(&typed_data->nslice, 0);
    }
}

void sui_button_on_mouse_over(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        sui_button_internal_data* typed_data = self->internal_data;
        if (self->is_pressed) {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 21;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 28;
        } else {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 31;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 37;
        }
        nine_slice_update(&typed_data->nslice, 0);
    }
}
void sui_button_on_mouse_down(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        sui_button_internal_data* typed_data = self->internal_data;
        typed_data->nslice.atlas_px_min.x = 151;
        typed_data->nslice.atlas_px_min.y = 21;
        typed_data->nslice.atlas_px_max.x = 158;
        typed_data->nslice.atlas_px_max.y = 28;
        nine_slice_update(&typed_data->nslice, 0);
    }
}
void sui_button_on_mouse_up(standard_ui_state* state, struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        // TODO: DRY
        sui_button_internal_data* typed_data = self->internal_data;
        if (self->is_hovered) {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 31;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 37;
        } else {
            typed_data->nslice.atlas_px_min.x = 151;
            typed_data->nslice.atlas_px_min.y = 31;
            typed_data->nslice.atlas_px_max.x = 158;
            typed_data->nslice.atlas_px_max.y = 37;
        }
        nine_slice_update(&typed_data->nslice, 0);
    }
}
