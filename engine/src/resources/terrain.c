#include "terrain.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "core/kmemory.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "math/geometry_utils.h"
#include "renderer/renderer_frontend.h"
#include "renderer/renderer_types.inl"
#include "systems/shader_system.h"
#include "systems/light_system.h"

typedef struct terrain_shader_locations {
    b8 loaded;
    u16 projection;
    u16 view;
    u16 ambient_colour;
    u16 view_position;
    u16 shininess;
    u16 diffuse_colour;
    u16 diffuse_texture;
    u16 specular_texture;
    u16 normal_texture;
    u16 model;
    u16 render_mode;
    u16 dir_light;
    u16 p_lights;
    u16 num_p_lights;
} terrain_shader_locations;

// NOTE: Might want to hold this state elsewhere in the future.
static terrain_shader_locations terrain_locations;

b8 terrain_create(terrain_config config, terrain* out_terrain) {
    if (!out_terrain) {
        KERROR("terrain_create requires a valid pointer to out_terrain.");
        return false;
    }

    out_terrain->config = config;
    out_terrain->name = string_duplicate(config.name);

    return true;
}
void terrain_destroy(terrain* t) {
    // TODO: Fill me out!
}

b8 terrain_initialize(terrain* t) {
    if (!t) {
        KERROR("terrain_initialize requires a valid pointer to a terrain!");
        return false;
    }

    if (!t->config.tile_count_x) {
        KERROR("Tile count x cannot be less than one.");
        return false;
    }

    if (!t->config.tile_count_z) {
        KERROR("Tile count z cannot be less than one.");
        return false;
    }

    t->xform = transform_create();
    t->extents = (extents_3d){0};
    t->origin = vec3_zero();

    t->tile_count_x = t->config.tile_count_x;
    t->tile_count_z = t->config.tile_count_z;
    t->tile_scale_x = t->config.tile_scale_x;
    t->tile_scale_z = t->config.tile_scale_z;

    t->vertex_count = t->tile_count_x * t->tile_count_z;
    t->vertices = kallocate(sizeof(terrain_vertex) * t->vertex_count, MEMORY_TAG_ARRAY);

    t->index_count = t->vertex_count * 6;
    t->indices = kallocate(sizeof(u32) * t->index_count, MEMORY_TAG_ARRAY);

    t->material_count = t->config.material_count;
    t->materials = kallocate(sizeof(material_config) * t->material_count, MEMORY_TAG_ARRAY);

    // Generate vertices.
    for (u32 z = 0; z < t->tile_count_z; z++) {
        for (u32 x = 0, i = 0; x < t->tile_count_x; ++x, ++i) {
            terrain_vertex* v = &t->vertices[i];
            v->position.x = x * t->tile_scale_x;
            v->position.z = z * t->tile_scale_z;
            v->position.y = 0.0f;  // <-- this will be modified by a heightmap.

            v->colour = vec4_one();       // white;
            v->normal = (vec3){0, 1, 0};  // TODO: calculate based on geometry.
            v->texcoord.x = (f32)x;
            v->texcoord.y = (f32)z;
            
            // TODO: Materials
            //kzero_memory(v->material_weights, sizeof(f32) * TERRAIN_MAX_MATERIAL_COUNT);
            //v->material_weights[0] = 1.0f;
            v->tangent = vec3_zero();  // TODO: obviously wrong.
        }
    }

    // Generate indices.
    for (u32 z = 0; z < t->tile_count_z - 1; z++) {
        for (u32 x = 0, i = 0; x < t->tile_count_x - 1; ++x, i += 6) {
            u32 v0 = (z * t->tile_count_x) + x;
            u32 v1 = (z * t->tile_count_x) + x + 1;
            u32 v2 = ((z + 1) * t->tile_count_x) + x;
            u32 v3 = ((z + 1) * t->tile_count_x) + x + 1;

            // v0, v1, v2, v2, v1, v3
            t->indices[i + 0] = v0;
            t->indices[i + 1] = v1;
            t->indices[i + 2] = v2;
            t->indices[i + 3] = v2;
            t->indices[i + 4] = v1;
            t->indices[i + 5] = v3;
        }
    }

    return true;
}

