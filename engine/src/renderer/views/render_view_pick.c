#include "render_view_pick.h"

#include "containers/darray.h"
#include "core/event.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "core/uuid.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "memory/linear_allocator.h"
#include "renderer/renderer_frontend.h"
#include "resources/ui_text.h"
#include "systems/camera_system.h"
#include "systems/render_view_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"

typedef struct render_view_pick_shader_info {
    shader* s;
    renderpass* pass;
    u16 id_colour_location;
    u16 model_location;
    u16 projection_location;
    u16 view_location;
    mat4 projection;
    mat4 view;
    f32 near_clip;
    f32 far_clip;
    f32 fov;
} render_view_pick_shader_info;

typedef struct render_view_pick_internal_data {
    render_view_pick_shader_info ui_shader_info;
    render_view_pick_shader_info world_shader_info;
    render_view_pick_shader_info terrain_shader_info;

    // Used as the colour attachment for both renderpasses.
    texture colour_target_attachment_texture;
    // The depth attachment.
    texture depth_target_attachment_texture;

    i32 instance_count;
    b8* instance_updated;

    i16 mouse_x, mouse_y;
    // u32 render_mode;
} render_view_pick_internal_data;

static b8 on_mouse_moved(u16 code, void* sender, void* listener_inst, event_context event_data) {
    if (code == EVENT_CODE_MOUSE_MOVED) {
        render_view* self = (render_view*)listener_inst;
        render_view_pick_internal_data* data = (render_view_pick_internal_data*)self->internal_data;

        // Update position and regenerate the projection matrix.
        i16 y = event_data.data.i16[1];
        i16 x = event_data.data.i16[0];

        data->mouse_x = x;
        data->mouse_y = y;

        return true;
    }
    return false;
}

static b8 render_view_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    render_view* self = (render_view*)listener_inst;
    if (!self) {
        return false;
    }

    switch (code) {
        case EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED:
            render_view_system_render_targets_regenerate(self);
            // This needs to be consumed by other views, so consider it _not_ handled.
            return false;
    }

    return false;
}

static void acquire_shader_instances(const struct render_view* self) {
    render_view_pick_internal_data* data = self->internal_data;

    // Not saving the instance id because it doesn't matter.
    u32 instance;
    // UI shader
    if (!renderer_shader_instance_resources_acquire(data->ui_shader_info.s, 0, 0, &instance)) {
        KFATAL("render_view_pick failed to acquire UI shader resources.");
        return;
    }
    // World shader
    if (!renderer_shader_instance_resources_acquire(data->world_shader_info.s, 0, 0, &instance)) {
        KFATAL("render_view_pick failed to acquire World shader resources.");
        return;
    }
    // Terrain shader
    if (!renderer_shader_instance_resources_acquire(data->terrain_shader_info.s, 0, 0, &instance)) {
        KFATAL("render_view_pick failed to acquire Terrain shader resources.");
        return;
    }
    data->instance_count++;
    darray_push(data->instance_updated, false);
}

void release_shader_instances(const struct render_view* self) {
    render_view_pick_internal_data* data = self->internal_data;

    for (i32 i = 0; i < data->instance_count; ++i) {
        // UI shader
        if (!renderer_shader_instance_resources_release(data->ui_shader_info.s, i)) {
            KWARN("Failed to release UI shader resources.");
        }

        // World shader
        if (!renderer_shader_instance_resources_release(data->world_shader_info.s, i)) {
            KWARN("Failed to release world shader resources.");
        }

        // Terrain shader
        if (!renderer_shader_instance_resources_release(data->terrain_shader_info.s, i)) {
            KWARN("Failed to release terrain shader resources.");
        }
    }
    darray_destroy(data->instance_updated);
}

