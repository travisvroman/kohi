#include "render_view_wireframe.h"

#include <containers/darray.h>
#include <core/event.h>
#include <core/frame_data.h>
#include <core/kmemory.h>
#include <core/logger.h>
#include <defines.h>
#include <math/kmath.h>
#include <renderer/camera.h>
#include <renderer/renderer_frontend.h>
#include <renderer/renderer_types.h>
#include <renderer/viewport.h>
#include <systems/resource_system.h>
#include <systems/shader_system.h>

typedef struct wireframe_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
    u16 colour;
} wireframe_shader_locations;

typedef struct wireframe_colour_instance {
    u32 id;
    u64 frame_number;
    u8 draw_index;
    vec4 colour;
} wireframe_colour_instance;

typedef struct wireframe_shader_info {
    shader* s;
    wireframe_shader_locations locations;
    // One instance per colour drawn.
    wireframe_colour_instance normal_instance;
    wireframe_colour_instance selected_instance;
} wireframe_shader_info;

typedef struct render_view_wireframe_internal_data {
    u32 selected_id;

    wireframe_shader_info mesh_shader;
    wireframe_shader_info terrain_shader;
} render_view_wireframe_internal_data;

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }
    render_view_wireframe_internal_data* data = (render_view_wireframe_internal_data*)self->internal_data;
    if (!data) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            /* render_view_system_render_targets_regenerate(self); */
            // This needs to be consumed by other views, so consider this as _not_ handled.
            return false;
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}

b8 render_view_wireframe_on_registered(struct render_view* self) {
    if (!self) {
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(render_view_wireframe_internal_data), MEMORY_TAG_RENDERER);
    render_view_wireframe_internal_data* data = self->internal_data;
    data->selected_id = INVALID_ID;

    const char* shader_names[2] = {"Shader.Builtin.Wireframe", "Shader.Builtin.WireframeTerrain"};
    wireframe_shader_info* shader_infos[2] = {&data->mesh_shader, &data->terrain_shader};
    vec4 normal_colours[2] = {vec4_create(0.5f, 0.8f, 0.8f, 1.0f), vec4_create(0.8f, 0.8f, 0.5f, 1.0f)};

    for (u32 s = 0; s < 2; ++s) {
        wireframe_shader_info* info = shader_infos[s];
        // Load the wireframe shader and its locations.
        resource wireframe_shader_config_resource;
        if (!resource_system_load(shader_names[s], RESOURCE_TYPE_SHADER, 0, &wireframe_shader_config_resource)) {
            KERROR("Failed to load builtin wireframe shader.");
            return false;
        }
        shader_config* wireframe_shader_config = wireframe_shader_config_resource.data;
        if (!shader_system_create(&self->passes[0], wireframe_shader_config)) {
            KERROR("Failed to load builtin wireframe shader.");
            return false;
        }
        resource_system_unload(&wireframe_shader_config_resource);

        info->s = shader_system_get(shader_names[s]);
        info->locations.projection = shader_system_uniform_location(info->s, "projection");
        info->locations.view = shader_system_uniform_location(info->s, "view");
        info->locations.model = shader_system_uniform_location(info->s, "model");
        info->locations.colour = shader_system_uniform_location(info->s, "colour");

        // Acquire shader instance resources.
        info->normal_instance = (wireframe_colour_instance){0};
        info->normal_instance.colour = normal_colours[s];

        shader_instance_resource_config instance_resource_config = {0};
        instance_resource_config.uniform_config_count = 0;  // NOTE: no textures, so this doesn't matter.
        instance_resource_config.uniform_configs = 0;

        if (!renderer_shader_instance_resources_acquire(info->s, &instance_resource_config, &info->normal_instance.id)) {
            KERROR("Unable to acquire geometry shader instance resources from wireframe shader.");
            return false;
        }

        info->selected_instance = (wireframe_colour_instance){0};
        info->selected_instance.colour = vec4_create(0.0f, 1.0f, 0.0f, 1.0f);
        if (!renderer_shader_instance_resources_acquire(info->s, &instance_resource_config, &info->selected_instance.id)) {
            KERROR("Unable to acquire selected shader instance resources from wireframe shader.");
            return false;
        }
    }

    // Register for events.
    if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
        KERROR("Unable to listen for refresh required event. Creation failed.");
        return false;
    }

    return true;
}

void render_view_wireframe_on_destroy(struct render_view* self) {
    if (!self) {
        return;
    }

    render_view_wireframe_internal_data* internal_data = self->internal_data;

    // Unregister from the event.
    event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

    // Release shader instance resources.
    renderer_shader_instance_resources_release(internal_data->mesh_shader.s, internal_data->mesh_shader.normal_instance.id);
    renderer_shader_instance_resources_release(internal_data->mesh_shader.s, internal_data->mesh_shader.selected_instance.id);
    renderer_shader_instance_resources_release(internal_data->terrain_shader.s, internal_data->terrain_shader.normal_instance.id);
    renderer_shader_instance_resources_release(internal_data->terrain_shader.s, internal_data->terrain_shader.selected_instance.id);

    // Free up the internal data structure.
    kfree(self->internal_data, sizeof(render_view_wireframe_internal_data), MEMORY_TAG_RENDERER);
    self->internal_data = 0;
}

void render_view_wireframe_on_resize(struct render_view* self, u32 width, u32 height) {
    if (self) {
        if (width != self->width || height != self->height) {
            self->width = width;
            self->height = height;
        }
    }
}

