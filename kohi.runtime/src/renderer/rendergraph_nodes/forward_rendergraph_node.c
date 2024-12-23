#include "forward_rendergraph_node.h"

#include "core/engine.h"
#include "core_render_types.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/camera.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "strings/kname.h"
#include "strings/kstring.h"

#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "resources/skybox.h"
#include "resources/water_plane.h"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"
#include "systems/timeline_system.h"
#include <runtime_defines.h>

#define UNIFORM_APPLY_OR_FAIL(expr)                   \
    if (!expr) {                                      \
        KERROR("Failed to apply uniform: %s", #expr); \
        return false;                                 \
    }

// Samplers
const u32 PBR_SAMP_IDX_ALBEDO = 0;
const u32 PBR_SAMP_IDX_NORMAL = 1;
const u32 PBR_SAMP_IDX_COMBINED = 2;
const u32 PBR_SAMP_IDX_SHADOW_MAP = 3;
const u32 PBR_SAMP_IDX_IRRADIANCE_MAP = 4;

// Terrain materials are now all loaded into a single array texture.
const u32 TERRAIN_SAMP_IDX_MATERIAL_ARRAY_MAP = 0;
const u32 TERRAIN_SAMP_IDX_SHADOW_MAP = 1 + TERRAIN_SAMP_IDX_MATERIAL_ARRAY_MAP;
const u32 TERRAIN_SAMP_IDX_IRRADIANCE_MAP = 1 + TERRAIN_SAMP_IDX_SHADOW_MAP;

typedef struct terrain_shader_locations {
    b8 loaded;
    u16 projection;
    u16 views;
    u16 cascade_splits;
    u16 view_positions;
    u16 model;
    u16 render_mode;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;

    u16 properties;
    u16 ibl_cube_texture;
    u16 shadow_textures;
    u16 light_space_0;
    u16 light_space_1;
    u16 light_space_2;
    u16 light_space_3;
    u16 material_texures;
    u16 use_pcf;
    u16 bias;
    u16 clipping_plane;
    u16 view_index;
} terrain_shader_locations;

typedef struct water_shader_locations {
    // Global
    u16 projection;
    u16 view;
    u16 light_space;
    u16 cascade_splits;
    u16 view_position;
    u16 mode;
    u16 use_pcf;
    u16 bias;
    // Instance uniforms
    u16 dir_light;
    u16 p_lights;
    u16 tiling;
    u16 wave_strength;
    u16 move_factor;
    u16 num_p_lights;
    // Instance samplers
    u16 reflection_texture;
    u16 refraction_texture;
    u16 dudv_texture;
    u16 normal_texture;
    u16 shadow_textures;
    u16 ibl_cube_texture;
    u16 refract_depth_texture;
    // Local uniforms
    u16 model;
} water_shader_locations;

typedef struct skybox_shader_locations {
    u16 frame_ubo;
    u16 cube_texture;
    u16 cube_sampler;
    u16 draw_ubo;
} skybox_shader_locations;

#define SKYBOX_MAX_VIEWS 4

// per frame UBO
typedef struct skybox_frame_ubo {
    mat4 views[SKYBOX_MAX_VIEWS];
    mat4 projection;
} skybox_frame_ubo;

// per draw UBO
typedef struct skybox_draw_ubo {
    u32 view_index;
    vec3 padding;
} skybox_draw_ubo;

typedef struct forward_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    struct texture_system_state* texture_system;
    struct material_system_state* material_system;
    /* forward_rendergraph_node_config config; */

    struct kresource_texture* colourbuffer_texture;
    struct kresource_texture* depthbuffer_texture;

    // Known locations for water shader.
    water_shader_locations water_shader_locations;

    // Known locations for skybox shader.
    skybox_shader_locations skybox_shader_locations;

    renderbuffer* vertex_buffer;
    renderbuffer* index_buffer;

    rendergraph_source* shadowmap_source;

    // Obtained from source.
    kresource_texture* shadow_map;

    // Execution data

    u32 render_mode;
    viewport vp;

    // mat4 view_matrix;
    // vec3 view_position;
    mat4 projection_matrix;
    camera* current_camera;

    u32 geometry_count;
    struct geometry_render_data* geometries;

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;

    u32 water_plane_count;
    water_plane** water_planes;

    skybox* sb;

    const struct kresource_texture* irradiance_cube_texture;
    const struct directional_light* dir_light;

    f32 cascade_splits[MATERIAL_MAX_SHADOW_CASCADES];
    mat4 directional_light_views[MATERIAL_MAX_SHADOW_CASCADES];
    mat4 directional_light_projections[MATERIAL_MAX_SHADOW_CASCADES];
    // The multiplied view/projections
    mat4 directional_light_spaces[MATERIAL_MAX_SHADOW_CASCADES];

    // An array of global scene-wide set of ibl cube textures from probes.
    u32 ibl_cube_texture_count;
    kresource_texture** ibl_cube_textures;

    // Skybox shader.
    khandle skybox_shader;

} forward_rendergraph_node_internal_data;

