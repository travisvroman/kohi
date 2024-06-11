#include "forward_rendergraph_node.h"

#include "core/engine.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "renderer/renderer_types.h"
#include "renderer/viewport.h"
#include "strings/kstring.h"

// FIXME: Kinda dumb to have to include this to get MAX_SHADOW_CASCADE_COUNT...
#include "renderer/rendergraph_nodes/shadow_rendergraph_node.h"

#include "renderer/renderer_frontend.h"
#include "renderer/rendergraph.h"
#include "resources/resource_types.h"
#include "systems/light_system.h"
#include "systems/material_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

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

typedef struct pbr_shader_uniform_locations {
    u16 projection;
    u16 view;
    u16 cascade_splits;
    u16 view_position;
    u16 properties;
    u16 ibl_cube_texture;
    u16 material_texures;
    u16 shadow_textures;
    u16 light_space_0;
    u16 light_space_1;
    u16 light_space_2;
    u16 light_space_3;
    u16 model;
    u16 render_mode;
    u16 use_pcf;
    u16 bias;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;
} pbr_shader_uniform_locations;

typedef struct terrain_shader_locations {
    b8 loaded;
    u16 projection;
    u16 view;
    u16 cascade_splits;
    u16 view_position;
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
} terrain_shader_locations;

typedef struct debug_shader_locations {
    u16 projection;
    u16 view;
    u16 model;
} debug_shader_locations;

typedef struct forward_rendergraph_node_internal_data {
    struct renderer_system_state* renderer;
    /* forward_rendergraph_node_config config; */

    struct texture* colourbuffer_texture;
    struct texture* depthbuffer_texture;

    shader* pbr_shader;
    u32 pbr_shader_id;
    // Known locations for the PBR shader.
    pbr_shader_uniform_locations pbr_locations;

    shader* terrain_shader;
    u32 terrain_shader_id;
    // Known locations for terrain shader.
    terrain_shader_locations terrain_locations;

    shader* colour_shader;
    u32 colour_shader_id;
    // FIXME: Move debug shape rendering to another node.
    debug_shader_locations debug_locations;

    rendergraph_source* shadowmap_source;

    // Obtained from source.
    texture_map shadow_map;

    // Execution data

    u32 render_mode;
    viewport vp;

    mat4 view_matrix;
    mat4 projection_matrix;
    vec3 view_position;

    u32 geometry_count;
    struct geometry_render_data* geometries;

    u32 terrain_geometry_count;
    struct geometry_render_data* terrain_geometries;

    u32 debug_geometry_count;
    struct geometry_render_data* debug_geometries;

    struct texture* irradiance_cube_texture;
    const struct directional_light* dir_light;

    f32 cascade_splits[MAX_SHADOW_CASCADE_COUNT];
    mat4 directional_light_views[MAX_SHADOW_CASCADE_COUNT];
    mat4 directional_light_projections[MAX_SHADOW_CASCADE_COUNT];
    // The multiplied view/projections
    mat4 directional_light_spaces[MAX_SHADOW_CASCADE_COUNT];
} forward_rendergraph_node_internal_data;

