#include "sui_button.h"

#include <containers/darray.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <core/logger.h>
#include <core/systems_manager.h>
#include <math/kmath.h>
#include <math/transform.h>
#include <renderer/renderer_frontend.h>
#include <systems/shader_system.h>

#include "standard_ui_system.h"

static void sui_button_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data);

b8 sui_button_control_create(const char* name, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
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

void sui_button_control_destroy(struct sui_control* self) {
    sui_base_control_destroy(self);
}

b8 sui_button_control_height_set(struct sui_control* self, i32 height) {
    if (!self) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    typed_data->size.y = height;
    typed_data->nslice.size.y = height;

    self->bounds.height = height;

    update_nine_slice(&typed_data->nslice, 0);

    return true;
}

b8 sui_button_control_load(struct sui_control* self) {
    if (!sui_base_control_load(self)) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    standard_ui_state* typed_state = systems_manager_get_state(K_SYSTEM_TYPE_STANDARD_UI_EXT);

    // HACK: TODO: remove hardcoded stuff.
    /* vec2i atlas_size = (vec2i){typed_state->ui_atlas.texture->width, typed_state->ui_atlas.texture->height}; */
    vec2i atlas_size = (vec2i){512, 512};
    vec2i atlas_min = (vec2i){151, 12};
    vec2i atlas_max = (vec2i){158, 19};
    vec2i corner_px_size = (vec2i){3, 3};
    vec2i corner_size = (vec2i){10, 10};
    if (!generate_nine_slice(self->name, typed_data->size, atlas_size, atlas_min, atlas_max, corner_px_size, corner_size, &typed_data->nslice)) {
        KERROR("Failed to generate nine slice.");
        return false;
    }

    self->bounds.x = 0.0f;
    self->bounds.y = 0.0f;
    self->bounds.width = typed_data->size.x;
    self->bounds.height = typed_data->size.y;

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

void sui_button_control_unload(struct sui_control* self) {
    //
}

b8 sui_button_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

static void sui_button_control_render_frame_prepare(struct sui_control* self, const struct frame_data* p_frame_data) {
    if (self) {
        sui_button_internal_data* internal_data = self->internal_data;
        nine_slice_render_frame_prepare(&internal_data->nslice, p_frame_data);
    }
}

b8 sui_button_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(self, p_frame_data, render_data)) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    if (typed_data->nslice.g) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = typed_data->nslice.g->material;
        renderable.render_data.vertex_count = typed_data->nslice.g->vertex_count;
        renderable.render_data.vertex_element_size = typed_data->nslice.g->vertex_element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->nslice.g->vertex_buffer_offset;
        renderable.render_data.index_count = typed_data->nslice.g->index_count;
        renderable.render_data.index_element_size = typed_data->nslice.g->index_element_size;
        renderable.render_data.index_buffer_offset = typed_data->nslice.g->index_buffer_offset;
        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = vec4_one();  // white. TODO: pull from object properties.

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}

void sui_button_on_mouse_out(struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        sui_button_internal_data* typed_data = self->internal_data;
        typed_data->nslice.atlas_px_min.x = 151;
        typed_data->nslice.atlas_px_min.y = 12;
        typed_data->nslice.atlas_px_max.x = 158;
        typed_data->nslice.atlas_px_max.y = 19;
        update_nine_slice(&typed_data->nslice, 0);
    }
}

void sui_button_on_mouse_over(struct sui_control* self, struct sui_mouse_event event) {
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
        update_nine_slice(&typed_data->nslice, 0);
    }
}
void sui_button_on_mouse_down(struct sui_control* self, struct sui_mouse_event event) {
    if (self) {
        sui_button_internal_data* typed_data = self->internal_data;
        typed_data->nslice.atlas_px_min.x = 151;
        typed_data->nslice.atlas_px_min.y = 21;
        typed_data->nslice.atlas_px_max.x = 158;
        typed_data->nslice.atlas_px_max.y = 28;
        update_nine_slice(&typed_data->nslice, 0);
    }
}
void sui_button_on_mouse_up(struct sui_control* self, struct sui_mouse_event event) {
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
        update_nine_slice(&typed_data->nslice, 0);
    }
}