b8 forward_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, const struct rendergraph_node_config* config) {
    if (!self) {
        KERROR("forward_rendergraph_node_create requires a valid pointer to a pass");
        return false;
    }
    if (!config) {
        KERROR("forward_rendergraph_node_create requires a valid configuration.");
        return false;
    }

    // Setup internal data.
    self->internal_data = kallocate(sizeof(forward_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;
    internal_data->renderer = engine_systems_get()->renderer_system;
    internal_data->texture_system = engine_systems_get()->texture_system;
    internal_data->material_system = engine_systems_get()->material_system;

    self->name = string_duplicate(config->name);

    // Setup sinks
    self->sink_count = 3;
    self->sinks = kallocate(sizeof(rendergraph_sink) * self->sink_count, MEMORY_TAG_ARRAY);

    // Grab sink configs first. These are all required because they need source linkages.
    rendergraph_node_sink_config* colourbuffer_sink_config = 0;
    rendergraph_node_sink_config* depthbuffer_sink_config = 0;
    rendergraph_node_sink_config* shadow_sink_config = 0;
    for (u32 i = 0; i < config->sink_count; ++i) {
        rendergraph_node_sink_config* sink = &config->sinks[i];
        if (strings_equali("colourbuffer", sink->name)) {
            colourbuffer_sink_config = sink;
            continue;
        } else if (strings_equali("depthbuffer", sink->name)) {
            depthbuffer_sink_config = sink;
            continue;
        } else if (strings_equali("shadow", sink->name)) {
            shadow_sink_config = sink;
            continue;
        }
    }

    // Colourbuffer sink
    if (!colourbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'colourbuffer'.");
        return false;
    } else {
        rendergraph_sink* colourbuffer_sink = &self->sinks[0];
        colourbuffer_sink->name = string_duplicate("colourbuffer");
        colourbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        colourbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        colourbuffer_sink->configured_source_name = string_duplicate(colourbuffer_sink_config->source_name);
    }

    // Depthbuffer sink
    if (!depthbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'depthbuffer'.");
        return false;
    } else {
        rendergraph_sink* depthbuffer_sink = &self->sinks[1];
        depthbuffer_sink->name = string_duplicate("depthbuffer");
        depthbuffer_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        depthbuffer_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        depthbuffer_sink->configured_source_name = string_duplicate(depthbuffer_sink_config->source_name);
    }

    // Shadow sink
    if (!depthbuffer_sink_config) {
        KERROR("Forward rendergraph node requires configuration for sink called 'shadow'.");
        return false;
    } else {
        rendergraph_sink* shadow_sink = &self->sinks[2];
        shadow_sink->name = string_duplicate("shadow");
        shadow_sink->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
        shadow_sink->bound_source = 0;
        // Save off the configured source name for later lookup and binding.
        shadow_sink->configured_source_name = string_duplicate(shadow_sink_config->source_name);
    }

    // Has two sources, one for the colourbuffer and one for the depthbuffer.
    self->source_count = 2;
    self->sources = kallocate(sizeof(rendergraph_source) * self->source_count, MEMORY_TAG_ARRAY);

    // Setup the colourbuffer source.
    rendergraph_source* colourbuffer_source = &self->sources[0];
    colourbuffer_source->name = string_duplicate("colourbuffer");
    colourbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    colourbuffer_source->value.t = 0;
    colourbuffer_source->is_bound = false;

    // Setup the depthbuffer source.
    rendergraph_source* depthbuffer_source = &self->sources[1];
    depthbuffer_source->name = string_duplicate("depthbuffer");
    depthbuffer_source->type = RENDERGRAPH_RESOURCE_TYPE_TEXTURE;
    depthbuffer_source->value.t = 0;
    depthbuffer_source->is_bound = false;

    // Function pointers.
    self->initialize = forward_rendergraph_node_initialize;
    self->destroy = forward_rendergraph_node_destroy;
    self->load_resources = forward_rendergraph_node_load_resources;
    self->execute = forward_rendergraph_node_execute;

    return true;
}

b8 forward_rendergraph_node_initialize(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }

    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Load Skybox shader and get shader uniform locations.
    internal_data->skybox_shader = shader_system_get(kname_create(SHADER_NAME_RUNTIME_SKYBOX), kname_create(PACKAGE_NAME_RUNTIME));
    internal_data->skybox_shader_locations.frame_ubo = shader_system_uniform_location(internal_data->skybox_shader, kname_create("skybox_frame_ubo"));
    if (internal_data->skybox_shader_locations.frame_ubo == INVALID_ID_U16) {
        KERROR("Failed to shader get uniform location for skybox_frame_ubo.");
        return false;
    }

    internal_data->skybox_shader_locations.cube_texture = shader_system_uniform_location(internal_data->skybox_shader, kname_create("cube_texture"));
    if (internal_data->skybox_shader_locations.cube_texture == INVALID_ID_U16) {
        KERROR("Failed to shader get uniform location for cube_texture.");
        return false;
    }

    internal_data->skybox_shader_locations.cube_sampler = shader_system_uniform_location(internal_data->skybox_shader, kname_create("cube_sampler"));
    if (internal_data->skybox_shader_locations.cube_sampler == INVALID_ID_U16) {
        KERROR("Failed to shader get uniform location for cube_sampler.");
        return false;
    }

    internal_data->skybox_shader_locations.draw_ubo = shader_system_uniform_location(internal_data->skybox_shader, kname_create("skybox_draw_ubo"));
    if (internal_data->skybox_shader_locations.draw_ubo == INVALID_ID_U16) {
        KERROR("Failed to shader get uniform location for skybox_draw_ubo.");
        return false;
    }

    internal_data->vertex_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_VERTEX);
    internal_data->index_buffer = renderer_renderbuffer_get(RENDERBUFFER_TYPE_INDEX);

    // Grab the default cubemap texture as the irradiance texture.
    internal_data->irradiance_cube_texture = texture_system_request(kname_create(DEFAULT_CUBE_TEXTURE_NAME), INVALID_KNAME, 0, 0);

    // Assign some defaults.
    for (u32 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
        internal_data->directional_light_spaces[i] = mat4_identity();
    }

    return true;
}

