#include "sui_panel.h"
#include "standard_ui_defines.h"

#include <containers/darray.h>
#include <logger.h>
#include <math/geometry.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <strings/kstring.h>
#include <systems/shader_system.h>

static void sui_panel_control_render_frame_prepare(standard_ui_state* state, struct sui_control* self, const struct frame_data* p_frame_data);

b8 sui_panel_control_create(standard_ui_state* state, const char* name, vec2 size, vec4 colour, struct sui_control* out_control) {
    if (!sui_base_control_create(state, name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_panel_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_panel_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->rect = vec4_create(0, 0, size.x, size.y);
    typed_data->colour = colour;
    typed_data->is_dirty = true;

    // Assign function pointers.
    out_control->destroy = sui_panel_control_destroy;
    out_control->load = sui_panel_control_load;
    out_control->unload = sui_panel_control_unload;
    out_control->update = sui_panel_control_update;
    out_control->render_prepare = sui_panel_control_render_frame_prepare;
    out_control->render = sui_panel_control_render;

    out_control->name = string_duplicate(name);
    return true;
}

void sui_panel_control_destroy(standard_ui_state* state, struct sui_control* self) {
    sui_base_control_destroy(state, self);
}

b8 sui_panel_control_load(standard_ui_state* state, struct sui_control* self) {
    if (!sui_base_control_load(state, self)) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;

    // Generate UVs.
    f32 xmin, ymin, xmax, ymax;
    generate_uvs_from_image_coords(512, 512, 44, 7, &xmin, &ymin);
    generate_uvs_from_image_coords(512, 512, 73, 36, &xmax, &ymax);

    // Create a simple plane.
    typed_data->g = geometry_generate_quad(typed_data->rect.width, typed_data->rect.height, xmin, xmax, ymin, ymax, kname_create(self->name));
    if (!renderer_geometry_upload(&typed_data->g)) {
        KERROR("sui_panel_control_load - Failed to upload geometry quad");
        return false;
    }

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

void sui_panel_control_unload(standard_ui_state* state, struct sui_control* self) {
}

b8 sui_panel_control_update(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(state, self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

b8 sui_panel_control_render(standard_ui_state* state, struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(state, self, p_frame_data, render_data)) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;
    if (typed_data->g.vertices) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.vertex_count = typed_data->g.vertex_count;
        renderable.render_data.vertex_element_size = typed_data->g.vertex_element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->g.vertex_buffer_offset;
        renderable.render_data.index_count = typed_data->g.index_count;
        renderable.render_data.index_element_size = typed_data->g.index_element_size;
        renderable.render_data.index_buffer_offset = typed_data->g.index_buffer_offset;
        renderable.render_data.model = xform_world_get(self->xform);
        renderable.render_data.diffuse_colour = typed_data->colour;

        renderable.group_id = &typed_data->group_id;
        renderable.per_draw_id = &typed_data->draw_id;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}
vec2 sui_panel_size(standard_ui_state* state, struct sui_control* self) {
    if (!self) {
        return vec2_zero();
    }

    sui_panel_internal_data* typed_data = self->internal_data;
    return (vec2){typed_data->rect.width, typed_data->rect.height};
}

b8 sui_panel_control_resize(standard_ui_state* state, struct sui_control* self, vec2 new_size) {
    if (!self) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;

    typed_data->rect.width = new_size.x;
    typed_data->rect.height = new_size.y;
    vertex_2d* vertices = typed_data->g.vertices;
    vertices[1].position.y = new_size.y;
    vertices[1].position.x = new_size.x;
    vertices[2].position.y = new_size.y;
    vertices[3].position.x = new_size.x;
    typed_data->is_dirty = true;

    return true;
}

static void sui_panel_control_render_frame_prepare(standard_ui_state* state, struct sui_control* self, const struct frame_data* p_frame_data) {
    if (self) {
        sui_panel_internal_data* typed_data = self->internal_data;
        if (typed_data->is_dirty) {
            renderer_geometry_vertex_update(&typed_data->g, 0, typed_data->g.vertex_count, typed_data->g.vertices, true);
            typed_data->is_dirty = false;
        }
    }
}