b8 forward_rendergraph_node_create(struct rendergraph* graph, struct rendergraph_node* self, struct rendergraph_node_config* config) {
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
            break;
        } else if (strings_equali("depthbuffer", sink->name)) {
            depthbuffer_sink_config = sink;
            break;
        } else if (strings_equali("shadow", sink->name)) {
            shadow_sink_config = sink;
            break;
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

    // Setup the colourbuffer source.
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

    // Save off a pointer to the PBR shader as well as its uniform locations.
    internal_data->pbr_shader = shader_system_get("Shader.PBRMaterial");
    internal_data->pbr_locations.projection = shader_system_uniform_location(internal_data->pbr_shader_id, "projection");
    internal_data->pbr_locations.view = shader_system_uniform_location(internal_data->pbr_shader_id, "view");
    internal_data->pbr_locations.light_space_0 = shader_system_uniform_location(internal_data->pbr_shader_id, "light_space_0");
    internal_data->pbr_locations.light_space_1 = shader_system_uniform_location(internal_data->pbr_shader_id, "light_space_1");
    internal_data->pbr_locations.light_space_2 = shader_system_uniform_location(internal_data->pbr_shader_id, "light_space_2");
    internal_data->pbr_locations.light_space_3 = shader_system_uniform_location(internal_data->pbr_shader_id, "light_space_3");
    internal_data->pbr_locations.cascade_splits = shader_system_uniform_location(internal_data->pbr_shader_id, "cascade_splits");
    internal_data->pbr_locations.view_position = shader_system_uniform_location(internal_data->pbr_shader_id, "view_position");
    internal_data->pbr_locations.properties = shader_system_uniform_location(internal_data->pbr_shader_id, "properties");
    internal_data->pbr_locations.material_texures = shader_system_uniform_location(internal_data->pbr_shader_id, "material_textures");
    internal_data->pbr_locations.shadow_textures = shader_system_uniform_location(internal_data->pbr_shader_id, "shadow_textures");
    internal_data->pbr_locations.ibl_cube_texture = shader_system_uniform_location(internal_data->pbr_shader_id, "ibl_cube_texture");
    internal_data->pbr_locations.model = shader_system_uniform_location(internal_data->pbr_shader_id, "model");
    internal_data->pbr_locations.render_mode = shader_system_uniform_location(internal_data->pbr_shader_id, "mode");
    internal_data->pbr_locations.dir_light = shader_system_uniform_location(internal_data->pbr_shader_id, "dir_light");
    internal_data->pbr_locations.p_lights = shader_system_uniform_location(internal_data->pbr_shader_id, "p_lights");
    internal_data->pbr_locations.num_p_lights = shader_system_uniform_location(internal_data->pbr_shader_id, "num_p_lights");
    internal_data->pbr_locations.use_pcf = shader_system_uniform_location(internal_data->pbr_shader_id, "use_pcf");
    internal_data->pbr_locations.bias = shader_system_uniform_location(internal_data->pbr_shader_id, "bias");

    // Save off a pointer to the terrain shader as well as its uniform locations.
    internal_data->terrain_shader = shader_system_get("Shader.Builtin.Terrain");
    internal_data->terrain_locations.projection = shader_system_uniform_location(internal_data->terrain_shader_id, "projection");
    internal_data->terrain_locations.view = shader_system_uniform_location(internal_data->terrain_shader_id, "view");
    internal_data->terrain_locations.light_space_0 = shader_system_uniform_location(internal_data->terrain_shader_id, "light_space_0");
    internal_data->terrain_locations.light_space_1 = shader_system_uniform_location(internal_data->terrain_shader_id, "light_space_1");
    internal_data->terrain_locations.light_space_2 = shader_system_uniform_location(internal_data->terrain_shader_id, "light_space_2");
    internal_data->terrain_locations.light_space_3 = shader_system_uniform_location(internal_data->terrain_shader_id, "light_space_3");
    internal_data->terrain_locations.cascade_splits = shader_system_uniform_location(internal_data->terrain_shader_id, "cascade_splits");
    internal_data->terrain_locations.view_position = shader_system_uniform_location(internal_data->terrain_shader_id, "view_position");
    internal_data->terrain_locations.model = shader_system_uniform_location(internal_data->terrain_shader_id, "model");
    internal_data->terrain_locations.render_mode = shader_system_uniform_location(internal_data->terrain_shader_id, "mode");
    internal_data->terrain_locations.dir_light = shader_system_uniform_location(internal_data->terrain_shader_id, "dir_light");
    internal_data->terrain_locations.p_lights = shader_system_uniform_location(internal_data->terrain_shader_id, "p_lights");
    internal_data->terrain_locations.num_p_lights = shader_system_uniform_location(internal_data->terrain_shader_id, "num_p_lights");
    internal_data->terrain_locations.properties = shader_system_uniform_location(internal_data->terrain_shader_id, "properties");
    internal_data->terrain_locations.material_texures = shader_system_uniform_location(internal_data->terrain_shader_id, "material_textures");
    internal_data->terrain_locations.shadow_textures = shader_system_uniform_location(internal_data->terrain_shader_id, "shadow_textures");
    internal_data->terrain_locations.ibl_cube_texture = shader_system_uniform_location(internal_data->terrain_shader_id, "ibl_cube_texture");
    internal_data->terrain_locations.use_pcf = shader_system_uniform_location(internal_data->terrain_shader_id, "use_pcf");
    internal_data->terrain_locations.bias = shader_system_uniform_location(internal_data->terrain_shader_id, "bias");

    // Load debug colour3d shader.
    // FIXME: Move to another node.
    //
    // Save off a pointer to the colour3d shader as well as its uniform locations.
    internal_data->colour_shader = shader_system_get("Shader.Builtin.ColourShader3D");
    internal_data->debug_locations.projection = shader_system_uniform_location(internal_data->colour_shader_id, "projection");
    internal_data->debug_locations.view = shader_system_uniform_location(internal_data->colour_shader_id, "view");
    internal_data->debug_locations.model = shader_system_uniform_location(internal_data->colour_shader_id, "model");

    // Grab the default cubemap texture as the irradiance texture.
    internal_data->irradiance_cube_texture = texture_system_get_default_cube_texture();

    // Assign some defaults.
    for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
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
        return true;
    }
    if (!internal_data->shadowmap_source) {
        KERROR("Required '%s' source not hooked up to forward pass. Creation fails.", "shadowmap");
        return false;
    }

    // Need a texture map (i.e. sampler) to use the shadowmap source texture.
    texture_map* sm = &internal_data->shadow_map;
    sm->repeat_u = sm->repeat_v = sm->repeat_w = TEXTURE_REPEAT_CLAMP_TO_BORDER;
    sm->filter_minify = sm->filter_magnify = TEXTURE_FILTER_MODE_LINEAR;
    // TODO: Might think about moving this elsewhere and handling in a more generic way.
    sm->texture = internal_data->shadowmap_source->value.t;
    sm->generation = INVALID_ID_U8;

    if (!renderer_texture_map_resources_acquire(sm)) {
        KERROR("Failed to acquire texture map resources for shadow map in forward pass. Initialize failed.");
        return false;
    }

    return true;
}

