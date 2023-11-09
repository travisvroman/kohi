#include "standard_ui_system.h"

#include <containers/darray.h>
#include <core/event.h>
#include <core/identifier.h>
#include <core/input.h>
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

#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "systems/font_system.h"

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

static b8 standard_ui_system_mouse_down(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        mat4 model = transform_world_get(&control->xform);
        mat4 inv = mat4_inverse(model);
        vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
        if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
            control->is_pressed = true;
            // TODO: Can't assume buttons.
            sui_button_on_mouse_down(control, evt);
        }
    }
    return false;
}
static b8 standard_ui_system_mouse_up(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];

        control->is_pressed = false;

        mat4 model = transform_world_get(&control->xform);
        mat4 inv = mat4_inverse(model);
        vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
        if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
            // TODO: Can't assume buttons.
            sui_button_on_mouse_up(control, evt);
        }
    }
    return false;
}
static b8 standard_ui_system_click(u16 code, void* sender, void* listener_inst, event_context context) {
    standard_ui_state* typed_state = (standard_ui_state*)listener_inst;

    sui_mouse_event evt;
    evt.mouse_button = (buttons)context.data.i16[0];
    evt.x = context.data.i16[1];
    evt.y = context.data.i16[2];
    for (u32 i = 0; i < typed_state->active_control_count; ++i) {
        sui_control* control = typed_state->active_controls[i];
        if (control->on_click) {
            mat4 model = transform_world_get(&control->xform);
            mat4 inv = mat4_inverse(model);
            vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
            if (rect_2d_contains_point(control->bounds, (vec2){transformed_evt.x, transformed_evt.y})) {
                control->on_click(control, evt);
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
        mat4 model = transform_world_get(&control->xform);
        mat4 inv = mat4_inverse(model);
        vec3 transformed_evt = vec3_transform((vec3){evt.x, evt.y, 0.0f}, 1.0f, inv);
        vec2 transformed_vec2 = (vec2){transformed_evt.x, transformed_evt.y};
        if (rect_2d_contains_point(control->bounds, transformed_vec2)) {
            KTRACE("Button hover: %s", control->name);
            if (!control->is_hovered) {
                control->is_hovered = true;
                sui_button_on_mouse_over(control, evt);
                if (control->on_mouse_over) {
                    control->on_mouse_over(control, evt);
                }
            }
        } else {
            if (control->is_hovered) {
                control->is_hovered = false;
                sui_button_on_mouse_out(control, evt);
                if (control->on_mouse_out) {
                    control->on_mouse_out(control, evt);
                }
            }
        }
    }
    return false;
}

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

    // Listen for input events.
    event_register(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
    event_register(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
    event_register(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
    event_register(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

    KTRACE("Initialized standard UI system.");

    return true;
}

void standard_ui_system_shutdown(void* state) {
    if (state) {
        standard_ui_state* typed_state = (standard_ui_state*)state;

        // Stop listening for input events.
        event_unregister(EVENT_CODE_BUTTON_CLICKED, state, standard_ui_system_click);
        event_unregister(EVENT_CODE_MOUSE_MOVED, state, standard_ui_system_move);
        event_unregister(EVENT_CODE_BUTTON_PRESSED, state, standard_ui_system_mouse_down);
        event_unregister(EVENT_CODE_BUTTON_RELEASED, state, standard_ui_system_mouse_up);

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

    out_control->xform = transform_create();

    return true;
}
void sui_base_control_destroy(struct sui_control* self) {
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

b8 sui_panel_control_create(const char* name, vec2 size, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_panel_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_panel_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->rect = vec4_create(0, 0, size.x, size.y);
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
    generate_quad_2d(self->name, typed_data->rect.width, typed_data->rect.height, xmin, xmax, ymin, ymax, &ui_config);
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
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = typed_data->g->material;
        renderable.render_data.vertex_count = typed_data->g->vertex_count;
        renderable.render_data.vertex_element_size = typed_data->g->vertex_element_size;
        renderable.render_data.vertex_buffer_offset = typed_data->g->vertex_buffer_offset;
        renderable.render_data.index_count = typed_data->g->index_count;
        renderable.render_data.index_element_size = typed_data->g->index_element_size;
        renderable.render_data.index_buffer_offset = typed_data->g->index_buffer_offset;
        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = vec4_one();  // white. TODO: pull from object properties.

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
    renderer_geometry_vertex_update(typed_data->g, 0, typed_data->g->vertex_count, vertices);

    return true;
}

typedef struct sui_button_internal_data {
    vec2i size;
    vec4 colour;
    nine_slice nslice;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;
} sui_button_internal_data;

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
    out_control->render = sui_button_control_render;

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
    standard_ui_state* typed_state = systems_manager_get_state(128);  // HACK: need standard way to get extension types.

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
    renderer_shader_instance_resources_acquire(s, 1, maps, &typed_data->instance_id);

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

b8 sui_button_on_mouse_out(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        return false;
    }
    sui_button_internal_data* typed_data = self->internal_data;
    typed_data->nslice.atlas_px_min.x = 151;
    typed_data->nslice.atlas_px_min.y = 12;
    typed_data->nslice.atlas_px_max.x = 158;
    typed_data->nslice.atlas_px_max.y = 19;
    update_nine_slice(&typed_data->nslice, 0);

    return true;
}

b8 sui_button_on_mouse_over(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        return false;
    }

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

    return true;
}
b8 sui_button_on_mouse_down(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        return false;
    }

    sui_button_internal_data* typed_data = self->internal_data;
    typed_data->nslice.atlas_px_min.x = 151;
    typed_data->nslice.atlas_px_min.y = 21;
    typed_data->nslice.atlas_px_max.x = 158;
    typed_data->nslice.atlas_px_max.y = 28;
    update_nine_slice(&typed_data->nslice, 0);

    return true;
}
b8 sui_button_on_mouse_up(struct sui_control* self, struct sui_mouse_event event) {
    if (!self) {
        return false;
    }

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

    return true;
}

typedef struct sui_label_internal_data {
    vec2i size;
    vec4 colour;
    u32 instance_id;
    u64 frame_number;
    u8 draw_index;

    font_type type;
    struct font_data* data;
    u64 vertex_buffer_offset;
    u64 index_buffer_offset;
    char* text;
    u32 max_text_length;
    u32 cached_ut8_length;
} sui_label_internal_data;

static void regenerate_label_geometry(sui_control* self);

b8 sui_label_control_create(const char* name, font_type type, const char* font_name, u16 font_size, const char* text, struct sui_control* out_control) {
    if (!sui_base_control_create(name, out_control)) {
        return false;
    }

    out_control->internal_data_size = sizeof(sui_label_internal_data);
    out_control->internal_data = kallocate(out_control->internal_data_size, MEMORY_TAG_UI);
    sui_label_internal_data* typed_data = out_control->internal_data;

    // Reasonable defaults.
    typed_data->colour = vec4_one();

    // Assign function pointers.
    out_control->destroy = sui_label_control_destroy;
    out_control->load = sui_label_control_load;
    out_control->unload = sui_label_control_unload;
    out_control->update = sui_label_control_update;
    out_control->render = sui_label_control_render;

    out_control->name = string_duplicate(name);

    // Assign the type first
    typed_data->type = type;

    // Acquire the font of the correct type and assign its internal data.
    // This also gets the atlas texture.
    typed_data->data = font_system_acquire(font_name, font_size, typed_data->type);
    if (!typed_data->data) {
        KERROR("Unable to acquire font: '%s'. ui_text cannot be created.", font_name);
        return false;
    }

    typed_data->text = string_duplicate(text);

    typed_data->instance_id = INVALID_ID;
    typed_data->frame_number = INVALID_ID_U64;

    typed_data->vertex_buffer_offset = INVALID_ID_U64;
    typed_data->index_buffer_offset = INVALID_ID_U64;

    // Acquire resources for font texture map.
    // TODO: Should there be an override option for the shader?
    shader* ui_shader = shader_system_get("Shader.StandardUI");  // TODO: text shader.
    texture_map* font_maps[1] = {&typed_data->data->atlas};
    if (!renderer_shader_instance_resources_acquire(ui_shader, 1, font_maps, &typed_data->instance_id)) {
        KFATAL("Unable to acquire shader resources for font texture map.");
        return false;
    }

    // Verify atlas has the glyphs needed.
    if (!font_system_verify_atlas(typed_data->data, text)) {
        KERROR("Font atlas verification failed.");
        return false;
    }

    return true;
}

void sui_label_control_destroy(struct sui_control* self) {
    sui_base_control_destroy(self);
}

b8 sui_label_control_load(struct sui_control* self) {
    if (!sui_base_control_load(self)) {
        return false;
    }

    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->text && typed_data->text[0] != 0) {
        static const u64 quad_vertex_size = (sizeof(vertex_2d) * 4);
        static const u64 quad_index_size = (sizeof(u32) * 6);
        u64 text_length = string_utf8_length(typed_data->text);

        // Allocate space in the buffers.
        renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
        if (!renderer_renderbuffer_allocate(vertex_buffer, quad_vertex_size * text_length, &typed_data->vertex_buffer_offset)) {
            KERROR("sui_label_control_load failed to allocate from the renderer's vertex buffer!");
            return false;
        }

        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        if (!renderer_renderbuffer_allocate(index_buffer, quad_index_size * text_length, &typed_data->index_buffer_offset)) {
            KERROR("sui_label_control_load failed to allocate from the renderer's index buffer!");
            return false;
        }
    }
    // Generate geometry.
    regenerate_label_geometry(self);

    return true;
}

void sui_label_control_unload(struct sui_control* self) {
    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->text) {
        u32 text_length = string_length(typed_data->text);
        kfree(typed_data->text, sizeof(char) * text_length + 1, MEMORY_TAG_STRING);
        typed_data->text = 0;
    }

    // Free from the vertex buffer.
    renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    if (typed_data->max_text_length > 0) {
        renderer_renderbuffer_free(vertex_buffer, sizeof(vertex_2d) * 4 * typed_data->max_text_length, typed_data->vertex_buffer_offset);
    }

    // Free from the index buffer.
    if (typed_data->vertex_buffer_offset != INVALID_ID_U64) {
        static const u64 quad_index_size = (sizeof(u32) * 6);
        renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);
        if (typed_data->max_text_length > 0) {
            renderer_renderbuffer_free(index_buffer, quad_index_size * typed_data->max_text_length, typed_data->index_buffer_offset);
        }
        typed_data->vertex_buffer_offset = INVALID_ID_U64;
    }

    // Release resources for font texture map.
    if (typed_data->index_buffer_offset != INVALID_ID_U64) {
        shader* ui_shader = shader_system_get("Shader.StandardUI");  // TODO: text shader.
        if (!renderer_shader_instance_resources_release(ui_shader, typed_data->instance_id)) {
            KFATAL("Unable to release shader resources for font texture map.");
        }
        typed_data->index_buffer_offset = INVALID_ID_U64;
    }
}

b8 sui_label_control_update(struct sui_control* self, struct frame_data* p_frame_data) {
    if (!sui_base_control_update(self, p_frame_data)) {
        return false;
    }

    //

    return true;
}

b8 sui_label_control_render(struct sui_control* self, struct frame_data* p_frame_data, standard_ui_render_data* render_data) {
    if (!sui_base_control_render(self, p_frame_data, render_data)) {
        return false;
    }

    sui_label_internal_data* typed_data = self->internal_data;

    if (typed_data->cached_ut8_length) {
        standard_ui_renderable renderable = {0};
        renderable.render_data.unique_id = self->id.uniqueid;
        renderable.render_data.material = 0;
        renderable.render_data.vertex_count = typed_data->cached_ut8_length * 4;
        renderable.render_data.vertex_buffer_offset = typed_data->vertex_buffer_offset;
        renderable.render_data.vertex_element_size = sizeof(vertex_2d);
        renderable.render_data.index_count = typed_data->cached_ut8_length * 6;
        renderable.render_data.index_buffer_offset = typed_data->index_buffer_offset;
        renderable.render_data.index_element_size = sizeof(u32);

        // NOTE: Override the default UI atlas and use that of the loaded font instead.
        renderable.atlas_override = &typed_data->data->atlas;

        renderable.render_data.model = transform_world_get(&self->xform);
        renderable.render_data.diffuse_colour = vec4_one();  // white. TODO: pull from object properties.

        renderable.instance_id = &typed_data->instance_id;
        renderable.frame_number = &typed_data->frame_number;
        renderable.draw_index = &typed_data->draw_index;

        darray_push(render_data->renderables, renderable);
    }

    return true;
}

void sui_label_position_set(struct sui_control* self, vec3 position) {
    transform_position_set(&self->xform, position);
}

void sui_label_text_set(struct sui_control* self, const char* text) {
    if (self) {
        sui_label_internal_data* typed_data = self->internal_data;

        // If strings are already equal, don't do anything.
        if (strings_equal(text, typed_data->text)) {
            return;
        }

        if (typed_data->text) {
            u32 text_length = string_length(typed_data->text);
            kfree(typed_data->text, sizeof(char) * text_length + 1, MEMORY_TAG_STRING);
        }
        typed_data->text = string_duplicate(text);

        // Verify atlas has the glyphs needed.
        if (!font_system_verify_atlas(typed_data->data, text)) {
            KERROR("Font atlas verification failed.");
        }

        regenerate_label_geometry(self);
    }
}

const char* sui_label_text_get(struct sui_control* self) {
    if (self && self->internal_data) {
        sui_label_internal_data* typed_data = self->internal_data;
        return typed_data->text;
    }
    return 0;
}

static void regenerate_label_geometry(sui_control* self) {
    sui_label_internal_data* typed_data = self->internal_data;

    // Get the UTF-8 string length
    u32 text_length_utf8 = string_utf8_length(typed_data->text);
    typed_data->cached_ut8_length = text_length_utf8;
    // Also get the length in characters.
    u32 char_length = string_length(typed_data->text);

    b8 needs_realloc = text_length_utf8 > typed_data->max_text_length;

    // Don't try to regenerate geometry for something that doesn't have any text.
    if (text_length_utf8 < 1) {
        return;
    }

    // Calculate buffer sizes.
    static const u64 verts_per_quad = 4;
    static const u8 indices_per_quad = 6;
    u64 prev_vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * typed_data->max_text_length;
    u64 prev_index_buffer_size = sizeof(u32) * indices_per_quad * typed_data->max_text_length;
    u64 vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * text_length_utf8;
    u64 index_buffer_size = sizeof(u32) * indices_per_quad * text_length_utf8;

    renderbuffer* vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    renderbuffer* index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    if (needs_realloc) {
        // Realloc from the vertex buffer.
        if (typed_data->max_text_length > 0) {
            if (!renderer_renderbuffer_free(vertex_buffer, prev_vertex_buffer_size, typed_data->vertex_buffer_offset)) {
                KERROR("Failed to free from renderer vertex buffer: size=%u, offset=%u", vertex_buffer_size, typed_data->vertex_buffer_offset);
            }
        }
        if (!renderer_renderbuffer_allocate(vertex_buffer, vertex_buffer_size, &typed_data->vertex_buffer_offset)) {
            KERROR("regenerate_label_geometry failed to allocate from the renderer's vertex buffer!");
            return;
        }

        // Realloc from the index buffer.
        if (typed_data->max_text_length > 0) {
            if (!renderer_renderbuffer_free(index_buffer, prev_index_buffer_size, typed_data->index_buffer_offset)) {
                KERROR("Failed to free from renderer index buffer: size=%u, offset=%u", index_buffer_size, typed_data->index_buffer_offset);
            }
        }
        if (!renderer_renderbuffer_allocate(index_buffer, index_buffer_size, &typed_data->index_buffer_offset)) {
            KERROR("regenerate_label_geometry failed to allocate from the renderer's index buffer!");
            return;
        }
    }

    // Update the max length if the string is now longer.
    if (text_length_utf8 > typed_data->max_text_length) {
        typed_data->max_text_length = text_length_utf8;
    }

    // Generate new geometry for each character.
    f32 x = 0;
    f32 y = 0;
    // Temp arrays to hold vertex/index data.
    vertex_2d* vertex_buffer_data = kallocate(vertex_buffer_size, MEMORY_TAG_ARRAY);
    u32* index_buffer_data = kallocate(index_buffer_size, MEMORY_TAG_ARRAY);

    // Take the length in chars and get the correct codepoint from it.
    for (u32 c = 0, uc = 0; c < char_length; ++c) {
        i32 codepoint = typed_data->text[c];

        // Continue to next line for newline.
        if (codepoint == '\n') {
            x = 0;
            y += typed_data->data->line_height;
            // Increment utf-8 character count.
            uc++;
            continue;
        }

        if (codepoint == '\t') {
            x += typed_data->data->tab_x_advance;
            uc++;
            continue;
        }

        // NOTE: UTF-8 codepoint handling.
        u8 advance = 0;
        if (!bytes_to_codepoint(typed_data->text, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        font_glyph* g = 0;
        for (u32 i = 0; i < typed_data->data->glyph_count; ++i) {
            if (typed_data->data->glyphs[i].codepoint == codepoint) {
                g = &typed_data->data->glyphs[i];
                break;
            }
        }

        if (!g) {
            // If not found, use the codepoint -1
            codepoint = -1;
            for (u32 i = 0; i < typed_data->data->glyph_count; ++i) {
                if (typed_data->data->glyphs[i].codepoint == codepoint) {
                    g = &typed_data->data->glyphs[i];
                    break;
                }
            }
        }

        if (g) {
            // Found the glyph. generate points.
            f32 minx = x + g->x_offset;
            f32 miny = y + g->y_offset;
            f32 maxx = minx + g->width;
            f32 maxy = miny + g->height;
            f32 tminx = (f32)g->x / typed_data->data->atlas_size_x;
            f32 tmaxx = (f32)(g->x + g->width) / typed_data->data->atlas_size_x;
            f32 tminy = (f32)g->y / typed_data->data->atlas_size_y;
            f32 tmaxy = (f32)(g->y + g->height) / typed_data->data->atlas_size_y;
            // Flip the y axis for system text
            if (typed_data->type == FONT_TYPE_SYSTEM) {
                tminy = 1.0f - tminy;
                tmaxy = 1.0f - tmaxy;
            }

            vertex_2d p0 = (vertex_2d){vec2_create(minx, miny), vec2_create(tminx, tminy)};
            vertex_2d p1 = (vertex_2d){vec2_create(maxx, miny), vec2_create(tmaxx, tminy)};
            vertex_2d p2 = (vertex_2d){vec2_create(maxx, maxy), vec2_create(tmaxx, tmaxy)};
            vertex_2d p3 = (vertex_2d){vec2_create(minx, maxy), vec2_create(tminx, tmaxy)};

            vertex_buffer_data[(uc * 4) + 0] = p0;  // 0    3
            vertex_buffer_data[(uc * 4) + 1] = p2;  //
            vertex_buffer_data[(uc * 4) + 2] = p3;  //
            vertex_buffer_data[(uc * 4) + 3] = p1;  // 2    1

            // Try to find kerning
            i32 kerning = 0;

            // Get the offset of the next character. If there is no advance, move forward one,
            // otherwise use advance as-is.
            u32 offset = c + advance;  //(advance < 1 ? 1 : advance);
            if (offset < text_length_utf8 - 1) {
                // Get the next codepoint.
                i32 next_codepoint = 0;
                u8 advance_next = 0;

                if (!bytes_to_codepoint(typed_data->text, offset, &next_codepoint, &advance_next)) {
                    KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
                    codepoint = -1;
                } else {
                    for (u32 i = 0; i < typed_data->data->kerning_count; ++i) {
                        font_kerning* k = &typed_data->data->kernings[i];
                        if (k->codepoint_0 == codepoint && k->codepoint_1 == next_codepoint) {
                            kerning = k->amount;
                        }
                    }
                }
            }
            x += g->x_advance + kerning;

        } else {
            KERROR("Unable to find unknown codepoint. Skipping.");
            // Increment utf-8 character count.
            uc++;
            continue;
        }

        // Index data 210301
        index_buffer_data[(uc * 6) + 0] = (uc * 4) + 2;
        index_buffer_data[(uc * 6) + 1] = (uc * 4) + 1;
        index_buffer_data[(uc * 6) + 2] = (uc * 4) + 0;
        index_buffer_data[(uc * 6) + 3] = (uc * 4) + 3;
        index_buffer_data[(uc * 6) + 4] = (uc * 4) + 0;
        index_buffer_data[(uc * 6) + 5] = (uc * 4) + 1;

        // Now advance c
        c += advance - 1;  // Subtracting 1 because the loop always increments once for single-byte anyway.
        // Increment utf-8 character count.
        uc++;
    }

    // Load up the data.
    b8 vertex_load_result = renderer_renderbuffer_load_range(vertex_buffer, typed_data->vertex_buffer_offset, vertex_buffer_size, vertex_buffer_data);
    b8 index_load_result = renderer_renderbuffer_load_range(index_buffer, typed_data->index_buffer_offset, index_buffer_size, index_buffer_data);

    // Clean up.
    kfree(vertex_buffer_data, vertex_buffer_size, MEMORY_TAG_ARRAY);
    kfree(index_buffer_data, index_buffer_size, MEMORY_TAG_ARRAY);

    // Verify results.
    if (!vertex_load_result) {
        KERROR("regenerate_geometry failed to load data into vertex buffer range.");
    }
    if (!index_load_result) {
        KERROR("regenerate_geometry failed to load data into index buffer range.");
    }
}