b8 forward_rendergraph_node_load_resources(struct rendergraph_node* self) {
    if (!self) {
        return false;
    }
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Resolve framebuffer handle via bound source.
    if (self->sinks[0].bound_source) {
        internal_data->colourbuffer_texture = self->sinks[0].bound_source->value.t;
        self->sources[0].value.t = internal_data->colourbuffer_texture;
        self->sources[0].is_bound = true;
    }

    if (self->sinks[1].bound_source) {
        internal_data->depthbuffer_texture = self->sinks[1].bound_source->value.t;
        self->sources[1].value.t = internal_data->depthbuffer_texture;
        self->sources[1].is_bound = true;
    }

    if (self->sinks[2].bound_source) {
        internal_data->shadowmap_source = self->sinks[2].bound_source;
    }

    if (!internal_data->shadowmap_source) {
        KERROR("Required '%s' source not hooked up to forward pass. Creation fails.", "shadowmap");
        return false;
    }

    return true;
}

b8 render_water_planes(forward_rendergraph_node_internal_data* internal_data, u32 plane_count, water_plane** planes, kresource_texture* colour, kresource_texture* depth, vec4 clipping_plane, camera* cam, struct frame_data* p_frame_data) {
    // Draw the water plane
    renderer_begin_debug_label("water planes", (vec3){0, 0, 1});

    if (plane_count) {
        khandle game_timeline = timeline_system_get_game();
        f32 delta_time = timeline_system_delta_get(game_timeline);
        f32 game_time = timeline_system_total_get(game_timeline);
        mat4 view_matrix = camera_view_get(internal_data->current_camera);
        vec4 view_position = vec4_from_vec3(camera_position_get(internal_data->current_camera), 1.0);

        // Bind the viewport
        renderer_active_viewport_set(&internal_data->vp);

        // Set the per-frame material data (i.e. view, projection, etc.) before the below call.
        {
            material_frame_data mat_frame_data = {0};
            mat_frame_data.delta_time = delta_time;
            mat_frame_data.game_time = game_time;
            mat_frame_data.projection = internal_data->projection_matrix;
            mat_frame_data.views[0] = view_matrix;
            mat_frame_data.view_positions[0] = view_position;
            mat_frame_data.render_mode = internal_data->render_mode;
            // Cascade splits and light space matrices for shadow mapping. One per cascade.
            kcopy_memory(mat_frame_data.cascade_splits, internal_data->cascade_splits, sizeof(vec4) * MATERIAL_MAX_SHADOW_CASCADES);
            kcopy_memory(mat_frame_data.directional_light_spaces, internal_data->directional_light_spaces, sizeof(mat4) * MATERIAL_MAX_SHADOW_CASCADES);

            // HACK: Read this in from somewhere (or have global setter?);
            mat_frame_data.shadow_bias = 0.0005f;

            // Shadow Maps (global)
            mat_frame_data.shadow_map_texture = internal_data->shadow_map;

            // Irradience maps provided by probes around in the world.
            kzero_memory(mat_frame_data.irradiance_cubemap_textures, sizeof(kresource_texture*) * MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT);
            if (internal_data->ibl_cube_texture_count == 0) {
                mat_frame_data.irradiance_cubemap_textures[0] = texture_system_request_cube(kname_create(DEFAULT_CUBE_TEXTURE_NAME), false, false, 0, 0);
            } else {
                for (u32 i = 0; i < internal_data->ibl_cube_texture_count; ++i) {
                    mat_frame_data.irradiance_cubemap_textures[i] = internal_data->ibl_cube_textures[i];
                }
            }

            // Update per-frame data for materials.
            material_system_prepare_frame(internal_data->material_system, mat_frame_data, p_frame_data);
        }

        // Draw each plane.
        for (u32 i = 0; i < plane_count; ++i) {

            water_plane* plane = planes[i];
            material_instance* inst = &plane->material;

            // Apply material-level (i.e. group-level) data.
            if (!material_system_apply(internal_data->material_system, inst->material, p_frame_data)) {
                KERROR("Error applying material. See logs for details.");
                return false;
            }

            // Apply material-instance-level (i.e. per-draw-level) data.
            material_instance_draw_data instance_draw_data = {0};
            instance_draw_data.model = plane->model;
            instance_draw_data.view_index = 0; // FIXME: This should be passed in.
            instance_draw_data.clipping_plane = clipping_plane;
            instance_draw_data.irradiance_cubemap_index = 0; // FIXME: Get this passed in as well.
            if (!material_system_apply_instance(internal_data->material_system, inst, instance_draw_data, p_frame_data)) {
                KERROR("Failed to apply per-instance material data. See logs for details.");
                return false;
            }

            // Draw based on vert/index data.
            if (!renderer_renderbuffer_draw(internal_data->vertex_buffer, plane->vertex_buffer_offset, 4, true)) {
                KERROR("Failed to bind vertex buffer data for water plane.");
                return false;
            }
            if (!renderer_renderbuffer_draw(internal_data->index_buffer, plane->index_buffer_offset, 6, false)) {
                KERROR("Failed to draw water plane using index data.");
                return false;
            }
        }
    }

    renderer_end_debug_label();

    return true;
}

