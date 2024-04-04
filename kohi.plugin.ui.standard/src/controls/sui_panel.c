#include "sui_panel.h"

#include <containers/darray.h>
#include <kstring.h>
#include <core/systems_manager.h>
#include <math/geometry_utils.h>
#include <math/kmath.h>
#include <renderer/renderer_frontend.h>
#include <resources/resource_types.h>
#include <systems/geometry_system.h>
#include <systems/shader_system.h>

static void sui_panel_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data);

b8 sui_panel_control_create(const char* name, vec2 size, vec4 colour, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
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

void sui_panel_control_destroy(struct sui_control* self) {
    sui_base_control_destroy(self);
}

b8 sui_panel_control_load(struct sui_control* self) {
    if (!sui_base_control_load(self)) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;

    // Generate UVs.
    f32 xmin, ymin, xmax, ymax;
    generate_uvs_from_image_coords(512, 512, 44, 7, &xmin, &ymin);
    generate_uvs_from_image_coords(512, 512, 73, 36, &xmax, &ymax);

    // Create a simple plane.
    geometry_config ui_config = {0};
    generate_quad_2d(self->name, typed_data->rect.width, typed_data->rect.height, xmin, xmax, ymin, ymax, &ui_config);
    // Get UI geometry from config. NOTE: this uploads to GPU
    typed_data->g = geometry_system_acquire_from_config(ui_config, true);

    standard_ui_state* typed_state = systems_manager_get_state(128); // HACK: need standard way to get extension types.

    // Acquire instance resources for this control.
    texture_map* maps[1] = {&typed_state->ui_atlas};
    shader* s = shader_system_get("Shader.StandardUI");
    u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    shader_instance_uniform_texture_config atlas_texture = {0};
    atlas_texture.uniform_location = atlas_location;
    atlas_texture.texture_map_count = 1;
    atlas_texture.texture_maps = maps;

    instance_resource_config.uniform_config_count = 1;
    instance_resource_config.uniform_configs = &atlas_texture;

    renderer_shader_instance_resources_acquire(s, &instance_resource_config, &typed_data->instance_id);

    return true;
}

void sui_panel_control_unload(struct sui_control* self) {
}

b8 sui_panel_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

b8 sui_panel_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(self, p_frame_data, render_data)) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;
    if (typed_data->g) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = typed_data->g->material;
        renderable.render_data.vertex_count = typed_data->g->vertex_count;
        renderable.render_data.vertex_element_size = typed_data->g->vertex_element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->g->vertex_buffer_offset;
        renderable.render_data.index_count = typed_data->g->index_count;
        renderable.render_data.index_element_size = typed_data->g->index_element_size;
        renderable.render_data.index_buffer_offset = typed_data->g->index_buffer_offset;
        renderable.render_data.model = xform_world_get(self->xform);
        renderable.render_data.diffuse_colour = typed_data->colour;

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}
vec2 sui_panel_size(struct sui_control* self) {
    if (!self) {
        return vec2_zero();
    }

    sui_panel_internal_data* typed_data = self->internal_data;
    return (vec2){typed_data->rect.width, typed_data->rect.height};
}

b8 sui_panel_control_resize(struct sui_control* self, vec2 new_size) {
    if (!self) {
        return false;
    }

    sui_panel_internal_data* typed_data = self->internal_data;

    typed_data->rect.width = new_size.x;
    typed_data->rect.height = new_size.y;
    vertex_2d* vertices = typed_data->g->vertices;
    vertices[1].position.y = new_size.y;
    vertices[1].position.x = new_size.x;
    vertices[2].position.y = new_size.y;
    vertices[3].position.x = new_size.x;
    typed_data->is_dirty = true;

    return true;
}

static void sui_panel_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data) {
    if (self) {
        sui_panel_internal_data* typed_data = self->internal_data;
        if (typed_data->is_dirty) {
            renderer_geometry_vertex_update(typed_data->g, 0, typed_data->g->vertex_count, typed_data->g->vertices, true);
            typed_data->is_dirty = false;
        }
    }
}