b8 render_view_pick_on_create(struct render_view* self) {
    if (self) {
        self->internal_data = kallocate(sizeof(render_view_pick_internal_data), MEMORY_TAG_RENDERER);
        render_view_pick_internal_data* data = self->internal_data;

        data->instance_updated = darray_create(b8);

        // NOTE: In this heavily-customized view, the exact number of passes is known, so
        // these index assumptions are fine.
        data->world_shader_info.pass = &self->passes[0];
        data->terrain_shader_info.pass = &self->passes[0];
        data->ui_shader_info.pass = &self->passes[1];

        // Builtin UI Pick shader.
        const char* ui_shader_name = "Shader.Builtin.UIPick";
        resource config_resource;
        if (!resource_system_load(ui_shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin UI Pick shader.");
            return false;
        }
        shader_config* config = (shader_config*)config_resource.data;
        if (!shader_system_create(data->ui_shader_info.pass, config)) {
            KERROR("Failed to load builtin UI Pick shader.");
            return false;
        }
        resource_system_unload(&config_resource);
        data->ui_shader_info.s = shader_system_get(ui_shader_name);

        // Extract uniform locations
        data->ui_shader_info.id_colour_location = shader_system_uniform_index(data->ui_shader_info.s, "id_colour");
        data->ui_shader_info.model_location = shader_system_uniform_index(data->ui_shader_info.s, "model");
        data->ui_shader_info.projection_location = shader_system_uniform_index(data->ui_shader_info.s, "projection");
        data->ui_shader_info.view_location = shader_system_uniform_index(data->ui_shader_info.s, "view");

        // Default UI properties
        data->ui_shader_info.near_clip = -100.0f;
        data->ui_shader_info.far_clip = 100.0f;
        data->ui_shader_info.fov = 0;
        data->ui_shader_info.projection = mat4_orthographic(0.0f, 1280.0f, 720.0f, 0.0f, data->ui_shader_info.near_clip, data->ui_shader_info.far_clip);
        data->ui_shader_info.view = mat4_identity();

        // Builtin World Pick shader.
        const char* world_shader_name = "Shader.Builtin.WorldPick";
        if (!resource_system_load(world_shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin World Pick shader.");
            return false;
        }
        config = (shader_config*)config_resource.data;
        if (!shader_system_create(data->world_shader_info.pass, config)) {
            KERROR("Failed to load builtin World Pick shader.");
            return false;
        }
        resource_system_unload(&config_resource);
        data->world_shader_info.s = shader_system_get(world_shader_name);

        // Extract uniform locations.
        data->world_shader_info.id_colour_location = shader_system_uniform_index(data->world_shader_info.s, "id_colour");
        data->world_shader_info.model_location = shader_system_uniform_index(data->world_shader_info.s, "model");
        data->world_shader_info.projection_location = shader_system_uniform_index(data->world_shader_info.s, "projection");
        data->world_shader_info.view_location = shader_system_uniform_index(data->world_shader_info.s, "view");

        // Default World properties
        data->world_shader_info.near_clip = 0.1f;
        data->world_shader_info.far_clip = 4000.0f;
        data->world_shader_info.fov = deg_to_rad(45.0f);
        data->world_shader_info.projection = mat4_perspective(data->world_shader_info.fov, 1280 / 720.0f, data->world_shader_info.near_clip, data->world_shader_info.far_clip);
        data->world_shader_info.view = mat4_identity();

        // Builtin Terrain Pick shader.
        const char* terrain_shader_name = "Shader.Builtin.TerrainPick";
        if (!resource_system_load(terrain_shader_name, RESOURCE_TYPE_SHADER, 0, &config_resource)) {
            KERROR("Failed to load builtin Terrain Pick shader.");
            return false;
        }
        config = (shader_config*)config_resource.data;
        if (!shader_system_create(data->terrain_shader_info.pass, config)) {
            KERROR("Failed to load builtin Terrain Pick shader.");
            return false;
        }
        resource_system_unload(&config_resource);
        data->terrain_shader_info.s = shader_system_get(terrain_shader_name);

        // Extract uniform locations.
        data->terrain_shader_info.id_colour_location = shader_system_uniform_index(data->terrain_shader_info.s, "id_colour");
        data->terrain_shader_info.model_location = shader_system_uniform_index(data->terrain_shader_info.s, "model");
        data->terrain_shader_info.projection_location = shader_system_uniform_index(data->terrain_shader_info.s, "projection");
        data->terrain_shader_info.view_location = shader_system_uniform_index(data->terrain_shader_info.s, "view");

        // Default terrain properties.
        data->terrain_shader_info.near_clip = 0.1f;
        data->terrain_shader_info.far_clip = 4000.0f;
        data->terrain_shader_info.fov = deg_to_rad(45.0f);
        data->terrain_shader_info.projection = mat4_perspective(data->terrain_shader_info.fov, 1280 / 720.0f, data->terrain_shader_info.near_clip, data->terrain_shader_info.far_clip);
        data->terrain_shader_info.view = mat4_identity();

        data->instance_count = 0;

        kzero_memory(&data->colour_target_attachment_texture, sizeof(texture));
        kzero_memory(&data->depth_target_attachment_texture, sizeof(texture));

        // Register for mouse move event.
        if (!event_register(EVENT_CODE_MOUSE_MOVED, self, on_mouse_moved)) {
            KERROR("Unable to listen for mouse move event, creation failed.");
            return false;
        }

        if (!event_register(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event)) {
            KERROR("Unable to listen for refresh required event, creation failed.");
            return false;
        }

        return true;
    }
    KERROR("render_view_pick_on_create - Requires a valid pointer to a view.");
    return false;
}

void render_view_pick_on_destroy(struct render_view* self) {
    if (self && self->internal_data) {
        render_view_pick_internal_data* data = self->internal_data;

        // Unregister from the events.
        event_unregister(EVENT_CODE_MOUSE_MOVED, self, on_mouse_moved);
        event_unregister(EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED, self, render_view_on_event);

        release_shader_instances(self);

        renderer_texture_destroy(&data->colour_target_attachment_texture);
        renderer_texture_destroy(&data->depth_target_attachment_texture);

        kfree(self->internal_data, sizeof(render_view_pick_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}

void render_view_pick_on_resize(struct render_view* self, u32 width, u32 height) {
    render_view_pick_internal_data* data = self->internal_data;

    self->width = width;
    self->height = height;

    // UI
    data->ui_shader_info.projection = mat4_orthographic(0.0f, (f32)width, (f32)height, 0.0f, data->ui_shader_info.near_clip, data->ui_shader_info.far_clip);

    // World
    f32 aspect = (f32)self->width / self->height;
    data->world_shader_info.projection = mat4_perspective(data->world_shader_info.fov, aspect, data->world_shader_info.near_clip, data->world_shader_info.far_clip);

    // Terrain
    data->terrain_shader_info.projection = mat4_perspective(data->terrain_shader_info.fov, aspect, data->terrain_shader_info.near_clip, data->terrain_shader_info.far_clip);

    for (u32 i = 0; i < self->renderpass_count; ++i) {
        self->passes[i].render_area.x = 0;
        self->passes[i].render_area.y = 0;
        self->passes[i].render_area.z = width;
        self->passes[i].render_area.w = height;
    }
}

b8 render_view_pick_on_packet_build(const struct render_view* self, struct linear_allocator* frame_allocator, void* data, struct render_view_packet* out_packet) {
    if (!self || !data || !out_packet) {
        KWARN("render_view_pick_on_packet_build requires valid pointer to view, packet, and data.");
        return false;
    }

    pick_packet_data* packet_data = (pick_packet_data*)data;
    render_view_pick_internal_data* internal_data = (render_view_pick_internal_data*)self->internal_data;

    out_packet->geometries = darray_create(geometry_render_data);
    out_packet->terrain_geometries = darray_create(geometry_render_data);
    out_packet->view = self;

    // TODO: Get active camera.
    camera* world_camera = camera_system_get_default();
    internal_data->world_shader_info.view = camera_view_get(world_camera);
    internal_data->terrain_shader_info.view = camera_view_get(world_camera);

    // Set the pick packet data to extended data.
    packet_data->ui_geometry_count = 0;
    out_packet->extended_data = linear_allocator_allocate(frame_allocator, sizeof(pick_packet_data));

    u32 world_geometry_count = !packet_data->world_mesh_data ? 0 : darray_length(packet_data->world_mesh_data);

    u32 highest_instance_id = 0;
    // Iterate all geometries in world data.
    for (u32 i = 0; i < world_geometry_count; ++i) {
        darray_push(out_packet->geometries, packet_data->world_mesh_data[i]);

        // Count all geometries as a single id.
        if (packet_data->world_mesh_data[i].unique_id > highest_instance_id) {
            highest_instance_id = packet_data->world_mesh_data[i].unique_id;
        }
    }

    // Iterate all terrains in the world data.
    u32 terrain_geometry_count = !packet_data->terrain_mesh_data ? 0 : darray_length(packet_data->terrain_mesh_data);

    // Iterate all geometries in terrain data.
    for (u32 i = 0; i < terrain_geometry_count; ++i) {
        darray_push(out_packet->terrain_geometries, packet_data->terrain_mesh_data[i]);

        // Count all geometries as a single id.
        if (packet_data->terrain_mesh_data[i].unique_id > highest_instance_id) {
            highest_instance_id = packet_data->terrain_mesh_data[i].unique_id;
        }
    }

    // Iterate all meshes in UI data.
    for (u32 i = 0; i < packet_data->ui_mesh_data.mesh_count; ++i) {
        mesh* m = packet_data->ui_mesh_data.meshes[i];
        for (u32 j = 0; j < m->geometry_count; ++j) {
            geometry_render_data render_data;
            render_data.geometry = m->geometries[j];
            render_data.model = transform_world_get(&m->transform);
            render_data.unique_id = m->unique_id;
            darray_push(out_packet->geometries, render_data);
            out_packet->geometry_count++;
        }
        // Count all geometries as a single id.
        if (m->unique_id > highest_instance_id) {
            highest_instance_id = m->unique_id;
        }
    }

    // Count texts as well.
    for (u32 i = 0; i < packet_data->text_count; ++i) {
        if (packet_data->texts[i]->unique_id > highest_instance_id) {
            highest_instance_id = packet_data->texts[i]->unique_id;
        }
    }

    i32 required_instance_count = highest_instance_id + 1;

    // TODO: this needs to take into account the highest id, not the count, because they can and do skip ids.
    // Verify instance resources exist.
    if (required_instance_count > internal_data->instance_count) {
        u32 diff = required_instance_count - internal_data->instance_count;
        for (u32 i = 0; i < diff; ++i) {
            acquire_shader_instances(self);
        }
    }

    // Copy over the packet data.
    kcopy_memory(out_packet->extended_data, packet_data, sizeof(pick_packet_data));

    return true;
}

void render_view_pick_on_packet_destroy(const struct render_view* self, struct render_view_packet* packet) {
    darray_destroy(packet->geometries);
    darray_destroy(packet->terrain_geometries);
    kzero_memory(packet, sizeof(render_view_packet));
}

b8 render_view_pick_on_render(const struct render_view* self, const struct render_view_packet* packet, u64 frame_number, u64 render_target_index, const struct frame_data* p_frame_data) {
    render_view_pick_internal_data* data = self->internal_data;

    u32 p = 0;
    renderpass* pass = &self->passes[p];  // First pass

    if (render_target_index == 0) {
        // Reset.
        u64 count = darray_length(data->instance_updated);
        for (u64 i = 0; i < count; ++i) {
            data->instance_updated[i] = false;
        }

        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_ui_on_render pass index %u failed to start.", p);
            return false;
        }

        pick_packet_data* packet_data = (pick_packet_data*)packet->extended_data;

        i32 current_instance_id = 0;

        // World
        if (!shader_system_use_by_id(data->world_shader_info.s->id)) {
            KERROR("Failed to use world pick shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!shader_system_uniform_set_by_index(data->world_shader_info.projection_location, &data->world_shader_info.projection)) {
            KERROR("Failed to apply projection matrix");
        }
        if (!shader_system_uniform_set_by_index(data->world_shader_info.view_location, &data->world_shader_info.view)) {
            KERROR("Failed to apply view matrix");
        }
        shader_system_apply_global();

        // Draw geometries. Start from 0 since world geometries are added first, and stop at the world geometry count.
        u32 world_geometry_count = !packet_data->world_mesh_data ? 0 : darray_length(packet_data->world_mesh_data);
        for (u32 i = 0; i < world_geometry_count; ++i) {
            geometry_render_data* geo = &packet->geometries[i];
            current_instance_id = geo->unique_id;

            shader_system_bind_instance(current_instance_id);

            // Get colour based on id
            vec3 id_colour;
            u32 r, g, b;
            u32_to_rgb(geo->unique_id, &r, &g, &b);
            rgb_u32_to_vec3(r, g, b, &id_colour);
            if (!shader_system_uniform_set_by_index(data->world_shader_info.id_colour_location, &id_colour)) {
                KERROR("Failed to apply id colour uniform.");
                return false;
            }

            b8 needs_update = !data->instance_updated[current_instance_id];
            shader_system_apply_instance(needs_update);
            data->instance_updated[current_instance_id] = true;

            // Apply the locals
            if (!shader_system_uniform_set_by_index(data->world_shader_info.model_location, &geo->model)) {
                KERROR("Failed to apply model matrix for world geometry.");
            }

            // Draw it.
            renderer_geometry_draw(&packet->geometries[i]);
        }
        // End world geometries

        // Terrain geometries
        if (!shader_system_use_by_id(data->terrain_shader_info.s->id)) {
            KERROR("Failed to use terrain pick shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!shader_system_uniform_set_by_index(data->terrain_shader_info.projection_location, &data->terrain_shader_info.projection)) {
            KERROR("Failed to apply projection matrix");
        }
        if (!shader_system_uniform_set_by_index(data->terrain_shader_info.view_location, &data->terrain_shader_info.view)) {
            KERROR("Failed to apply view matrix");
        }
        shader_system_apply_global();

        // Draw geometries. Start from 0 since terrain geometries are added first, and stop at the terrain geometry count.
        u32 terrain_geometry_count = !packet_data->terrain_mesh_data ? 0 : darray_length(packet_data->terrain_mesh_data);
        for (u32 i = 0; i < terrain_geometry_count; ++i) {
            geometry_render_data* geo = &packet->terrain_geometries[i];
            current_instance_id = geo->unique_id;

            shader_system_bind_instance(current_instance_id);

            // Get colour based on id
            vec3 id_colour;
            u32 r, g, b;
            u32_to_rgb(geo->unique_id, &r, &g, &b);
            rgb_u32_to_vec3(r, g, b, &id_colour);
            if (!shader_system_uniform_set_by_index(data->terrain_shader_info.id_colour_location, &id_colour)) {
                KERROR("Failed to apply id colour uniform.");
                return false;
            }

            b8 needs_update = !data->instance_updated[current_instance_id];
            shader_system_apply_instance(needs_update);
            data->instance_updated[current_instance_id] = true;

            // Apply the locals
            if (!shader_system_uniform_set_by_index(data->terrain_shader_info.model_location, &geo->model)) {
                KERROR("Failed to apply model matrix for terrain geometry.");
            }

            // Draw it.
            renderer_geometry_draw(&packet->terrain_geometries[i]);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_pick_on_render pass index %u failed to end.", p);
            return false;
        }

        p++;
        pass = &self->passes[p];  // Second pass

        if (!renderer_renderpass_begin(pass, &pass->targets[render_target_index])) {
            KERROR("render_view_pick_on_render pass index %u failed to start.", p);
            return false;
        }

        // UI
        if (!shader_system_use_by_id(data->ui_shader_info.s->id)) {
            KERROR("Failed to use material shader. Render frame failed.");
            return false;
        }

        // Apply globals
        if (!shader_system_uniform_set_by_index(data->ui_shader_info.projection_location, &data->ui_shader_info.projection)) {
            KERROR("Failed to apply projection matrix");
        }
        if (!shader_system_uniform_set_by_index(data->ui_shader_info.view_location, &data->ui_shader_info.view)) {
            KERROR("Failed to apply view matrix");
        }
        shader_system_apply_global();

        // Draw geometries. Start off where world geometries left off.
        for (u32 i = world_geometry_count; i < packet->geometry_count; ++i) {
            geometry_render_data* geo = &packet->geometries[i];
            current_instance_id = geo->unique_id;

            shader_system_bind_instance(current_instance_id);

            // Get colour based on id
            vec3 id_colour;
            u32 r, g, b;
            u32_to_rgb(geo->unique_id, &r, &g, &b);
            rgb_u32_to_vec3(r, g, b, &id_colour);
            if (!shader_system_uniform_set_by_index(data->ui_shader_info.id_colour_location, &id_colour)) {
                KERROR("Failed to apply id colour uniform.");
                return false;
            }

            b8 needs_update = !data->instance_updated[current_instance_id];
            shader_system_apply_instance(needs_update);
            data->instance_updated[current_instance_id] = true;

            // Apply the locals
            if (!shader_system_uniform_set_by_index(data->ui_shader_info.model_location, &geo->model)) {
                KERROR("Failed to apply model matrix for text");
            }

            // Draw it.
            renderer_geometry_draw(&packet->geometries[i]);
        }

        // Draw bitmap text
        for (u32 i = 0; i < packet_data->text_count; ++i) {
            ui_text* text = packet_data->texts[i];
            current_instance_id = text->unique_id;
            shader_system_bind_instance(current_instance_id);

            // Get colour based on id
            vec3 id_colour;
            u32 r, g, b;
            u32_to_rgb(text->unique_id, &r, &g, &b);
            rgb_u32_to_vec3(r, g, b, &id_colour);
            if (!shader_system_uniform_set_by_index(data->ui_shader_info.id_colour_location, &id_colour)) {
                KERROR("Failed to apply id colour uniform.");
                return false;
            }

            shader_system_apply_instance(true);

            // Apply the locals
            mat4 model = transform_world_get(&text->transform);
            if (!shader_system_uniform_set_by_index(data->ui_shader_info.model_location, &model)) {
                KERROR("Failed to apply model matrix for text");
            }

            ui_text_draw(text);
        }

        if (!renderer_renderpass_end(pass)) {
            KERROR("render_view_ui_on_render pass index %u failed to end.", p);
            return false;
        }
    }

    // Read pixel data.
    texture* t = &data->colour_target_attachment_texture;

    // Read the pixel at the mouse coordinate.
    u8 pixel_rgba[4] = {0};
    u8* pixel = &pixel_rgba[0];

    // Clamp to image size
    u16 x_coord = KCLAMP(data->mouse_x, 0, self->width - 1);
    u16 y_coord = KCLAMP(data->mouse_y, 0, self->height - 1);
    renderer_texture_read_pixel(t, x_coord, y_coord, &pixel);

    // Extract the id from the sampled colour.
    u32 id = INVALID_ID;
    rgbu_to_u32(pixel[0], pixel[1], pixel[2], &id);
    if (id == 0x00FFFFFF) {
        // This is pure white.
        id = INVALID_ID;
    }

    event_context context;
    context.data.u32[0] = id;
    event_fire(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, 0, context);

    return true;
}

b8 render_view_pick_attachment_target_regenerate(struct render_view* self, u32 pass_index, struct render_target_attachment* attachment) {
    render_view_pick_internal_data* data = self->internal_data;

    if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_COLOUR) {
        attachment->texture = &data->colour_target_attachment_texture;
    } else if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_DEPTH) {
        attachment->texture = &data->depth_target_attachment_texture;
    } else {
        KERROR("Unsupported attachment type 0x%x.", attachment->type);
        return false;
    }

    if (pass_index == 1) {
        // Do not need to regenerate for both passes since they both use the same attachment.
        // Just attach it and move on.
        return true;
    }

    // Destroy current attachment if it exists.
    if (attachment->texture->internal_data) {
        renderer_texture_destroy(attachment->texture);
        kzero_memory(attachment->texture, sizeof(texture));
    }

    // Setup a new texture.
    // Generate a UUID to act as the texture name.
    uuid texture_name_uuid = uuid_generate();

    u32 width = self->passes[pass_index].render_area.z;
    u32 height = self->passes[pass_index].render_area.w;
    b8 has_transparency = false;  // TODO: configurable

    attachment->texture->id = INVALID_ID;
    attachment->texture->type = TEXTURE_TYPE_2D;
    string_ncopy(attachment->texture->name, texture_name_uuid.value, TEXTURE_NAME_MAX_LENGTH);
    attachment->texture->width = width;
    attachment->texture->height = height;
    attachment->texture->channel_count = 4;  // TODO: configurable
    attachment->texture->generation = INVALID_ID;
    attachment->texture->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    attachment->texture->flags |= TEXTURE_FLAG_IS_WRITEABLE;
    if (attachment->type == RENDER_TARGET_ATTACHMENT_TYPE_DEPTH) {
        attachment->texture->flags |= TEXTURE_FLAG_DEPTH;
    }
    attachment->texture->internal_data = 0;

    renderer_texture_create_writeable(attachment->texture);

    return true;
}