b8 render_scene(forward_rendergraph_node_internal_data* internal_data, kresource_texture* colour, kresource_texture* depth, u32 plane_count, water_plane** planes, b8 include_water_plane, vec4 clipping_plane, camera* cam, camera* inverted_cam, b8 use_inverted, struct frame_data* p_frame_data) {
    mat4 view_matrix = camera_view_get(cam);
    vec4 view_position = vec4_from_vec3(camera_position_get(cam), 1.0);
    mat4 inverted_view_matrix = camera_view_get(inverted_cam);
    vec4 inverted_view_position = vec4_from_vec3(camera_position_get(inverted_cam), 1.0);
    u32 view_index = use_inverted ? 1 : 0;

    khandle game_timeline = timeline_system_get_game();
    f32 delta_time = timeline_system_delta_get(game_timeline);
    f32 game_time = timeline_system_total_get(game_timeline);

    // Begin rendering the scene
    renderer_begin_rendering(internal_data->renderer, p_frame_data, internal_data->vp.rect, 1, &colour->renderer_texture_handle, depth->renderer_texture_handle, 0);

    // Bind the viewport
    if (include_water_plane) {
        renderer_active_viewport_set(&internal_data->vp);
    } else {
        // Drawing to render target, use its size instead.
        rect_2d viewport_rect = (vec4){0, 0 + (f32)colour->height, (f32)colour->width, -(f32)colour->height};
        renderer_viewport_set(viewport_rect);

        rect_2d scissor_rect = (vec4){0, 0, (f32)colour->width, (f32)colour->height};
        renderer_scissor_set(scissor_rect);
    }

    // Skybox first.
    if (internal_data->sb && internal_data->sb->state == SKYBOX_STATE_LOADED) {
        renderer_begin_debug_label("skybox", (vec3){0.5f, 0.5f, 1.0});
        renderer_set_depth_test_enabled(false);
        renderer_set_depth_write_enabled(false);

        if (internal_data->sb->geometry.generation != INVALID_ID_U16) {
            shader_system_use(internal_data->skybox_shader);

            // Get the view matrix, but zero out the position so the skybox stays put on screen.
            mat4 view_matrix_skybox = view_matrix;
            view_matrix_skybox.data[12] = 0.0f;
            view_matrix_skybox.data[13] = 0.0f;
            view_matrix_skybox.data[14] = 0.0f;

            mat4 inverted_view_matrix_skybox = inverted_view_matrix;
            inverted_view_matrix_skybox.data[12] = 0.0f;
            inverted_view_matrix_skybox.data[13] = 0.0f;
            inverted_view_matrix_skybox.data[14] = 0.0f;

            // Apply per-frame
            {
                shader_system_bind_frame(internal_data->skybox_shader);

                skybox_frame_ubo frame_ubo = {0};
                frame_ubo.projection = internal_data->projection_matrix;
                frame_ubo.views[0] = view_matrix_skybox;
                frame_ubo.views[1] = inverted_view_matrix_skybox;

                if (!shader_system_uniform_set_by_location(internal_data->skybox_shader, internal_data->skybox_shader_locations.frame_ubo, &frame_ubo)) {
                    KERROR("Failed to apply skybox frame ubo.");
                    return false;
                }

                shader_system_apply_per_frame(internal_data->skybox_shader);
            }

            // per-group
            {
                shader_system_bind_group(internal_data->skybox_shader, internal_data->sb->group_id);

                if (!shader_system_texture_set_by_location(internal_data->skybox_shader, internal_data->skybox_shader_locations.cube_texture, internal_data->sb->cubemap)) {
                    KERROR("Failed to apply skybox cube map uniform.");
                    return false;
                }

                shader_system_apply_per_group(internal_data->skybox_shader, internal_data->sb->skybox_shader_group_data_generation);
            }

            // Apply the per-draw
            {
                shader_system_bind_draw_id(internal_data->skybox_shader, internal_data->sb->draw_id);
                skybox_draw_ubo draw_ubo = {0};
                draw_ubo.view_index = use_inverted ? 1 : 0;
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->skybox_shader, internal_data->skybox_shader_locations.draw_ubo, &draw_ubo));
                shader_system_apply_per_draw(internal_data->skybox_shader, internal_data->sb->skybox_shader_draw_data_generation);
            }

            // Draw it.
            geometry_render_data render_data = {};
            render_data.vertex_count = internal_data->sb->geometry.vertex_count;
            render_data.vertex_element_size = internal_data->sb->geometry.vertex_element_size;
            render_data.vertex_buffer_offset = internal_data->sb->geometry.vertex_buffer_offset;
            render_data.index_count = internal_data->sb->geometry.index_count;
            render_data.index_element_size = internal_data->sb->geometry.index_element_size;
            render_data.index_buffer_offset = internal_data->sb->geometry.index_buffer_offset;

            renderer_geometry_draw(&render_data);
        }

        // Restore depth state.
        renderer_set_depth_test_enabled(true);
        renderer_set_depth_write_enabled(true);

        renderer_end_debug_label();
    }

    // Calculate light-space matrices for each shadow cascade.
    for (u8 i = 0; i < MATERIAL_MAX_SHADOW_CASCADES; ++i) {
        internal_data->directional_light_spaces[i] = mat4_mul(internal_data->directional_light_views[i], internal_data->directional_light_projections[i]);
    }

    // Use the appropriate shader and apply the global uniforms.
    // FIXME: Replace "terrain" material with "blended" material and re-enable.
    /* u32 terrain_geometry_count = internal_data->terrain_geometry_count;
    if (terrain_geometry_count > 0) {
        if (!shader_system_use_by_id(internal_data->terrain_shader->id)) {
            KERROR("Failed to use terrain pbr shader.");
            return false;
        }

        // Ensure wireframe mode is (un)set.
        if (internal_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
            shader_system_set_wireframe(internal_data->terrain_shader_id, true);
        } else {
            shader_system_set_wireframe(internal_data->terrain_shader_id, false);
        }

        // Apply globals
        {
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.projection, &internal_data->projection_matrix));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.views, 0, &view_matrix));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.view_positions, 0, &view_position));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.views, 1, &inverted_view_matrix));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->terrain_shader_id, internal_data->terrain_locations.view_positions, 1, &inverted_view_position));

            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.render_mode, &internal_data->render_mode));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.cascade_splits, &internal_data->cascade_splits));

            // Light space for shadow mapping. Per cascade
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.light_space_0 + i, &internal_data->directional_light_spaces[i]));
            }

            // Directional light - global for this shader..
            directional_light_data default_dir_light_data = {0};
            const directional_light_data* dir_light_data = &default_dir_light_data;
            if (internal_data->dir_light) {
                dir_light_data = &internal_data->dir_light->data;
            }
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.dir_light, dir_light_data));

            // Global shader options.
            i32 use_pcf = (i32)renderer_pcf_enabled(internal_data->renderer);
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.use_pcf, &use_pcf));

            // HACK: Read this in from somewhere (or have global setter?);
            f32 bias = 0.0005f;
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.bias, &bias));

            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.clipping_plane, &clipping_plane));

            // Apply/upload them to the GPU
            if (!shader_system_apply_per_frame(internal_data->terrain_shader_id)) {
                KERROR("Failed to apply global uniforms.");
                return false;
            }
        }

        // Handle each terrain chunk
        for (u32 i = 0; i < terrain_geometry_count; ++i) {
            material* m = internal_data->terrain_geometries[i].material;
            if (!m || m->internal_id == INVALID_ID) {
                m = material_system_get_default_terrain();
            }

            // Update the material if it hasn't already been this frame. This keeps the
            // same material from being updated multiple times. It still needs to be bound
            // either way, so this check result gets passed to the backend which either
            // updates the internal shader bindings and binds them, or only binds them.
            // Also need to check against the renderer draw index.
            // TODO: At least for now, the entire terrain shares one material, so a lot of this
            // should probably be moved to global (i.e. texture maps and surface properties), but
            // leave lighting at the instance level.

            // Apply instance
            {
                // Bind the instance.
                if (!shader_system_bind_group(internal_data->terrain_shader_id, m->internal_id)) {
                    KERROR("Failed to bind instance for material: %d", m->internal_id);
                    return false;
                }

                // Apply material maps, all as one layered texture.
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.material_texures, &m->maps[TERRAIN_SAMP_IDX_MATERIAL_ARRAY_MAP]));

                // NOTE: apply other maps separately.

                // Shadow Maps TODO: Should this be global?
                const kresource_texture* shadow_map_texture = internal_data->shadowmap_source->value.t;
                m->maps[TERRAIN_SAMP_IDX_SHADOW_MAP].texture = shadow_map_texture ? shadow_map_texture : texture_system_get_default_kresource_terrain_texture(internal_data->texture_system);
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.shadow_textures, &m->maps[TERRAIN_SAMP_IDX_SHADOW_MAP]));

                // Irradience map - use the material-assigned one if exists, otherwise use the "global" assigned one.
                m->maps[TERRAIN_SAMP_IDX_IRRADIANCE_MAP].texture = m->irradiance_texture ? m->irradiance_texture : internal_data->irradiance_cube_texture;
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.ibl_cube_texture, &m->maps[TERRAIN_SAMP_IDX_IRRADIANCE_MAP]));

                // Apply properties.
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.properties, m->properties));

                // Point lights.
                u32 p_light_count = light_system_point_light_count();
                if (p_light_count) {
                    point_light* p_lights = p_frame_data->allocator.allocate(sizeof(point_light) * p_light_count);
                    light_system_point_lights_get(p_lights);

                    point_light_data* p_light_datas = p_frame_data->allocator.allocate(sizeof(point_light_data) * p_light_count);
                    for (u32 i = 0; i < p_light_count; ++i) {
                        p_light_datas[i] = p_lights[i].data;
                    }

                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.p_lights, p_light_datas));
                }

                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.num_p_lights, &p_light_count));

                // Apply/upload them to the GPU
                if (!shader_system_apply_per_group(internal_data->terrain_shader_id)) {
                    KERROR("Failed to apply instance-level uniforms for material '%s'.", m->name);
                }
            }

            // Apply the locals
            {
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.model, &internal_data->terrain_geometries[i].model));
                int view_index = use_inverted ? 1 : 0;
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.view_index, &view_index));
                shader_system_apply_per_draw(internal_data->terrain_shader_id);
            }

            // Draw it.
            renderer_geometry_draw(&internal_data->terrain_geometries[i]);
        }
    } */

    // Tracker for water being drawn.
    b8 water_drawn = false;

    // Static geometries.
    // TODO: this should essentially turn into a "material system apply" here.
    {
        u32 geometry_count = internal_data->geometry_count;
        if (geometry_count > 0) {

            // Set the per-frame material data (i.e. view, projection, etc.) before the below call.
            {
                material_frame_data mat_frame_data = {0};
                mat_frame_data.delta_time = delta_time;
                mat_frame_data.game_time = game_time;
                mat_frame_data.projection = internal_data->projection_matrix;
                mat_frame_data.views[0] = view_matrix;
                mat_frame_data.views[1] = inverted_view_matrix;
                mat_frame_data.view_positions[0] = view_position;
                mat_frame_data.view_positions[1] = inverted_view_position;
                mat_frame_data.render_mode = internal_data->render_mode;
                // Cascade splits and light space matrices for shadow mapping. One per cascade.
                kcopy_memory(mat_frame_data.cascade_splits, internal_data->cascade_splits, sizeof(vec4) * MATERIAL_MAX_SHADOW_CASCADES);
                kcopy_memory(mat_frame_data.directional_light_spaces, internal_data->directional_light_spaces, sizeof(mat4) * MATERIAL_MAX_SHADOW_CASCADES);

                // HACK: Read this in from somewhere (or have global setter?);
                mat_frame_data.shadow_bias = 0.0005f;

                // Shadow Maps (global)
                mat_frame_data.shadow_map_texture = internal_data->shadow_map;

                // Irradience maps provided by probes around in the world.
                kzero_memory(mat_frame_data.irradiance_cubemap_textures, sizeof(kresource_texture*) * MATERIAL_MAX_IRRADIANCE_CUBEMAP_COUNT);
                if (internal_data->ibl_cube_texture_count == 0) {
                    mat_frame_data.irradiance_cubemap_textures[0] = texture_system_request_cube(kname_create(DEFAULT_CUBE_TEXTURE_NAME), false, false, 0, 0);
                } else {
                    for (u32 i = 0; i < internal_data->ibl_cube_texture_count; ++i) {
                        mat_frame_data.irradiance_cubemap_textures[i] = internal_data->ibl_cube_textures[i];
                    }
                }

                // Update per-frame data for materials.
                material_system_prepare_frame(internal_data->material_system, mat_frame_data, p_frame_data);
            }

            khandle current_material = khandle_invalid();
            // Draw geometries.
            u32 count = internal_data->geometry_count;
            // Keep track of when transparent rendering begins. The water plane, if drawn,
            // must happen before this. NOTE: This may cause problems with transparent objects behind the water plane.
            b8 transparency_started = false;

            for (u32 i = 0; i < count; ++i) {
                geometry_render_data* render_data = &internal_data->geometries[i];
                material_instance* inst = &render_data->material;

                // Only rebind/update the material if it's a different material. Duplicates can reuse the already-bound material.
                if (inst->material.handle_index != current_material.handle_index) {
                    // If the material has transparency, pause rendering if water planes are to be rendered.
                    b8 has_transparency = material_flag_get(internal_data->material_system, inst->material, KMATERIAL_FLAG_HAS_TRANSPARENCY_BIT);
                    if (include_water_plane && !transparency_started && has_transparency) {
                        if (!render_water_planes(internal_data, plane_count, planes, colour, depth, vec4_zero(), cam, p_frame_data)) {
                            KERROR("Failed to draw water plane! See logs for details.");
                            return false;
                        } else {
                            water_drawn = true;
                            transparency_started = true;
                        }

                        // Apply material-level (i.e. group-level) data.
                        if (!material_system_apply(internal_data->material_system, inst->material, p_frame_data)) {
                            KERROR("Error applying material. See logs for details.");
                            return false;
                        }
                    }

                    // Update the current material handle.
                    current_material = inst->material;
                }

                // Apply the per-draw (material instance)
                material_instance_draw_data instance_draw_data = {0};
                instance_draw_data.model = render_data->model;
                instance_draw_data.view_index = view_index;
                instance_draw_data.clipping_plane = clipping_plane;
                instance_draw_data.irradiance_cubemap_index = 0; // FIXME: Get this passed in as well.
                if (!material_system_apply_instance(internal_data->material_system, &render_data->material, instance_draw_data, p_frame_data)) {
                    KERROR("Failed to apply per-instance material data. See logs for details.");
                    return false;
                }

                // Invert if needed
                if (internal_data->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_CLOCKWISE);
                }

                // Draw it.
                renderer_geometry_draw(&internal_data->geometries[i]);

                // Change back if needed
                if (internal_data->geometries[i].winding_inverted) {
                    renderer_winding_set(RENDERER_WINDING_COUNTER_CLOCKWISE);
                }
            }
        }
    }

    // Edge case where no transparent/meshes were drawn, make sure the water is drawn (if it should be).
    if (include_water_plane && !water_drawn) {
        if (!render_water_planes(internal_data, internal_data->water_plane_count, internal_data->water_planes, colour, depth, vec4_zero(), cam, p_frame_data)) {
            KERROR("Failed to draw water plane! See logs for details.");
            return false;
        }
    }

    renderer_end_rendering(internal_data->renderer, p_frame_data);

    return true;
}