b8 terrain_load(terrain* t) {
    if (!t) {
        KERROR("terrain_load requires a valid pointer to a terrain, ya dingus!");
        return false;
    }

    geometry* g = &t->geo;
    kzero_memory(g, sizeof(geometry));
    g->generation = INVALID_ID_U16;

    // Send the geometry off to the renderer to be uploaded to the GPU.
    if (!renderer_create_geometry(g, sizeof(terrain_vertex), t->vertex_count, t->vertices, sizeof(u32), t->index_count, t->indices)) {
        return false;
    }

    // Copy over extents, center, etc.
    g->center = t->origin;
    g->extents.min = t->extents.min;
    g->extents.max = t->extents.max;
    g->generation++;

    // TODO: acquire material(s)
    // Acquire the material
    // if (string_length(config.material_name) > 0) {
    //     g->material = material_system_acquire(config.material_name);
    //     if (!g->material) {
    //         g->material = material_system_get_default();
    //     }
    // }


    // Acquire locations.
    shader* s = shader_system_get("Shader.Builtin.Terrain");
    terrain_locations.projection = shader_system_uniform_index(s, "projection");
    terrain_locations.view = shader_system_uniform_index(s, "view");
    terrain_locations.ambient_colour = shader_system_uniform_index(s, "ambient_colour");
    terrain_locations.view_position = shader_system_uniform_index(s, "view_position");
    terrain_locations.diffuse_colour = shader_system_uniform_index(s, "diffuse_colour");
    terrain_locations.diffuse_texture = shader_system_uniform_index(s, "diffuse_texture");
    terrain_locations.specular_texture = shader_system_uniform_index(s, "specular_texture");
    terrain_locations.normal_texture = shader_system_uniform_index(s, "normal_texture");
    terrain_locations.shininess = shader_system_uniform_index(s, "shininess");
    terrain_locations.model = shader_system_uniform_index(s, "model");
    terrain_locations.render_mode = shader_system_uniform_index(s, "mode");
    terrain_locations.dir_light = shader_system_uniform_index(s, "dir_light");
    terrain_locations.p_lights = shader_system_uniform_index(s, "p_lights");
    terrain_locations.num_p_lights = shader_system_uniform_index(s, "num_p_lights");


    return true;
}
b8 terrain_unload(terrain* t) {
    return true;
}

b8 terrain_update(terrain* t) {
    return true;
}

b8 terrain_render(terrain* t, frame_data* p_frame_data, const mat4* projection, const mat4* view, const mat4* model, const vec4* ambient_colour, const vec3* view_position, u32 render_mode) {

    if(!t) {
        KERROR("terrain_render requires a valid pointer to terrain.");
        return false;
    }


    shader* s = shader_system_get("Builtin.TerrainShader");
    if(!s) {
        KERROR("Unable to obtain terrain shader.");
        return false;
    }
    shader_system_use_by_id(s->id);

    // Apply uniforms, all are global for terrains.
    shader_system_uniform_set_by_index(terrain_locations.projection, projection);
    shader_system_uniform_set_by_index(terrain_locations.view, view);
    shader_system_uniform_set_by_index(terrain_locations.ambient_colour, ambient_colour);
    shader_system_uniform_set_by_index(terrain_locations.view_position, view_position);
    shader_system_uniform_set_by_index(terrain_locations.render_mode, &render_mode);

    
    // TODO: hardcoded temp
    vec4 white = vec4_one();
    shader_system_uniform_set_by_index(terrain_locations.diffuse_colour, &white);
    //shader_system_uniform_set_by_index(state_ptr->material_locations.diffuse_texture, &m->diffuse_map);
    //shader_system_uniform_set_by_index(state_ptr->material_locations.specular_texture, &m->specular_map);
    //shader_system_uniform_set_by_index(state_ptr->material_locations.normal_texture, &m->normal_map);
    // TODO: hardcoded temp
    f32 shininess = 32.0f;
    shader_system_uniform_set_by_index(terrain_locations.shininess, &shininess);

    // Directional light.
    directional_light* dir_light = light_system_directional_light_get();
    if (dir_light) {
        shader_system_uniform_set_by_index(terrain_locations.dir_light, &dir_light->data);
    } else {
        directional_light_data data = {0};
        shader_system_uniform_set_by_index(terrain_locations.dir_light, &data);
    }
    // Point lights.
    u32 p_light_count = light_system_point_light_count();
    if (p_light_count) {
        // TODO: frame allocator?
        point_light* p_lights = kallocate(sizeof(point_light) * p_light_count, MEMORY_TAG_ARRAY);
        light_system_point_lights_get(p_lights);

        point_light_data* p_light_datas = kallocate(sizeof(point_light_data) * p_light_count, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < p_light_count; ++i) {
            p_light_datas[i] = p_lights[i].data;
        }

        shader_system_uniform_set_by_index(terrain_locations.p_lights, p_light_datas);
        kfree(p_light_datas, sizeof(point_light_data), MEMORY_TAG_ARRAY);
        kfree(p_lights, sizeof(point_light), MEMORY_TAG_ARRAY);
    }

    shader_system_uniform_set_by_index(terrain_locations.num_p_lights, &p_light_count);

    shader_system_uniform_set_by_index(terrain_locations.model, &model);

    shader_system_apply_global();

    geometry_render_data render_data = {0};
    render_data.geometry = &t->geo;
    renderer_draw_terrain_geometry(&render_data);

    return true;
}
