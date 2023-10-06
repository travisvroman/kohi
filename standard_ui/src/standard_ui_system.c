#include "standard_ui_system.h"

#include <containers/darray.h>
#include <core/identifier.h>
#include <core/kmemory.h>
#include <core/kstring.h>
#include <core/logger.h>
#include <core/systems_manager.h>
#include <defines.h>
#include <math/geometry_utils.h>
#include <math/kmath.h>
#include <math/transform.h>
#include <renderer/renderer_frontend.h>
#include <resources/resource_types.h>
#include <systems/geometry_system.h>
#include <systems/shader_system.h>
#include <systems/texture_system.h>

#include "renderer/renderer_types.h"

typedef struct standard_ui_state {
    standard_ui_system_config config;
    // Array of pointers to controls, the system does not own these. The application does.
    u32 total_control_count;
    u32 active_control_count;
    sui_control** active_controls;
    u32 inactive_control_count;
    sui_control** inactive_controls;
    sui_control root;
    texture_map ui_atlas;

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

    sui_base_control_create("__ROOT__", &typed_state->root);

    texture* atlas = texture_system_acquire("StandardUIAtlas", true);
    if (!atlas) {
        KWARN("Unable to load atlas texture, using default.");
        atlas = texture_system_get_default_texture();
    }

    // Setup the texture map.
    texture_map* map = &typed_state->ui_atlas;
    map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    map->filter_minify = map->filter_magnify = TEXTURE_FILTER_MODE_NEAREST;
    map->texture = atlas;
    if (!renderer_texture_map_resources_acquire(map)) {
        KERROR("Unable to acquire texture map resources. StandardUI cannot be initialized.");
        return false;
    }

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

        if (typed_state->ui_atlas.texture) {
            texture_system_release(typed_state->ui_atlas.texture->name);
            typed_state->ui_atlas.texture = 0;
        }

        renderer_texture_map_resources_release(&typed_state->ui_atlas);
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

b8 standard_ui_system_render(void* state, sui_control* root, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!state) {
        return false;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;

    render_data->ui_atlas = &typed_state->ui_atlas;

    if (!root) {
        root = &typed_state->root;
    }

    if (root->render) {
        if (!root->render(root, p_frame_data, render_data)) {
            KERROR("Root element failed to render. See logs for more details");
            return false;
        }
    }

    if (root->children) {
        u32 length = darray_length(root->children);
        for (u32 i = 0; i < length; ++i) {
            sui_control* c = root->children[i];
            if (!standard_ui_system_render(state, c, p_frame_data, render_data)) {
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

b8 standard_ui_system_register_control(void* state, sui_control* control) {
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

b8 standard_ui_system_control_add_child(void* state, sui_control* parent, sui_control* child) {
    if (!child) {
        return false;
    }

    standard_ui_state* typed_state = (standard_ui_state*)state;
    if (!parent) {
        parent = &typed_state->root;
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

    transform_parent_set(&child->xform, &parent->xform);

    return true;
}

b8 standard_ui_system_control_remove_child(void* state, sui_control* parent, sui_control* child) {
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

            transform_parent_set(&child->xform, 0);
            return true;
        }
    }

    KERROR("Unable to remove child which is not a child of given parent.");
    return false;
}

b8 sui_base_control_create(const char* name, struct sui_control* out_control) {
    if (!out_control) {
        return false;
    }

    // Assign function pointers.
    out_control->destroy = sui_base_control_destroy;
    out_control->load = sui_base_control_load;
    out_control->unload = sui_base_control_unload;
    out_control->update = sui_base_control_update;
    out_control->render = sui_base_control_render;

    out_control->name = string_duplicate(name);
    out_control->unique_id = identifier_aquire_new_id(out_control);

    out_control->xform = transform_create();

    return true;
}
void sui_base_control_destroy(struct sui_control* self) {
    if (!self) {
        // TODO: recurse children/unparent?
        identifier_release_id(self->unique_id);

        if (self->internal_data && self->internal_data_size) {
            kfree(self->internal_data, self->internal_data_size, MEMORY_TAG_UI);
        }
        if (self->name) {
            string_free(self->name);
        }
        kzero_memory(self, sizeof(sui_control));
    }
}

b8 sui_base_control_load(struct sui_control* self) {
    if (!self) {
        return false;
    }

    return true;
}
void sui_base_control_unload(struct sui_control* self) {
    if (!self) {
        //
    }
}

b8 sui_base_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }

    return true;
}
b8 sui_base_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!self) {
        return false;
    }

    return true;
}

typedef struct sui_panel_internal_data {
    vec4 rect;
    vec4 colour;
    geometry* g;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;
} sui_panel_internal_data;

b8 sui_panel_control_create(const char* name, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_panel_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_panel_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->rect = vec4_create(0, 0, 10, 10);
    typed_data->colour = vec4_one();

    // Assign function pointers.
    out_control->destroy = sui_panel_control_destroy;
    out_control->load = sui_panel_control_load;
    out_control->unload = sui_panel_control_unload;
    out_control->update = sui_panel_control_update;
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
    generate_quad_2d(self->name, 512.0f, 512.0f, xmin, xmax, ymin, ymax, &ui_config);
    // Get UI geometry from config. NOTE: this uploads to GPU
    typed_data->g = geometry_system_acquire_from_config(ui_config, true);

    standard_ui_state* typed_state = systems_manager_get_state(128);  // HACK: need standard way to get extension types.

    // Acquire instance resources for this control.
    texture_map* maps[1] = {&typed_state->ui_atlas};
    shader* s = shader_system_get("Shader.StandardUI");
    renderer_shader_instance_resources_acquire(s, 1, maps, &typed_data->instance_id);

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
        renderable.render_data.unique_id = self->unique_id;
        renderable.render_data.geometry = typed_data->g;
        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = vec4_one();  // white. TODO: pull from object properties.

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}