b8 forward_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    renderer_begin_debug_label(self->name, (vec3){1.0f, 0.5f, 0});

    // Create and use an inverted camera
    camera inverted_camera = camera_copy(*internal_data->current_camera);
    // Invert position across plane.
    f32 double_distance = 2.0f * (internal_data->current_camera->position.y - 0); // TODO: water plane position, distance along plane normal.
    vec3 inv_cam_pos = camera_position_get(&inverted_camera);
    inv_cam_pos.y -= double_distance; // TODO: invert along water plane normal axis.
    camera_position_set(&inverted_camera, inv_cam_pos);
    vec3 inv_cam_rot = camera_rotation_euler_get(&inverted_camera);
    inv_cam_rot.x *= -1.0f;
    camera_rotation_euler_set_radians(&inverted_camera, inv_cam_rot);

    // Render to reflect/refract textures for each plane.
    for (u32 i = 0; i < internal_data->water_plane_count; ++i) {
        water_plane* plane = internal_data->water_planes[i];

        // Refraction, clip above plane. Don't render the water plane itself. Uses bound camera
        // TODO: clipping plane should be based on position/orientation of water plane.
        vec4 refract_plane = (vec4){0, -1, 0, 0 + 1.0f}; // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.
        kresource_texture* refraction_colour = material_texture_get(internal_data->material_system, plane->material.material, MATERIAL_TEXTURE_INPUT_REFRACTION);
        kresource_texture* refraction_depth = material_texture_get(internal_data->material_system, plane->material.material, MATERIAL_TEXTURE_INPUT_REFRACTION_DEPTH);
        renderer_clear_colour(internal_data->renderer, refraction_colour->renderer_texture_handle);
        renderer_clear_depth_stencil(internal_data->renderer, refraction_depth->renderer_texture_handle);
        if (!render_scene(internal_data, refraction_colour, refraction_depth, 0, 0, false, refract_plane, internal_data->current_camera, &inverted_camera, false, p_frame_data)) {
            KERROR("Failed to render scene.");
            return false;
        }

        // Reflection, clip below plane. Don't render the water plane itself.
        kresource_texture* reflection_colour = material_texture_get(internal_data->material_system, plane->material.material, MATERIAL_TEXTURE_INPUT_REFLECTION);
        kresource_texture* reflection_depth = material_texture_get(internal_data->material_system, plane->material.material, MATERIAL_TEXTURE_INPUT_REFLECTION_DEPTH);
        renderer_clear_colour(internal_data->renderer, reflection_colour->renderer_texture_handle);
        renderer_clear_depth_stencil(internal_data->renderer, reflection_depth->renderer_texture_handle);
        vec4 reflect_plane = (vec4){0, 1, 0, 0}; // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.
        if (!render_scene(internal_data, reflection_colour, reflection_depth, 0, 0, false, reflect_plane, internal_data->current_camera, &inverted_camera, true, p_frame_data)) {
            KERROR("Failed to render scene.");
            return false;
        }

        renderer_texture_prepare_for_sampling(internal_data->renderer, reflection_colour->renderer_texture_handle, reflection_colour->flags);
        renderer_texture_prepare_for_sampling(internal_data->renderer, refraction_colour->renderer_texture_handle, refraction_colour->flags);
        renderer_texture_prepare_for_sampling(internal_data->renderer, refraction_depth->renderer_texture_handle, refraction_depth->flags);
    }

    // Finally, draw the scene normally with no clipping. Include the water plane rendering. Uses bound camera.
    vec4 clipping_plane = vec4_zero(); // NOTE: w is distance from origin, in this case the y-coord. Setting this to vec4_zero() effectively disables this.
    if (!render_scene(internal_data, internal_data->colourbuffer_texture, internal_data->depthbuffer_texture, internal_data->water_plane_count, internal_data->water_planes, true, clipping_plane, internal_data->current_camera, &inverted_camera, false, p_frame_data)) {
        KERROR("Failed to render scene.");
        return false;
    }

    renderer_end_debug_label();

    return true;
}