b8 render_view_wireframe_on_packet_build(const struct render_view* self, struct frame_data* p_frame_data, struct viewport* v, struct camera* c, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_wireframe_on_packet_build requires a valid pointer to view, packet and data.");
        return false;
    }

    render_view_wireframe_data* world_data = data;
    render_view_wireframe_internal_data* internal_data = self->internal_data;

    out_packet->geometries = 0;
    out_packet->view = self;
    out_packet->vp = v;

    // Set matrices, etc.
    out_packet->projection_matrix = v->projection;
    out_packet->view_matrix = camera_view_get(c);
    out_packet->view_position = camera_position_get(c);

    // Take note of the currently selected object.
    internal_data->selected_id = world_data->selected_id;

    wireframe_shader_info* infos[2] = {&internal_data->mesh_shader, &internal_data->terrain_shader};
    geometry_render_data* source_arrays[2] = {world_data->world_geometries, world_data->terrain_geometries};
    u32* counts[2] = {&out_packet->geometry_count, &out_packet->terrain_geometry_count};
    geometry_render_data** target_arrays[2] = {&out_packet->geometries, &out_packet->terrain_geometries};

    for (u32 s = 0; s < 2; ++s) {
        // Reset draw indices.
        infos[s]->normal_instance.draw_index = 0;
        infos[s]->selected_instance.draw_index = 0;

        geometry_render_data* source_array = source_arrays[s];
        if (source_array) {
            u32 geometry_data_count = darray_length(source_array);
            *counts[s] = geometry_data_count;
            if (geometry_data_count) {
                // For this view, render everything provided.
                *target_arrays[s] = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_data_count);
                for (u32 i = 0; i < geometry_data_count; ++i) {
                    geometry_render_data* render_data = &((*target_arrays[s])[i]);
                    geometry_render_data* source_data = &source_array[i];
                    render_data->unique_id = source_data->unique_id;
                    render_data->material = source_data->material;
                    render_data->vertex_count = source_data->vertex_count;
                    render_data->vertex_buffer_offset = source_data->vertex_buffer_offset;
                    render_data->index_count = source_data->index_count;
                    render_data->index_buffer_offset = source_data->index_buffer_offset;
                    render_data->model = source_array[i].model;
                    render_data->winding_inverted = source_array[i].winding_inverted;
                }
            }
        }
    }

    return true;
}

void render_view_wireframe_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet) {
    if (self) {
        // Nothing to do.
    }
}
b8 render_view_wireframe_on_render(const struct render_view* self, const struct render_view_packet* packet, struct frame_data* p_frame_data) {
    if (self) {
        render_view_wireframe_internal_data* internal_data = self->internal_data;

        // Bind the viewport
        renderer_active_viewport_set(packet->vp);

        // NOTE: the first renderpass is the only one.
        renderpass* pass = &self->passes[0];

        if (!renderer_renderpass_begin(pass, &pass->targets[p_frame_data->render_target_index])) {
            KERROR("render_view_wireframe_on_render render pass failed to start.");
            return false;
        }

        wireframe_shader_info* infos[2] = {&internal_data->mesh_shader, &internal_data->terrain_shader};
        u32 counts[2] = {packet->geometry_count, packet->terrain_geometry_count};
        geometry_render_data* arrays[2] = {packet->geometries, packet->terrain_geometries};

        for (u32 s = 0; s < 2; ++s) {
            wireframe_shader_info* info = infos[s];
            geometry_render_data* array = arrays[s];

            shader_system_use_by_id(info->s->id);

            // Set global uniforms
            renderer_shader_bind_globals(info->s);
            if (!shader_system_uniform_set_by_location(info->locations.projection, &packet->projection_matrix)) {
                KERROR("Failed to set projection matrix uniform on wireframe shader.");
                return false;
            }
            if (!shader_system_uniform_set_by_location(info->locations.view, &packet->view_matrix)) {
                KERROR("Failed to set view matrix uniform on wireframe shader.");
                return false;
            }
            shader_system_apply_global(true);

            if (array) {
                for (u32 i = 0; i < counts[s]; ++i) {
                    // Set instance uniforms.

                    // Selecting the instance allows easy colour changing.
                    wireframe_colour_instance* inst = 0;
                    if (array[i].unique_id == internal_data->selected_id) {
                        inst = &info->selected_instance;
                    } else {
                        inst = &info->normal_instance;
                    }

                    shader_system_bind_instance(inst->id);

                    b8 needs_update = inst->frame_number != p_frame_data->renderer_frame_number || inst->draw_index != p_frame_data->draw_index;
                    if (needs_update) {
                        if (!shader_system_uniform_set_by_location(info->locations.colour, &inst->colour)) {
                            KERROR("Unable to set uniform colour for wireframe shader.");
                            return false;
                        }
                    }

                    shader_system_apply_instance(needs_update);

                    // Sync frame number and draw index.
                    inst->frame_number = p_frame_data->renderer_frame_number;
                    inst->draw_index = p_frame_data->draw_index;

                    // Locals.
                    if (!shader_system_uniform_set_by_location(info->locations.model, &array[i].model)) {
                        KERROR("Failed to apply model matrix uniform for wireframe shader.");
                        return false;
                    }

                    // Draw it.
                    renderer_geometry_draw(&array[i]);
                }
            }
        }

        // TODO: terrains.

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_wireframe_on_render Failed to end renderpass.");
            return false;
        }
    }

    return true;
}