b8 forward_rendergraph_node_execute(struct rendergraph_node* self, struct frame_data* p_frame_data) {
    if (!self) {
        return false;
    }
    forward_rendergraph_node_internal_data* internal_data = self->internal_data;

    // Bind the viewport
    renderer_active_viewport_set(&internal_data->vp);

    // Begin rendering
    renderer_begin_rendering(internal_data->renderer, p_frame_data, 1, &internal_data->colourbuffer_texture->renderer_texture_handle, internal_data->depthbuffer_texture->renderer_texture_handle);

    // Calculate light-space matrices for each shadow cascade.
    for (u8 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
        internal_data->directional_light_spaces[i] = mat4_mul(internal_data->directional_light_views[i], internal_data->directional_light_projections[i]);
    }

    // Use the appropriate shader and apply the global uniforms.
    u32 terrain_geometry_count = internal_data->terrain_geometry_count;
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
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.view, &internal_data->view_matrix));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.view_position, &internal_data->view_position));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.render_mode, &internal_data->render_mode));
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.cascade_splits, &internal_data->cascade_splits));

            // Light space for shadow mapping. Per cascade
            for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.light_space_0 + i, &internal_data->directional_light_spaces[i]));
            }

            // Directional light - global for this shader..
            directional_light_data default_dir_light_data = {0};
            const directional_light_data* dir_light_data = &internal_data->dir_light->data;
            if (!dir_light_data) {
                dir_light_data = &default_dir_light_data;
            }
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.dir_light, dir_light_data));

            // Global shader options.
            b8 use_pcf = renderer_pcf_enabled(internal_data->renderer);
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.use_pcf, &use_pcf));

            // HACK: Read this in from somewhere (or have global setter?);
            f32 bias = 0.00005f;
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.bias, &bias));

            // Apply/upload them to the GPU
            if (!shader_system_apply_global(internal_data->terrain_shader_id)) {
                KERROR("Failed to apply global uniforms.");
                return false;
            }
        }

        // Handle each terrain chunk
        for (u32 i = 0; i < terrain_geometry_count; ++i) {
            material* m = internal_data->terrain_geometries[i].material;
            if (!m) {
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
                if (!shader_system_bind_instance(internal_data->terrain_shader_id, m->internal_id)) {
                    KERROR("Failed to bind instance for material: %d", m->internal_id);
                    return false;
                }

                // Apply material maps, all as one layered texture.
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.material_texures, &m->maps[TERRAIN_SAMP_IDX_MATERIAL_ARRAY_MAP]));

                // NOTE: apply other maps separately.

                // Shadow Maps TODO: Should this be global?
                texture* shadow_map_texture = internal_data->shadowmap_source->value.t;
                m->maps[TERRAIN_SAMP_IDX_SHADOW_MAP].texture = shadow_map_texture ? shadow_map_texture : texture_system_get_default_diffuse_texture();
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
                if (!shader_system_apply_instance(internal_data->terrain_shader_id)) {
                    KERROR("Failed to apply instance-level uniforms for material '%s'.", m->name);
                }
            }

            // Apply the locals
            {
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.model, &internal_data->terrain_geometries[i].model));
                shader_system_apply_local(internal_data->terrain_shader_id);
            }

            // Draw it.
            renderer_geometry_draw(&internal_data->terrain_geometries[i]);
        }
    }

    // Static geometries.
    {
        u32 geometry_count = internal_data->geometry_count;
        if (geometry_count > 0) {
            // Update globals for material and PBR shaders.
            if (!shader_system_use_by_id(internal_data->pbr_shader_id)) {
                KERROR("Failed to use PBR shader. Render frame failed.");
                return false;
            }

            if (internal_data->render_mode == RENDERER_VIEW_MODE_WIREFRAME) {
                shader_system_set_wireframe(internal_data->pbr_shader_id, true);
            } else {
                shader_system_set_wireframe(internal_data->pbr_shader_id, false);
            }

            // Apply globals
            {
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->terrain_locations.projection, &internal_data->projection_matrix));
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->terrain_locations.view, &internal_data->view_matrix));
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->terrain_locations.view_position, &internal_data->view_position));
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->terrain_locations.render_mode, &internal_data->render_mode));
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.cascade_splits, &internal_data->cascade_splits));

                // Light space for shadow mapping. Per cascade
                for (u32 i = 0; i < MAX_SHADOW_CASCADE_COUNT; ++i) {
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.light_space_0 + i, &internal_data->directional_light_spaces[i]));
                }

                // Global shader options.
                b8 use_pcf = renderer_pcf_enabled(internal_data->renderer);
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.use_pcf, &use_pcf));

                // HACK: Read this in from somewhere (or have global setter?);
                f32 bias = 0.00005f;
                UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.bias, &bias));

                // Apply/upload them to the GPU
                if (!shader_system_apply_global(internal_data->pbr_shader_id)) {
                    KERROR("Failed to apply global uniforms.");
                    return false;
                }
            }

            u32 current_material_id = INVALID_ID - 1;
            // Draw geometries.
            u32 count = internal_data->geometry_count;
            for (u32 i = 0; i < count; ++i) {
                material* m = internal_data->geometries[i].material;
                if (!m) {
                    m = material_system_get_default();
                }

                // Only rebind/update the material if it's a new material. Duplicates can reuse the already-bound material.
                if (m->internal_id != current_material_id) {
                    // Properties
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.properties, m->properties));
                    // Maps
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->pbr_shader_id, internal_data->pbr_locations.material_texures, PBR_SAMP_IDX_ALBEDO, &m->maps[PBR_SAMP_IDX_ALBEDO]));
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->pbr_shader_id, internal_data->pbr_locations.material_texures, PBR_SAMP_IDX_NORMAL, &m->maps[PBR_SAMP_IDX_NORMAL]));
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location_arrayed(internal_data->pbr_shader_id, internal_data->pbr_locations.material_texures, PBR_SAMP_IDX_COMBINED, &m->maps[PBR_SAMP_IDX_COMBINED]));

                    // Shadow Maps
                    texture* shadow_map_texture = internal_data->shadowmap_source->value.t;
                    m->maps[PBR_SAMP_IDX_SHADOW_MAP].texture = shadow_map_texture ? shadow_map_texture : texture_system_get_default_diffuse_texture();
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->terrain_shader_id, internal_data->terrain_locations.shadow_textures, &m->maps[PBR_SAMP_IDX_SHADOW_MAP]));

                    // Irradience map - use the material-assigned one if exists, otherwise use the "global" assigned one.
                    m->maps[PBR_SAMP_IDX_IRRADIANCE_MAP].texture = m->irradiance_texture ? m->irradiance_texture : internal_data->irradiance_cube_texture;
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->terrain_locations.ibl_cube_texture, &m->maps[PBR_SAMP_IDX_IRRADIANCE_MAP]));

                    // Directional light.
                    directional_light* dir_light = light_system_directional_light_get();
                    if (dir_light) {
                        UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.dir_light, &dir_light->data));
                    } else {
                        directional_light_data data = {0};
                        UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.dir_light, &data));
                    }
                    // Point lights.
                    u32 p_light_count = light_system_point_light_count();
                    if (p_light_count) {
                        point_light* p_lights = p_frame_data->allocator.allocate(sizeof(point_light) * p_light_count);
                        light_system_point_lights_get(p_lights);

                        point_light_data* p_light_datas = p_frame_data->allocator.allocate(sizeof(point_light_data) * p_light_count);
                        for (u32 i = 0; i < p_light_count; ++i) {
                            p_light_datas[i] = p_lights[i].data;
                        }

                        UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.p_lights, p_light_datas));
                    }

                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.num_p_lights, &p_light_count));

                    UNIFORM_APPLY_OR_FAIL(shader_system_apply_instance(internal_data->pbr_shader_id));

                    // Update the current material id.
                    current_material_id = m->id;
                }

                // Apply the locals
                {
                    UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->pbr_shader_id, internal_data->pbr_locations.model, &internal_data->geometries[i].model));
                    shader_system_apply_local(internal_data->pbr_shader_id);
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

    // Debug geometries (i.e. grids, lines, boxes, gizmos, etc.)
    // This goes through the same geometry system as anything else.
    u32 debug_geometry_count = internal_data->debug_geometry_count;
    if (debug_geometry_count > 0) {
        shader_system_use_by_id(internal_data->colour_shader_id);

        // Globals
        UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.projection, &internal_data->projection_matrix));
        UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.view, &internal_data->view_matrix));

        if (!shader_system_apply_global(internal_data->colour_shader_id)) {
            KERROR("Failed to apply globals for colour shader.");
            return false;
        }

        // Each geometry.
        for (u32 i = 0; i < debug_geometry_count; ++i) {
            // NOTE: No instance-level uniforms to be set.

            // Local
            UNIFORM_APPLY_OR_FAIL(shader_system_uniform_set_by_location(internal_data->colour_shader_id, internal_data->debug_locations.model, &internal_data->debug_geometries[i].model));
            if (!shader_system_apply_local(internal_data->colour_shader_id)) {
                KERROR("Failed to apply locals for colour shader.");
            }

            // Draw it.
            renderer_geometry_draw(&internal_data->debug_geometries[i]);
        }
    }

    renderer_end_rendering(internal_data->renderer, p_frame_data);

    return true;
}