void forward_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self && self->internal_data) {
        kfree(self->internal_data, sizeof(forward_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
    }
}

void forward_rendergraph_node_reset(struct rendergraph_node* self) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->terrain_geometries = 0;
        internal_data->terrain_geometry_count = 0;
        internal_data->geometries = 0;
        internal_data->geometry_count = 0;
    }
}

b8 forward_rendergraph_node_render_mode_set(struct rendergraph_node* self, u32 render_mode) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->render_mode = render_mode;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_directional_light_set(struct rendergraph_node* self, const struct directional_light* light) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->dir_light = light;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_cascade_data_set(struct rendergraph_node* self, f32 split, mat4 dir_light_view, mat4 dir_light_projection, u8 cascade_index) {
    if (self && self->internal_data) {
        if (cascade_index >= MATERIAL_MAX_SHADOW_CASCADES) {
            KERROR("Shadow cascade index out of bounds: %d is not in range [0-%s]", cascade_index, MATERIAL_MAX_SHADOW_CASCADES - 1);
            return false;
        }
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->cascade_splits[cascade_index] = split;
        internal_data->directional_light_views[cascade_index] = dir_light_view;
        internal_data->directional_light_projections[cascade_index] = dir_light_projection;
        return true;
    }
    return false;
}

void forward_rendergraph_node_set_skybox(struct rendergraph_node* self, struct skybox* sb) {
    if (self) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->sb = sb;
    }
}

b8 forward_rendergraph_node_static_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->geometry_count = geometry_count;
        internal_data->geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
        kcopy_memory(internal_data->geometries, geometries, sizeof(geometry_render_data) * geometry_count);
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_terrain_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->terrain_geometry_count = geometry_count;
        internal_data->terrain_geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
        kcopy_memory(internal_data->terrain_geometries, geometries, sizeof(geometry_render_data) * geometry_count);
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_water_planes_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 count, struct water_plane** planes) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->water_plane_count = count;
        if (count > 0) {
            internal_data->water_planes = p_frame_data->allocator.allocate(sizeof(struct water_plane*) * count);
            kcopy_memory(internal_data->water_planes, planes, sizeof(struct water_plane*) * count);
        } else {
            internal_data->water_planes = 0;
        }
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_irradiance_texture_set(struct rendergraph_node* self, struct frame_data* p_frame_data, const struct kresource_texture* irradiance_cube_texture) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->irradiance_cube_texture = irradiance_cube_texture;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_viewport_set(struct rendergraph_node* self, viewport v) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->vp = v;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_camera_projection_set(struct rendergraph_node* self, struct camera* view_camera, mat4 projection_matrix) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->current_camera = view_camera;
        internal_data->projection_matrix = projection_matrix;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_register_factory(void) {
    rendergraph_node_factory factory = {0};
    factory.type = "forward";
    factory.create = forward_rendergraph_node_create;
    return rendergraph_system_node_factory_register(engine_systems_get()->rendergraph_system, &factory);
}