void forward_rendergraph_node_destroy(struct rendergraph_node* self) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;

        // Destroy the texture maps/samplers.
        renderer_texture_map_resources_release(&internal_data->shadow_map);

        kfree(self->internal_data, sizeof(forward_rendergraph_node_internal_data), MEMORY_TAG_RENDERER);
        self->internal_data = 0;
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
        if (cascade_index >= MAX_SHADOW_CASCADE_COUNT) {
            KERROR("Shadow cascade index out of bounds: %d is not in range [0-%s]", cascade_index, MAX_SHADOW_CASCADE_COUNT - 1);
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

b8 forward_rendergraph_node_debug_geometries_set(struct rendergraph_node* self, struct frame_data* p_frame_data, u32 geometry_count, const struct geometry_render_data* geometries) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->debug_geometry_count = geometry_count;
        internal_data->debug_geometries = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * geometry_count);
        kcopy_memory(internal_data->debug_geometries, geometries, sizeof(geometry_render_data) * geometry_count);
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_irradiance_texture_set(struct rendergraph_node* self, struct frame_data* p_frame_data, struct texture* irradiance_cube_texture) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->irradiance_cube_texture = irradiance_cube_texture;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_viewport_set(struct rendergraph_node* self, struct viewport* v) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->vp = *v;
        return true;
    }
    return false;
}

b8 forward_rendergraph_node_view_projection_set(struct rendergraph_node* self, mat4 view_matrix, vec3 view_pos, mat4 projection_matrix) {
    if (self && self->internal_data) {
        forward_rendergraph_node_internal_data* internal_data = self->internal_data;
        internal_data->view_matrix = view_matrix;
        internal_data->view_position = view_pos;
        internal_data->projection_matrix = projection_matrix;
        return true;
    }
    return false;
}
