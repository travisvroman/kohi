#include "skybox.h"

#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "renderer/renderer_frontend.h"
#include "systems/geometry_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

b8 skybox_create(skybox_config config, skybox* out_skybox) {
    if (!out_skybox) {
        KERROR("skybox_create requires a valid pointer to out_skybox!");
        return false;
    }

    out_skybox->config = config;

    return true;
}

b8 skybox_initialize(skybox* sb) {
    if (!sb) {
        KERROR("skybox_initialize requires a valid pointer to sb!");
        return false;
    }
    texture_map* cube_map = &sb->cubemap;
    cube_map->filter_magnify = cube_map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    cube_map->repeat_u = cube_map->repeat_v = cube_map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;

    sb->instance_id = INVALID_ID;

    sb->config.g_config = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, sb->config.cubemap_name, 0);
    // Clear out the material name.
    sb->config.g_config.material_name[0] = 0;

    return true;
}

b8 skybox_load(skybox* sb) {
    if (!sb) {
        KERROR("skybox_load requires a valid pointer to sb!");
        return false;
    }

    sb->cubemap.texture = texture_system_acquire_cube(sb->config.cubemap_name, true);
    if (!renderer_texture_map_resources_acquire(&sb->cubemap)) {
        KFATAL("Unable to acquire resources for cube map texture.");
        return false;
    }

    sb->g = geometry_system_acquire_from_config(sb->config.g_config, true);
    sb->render_frame_number = INVALID_ID_U64;

    shader* skybox_shader = shader_system_get("Shader.Builtin.Skybox");  // TODO: allow configurable shader.
    texture_map* maps[1] = {&sb->cubemap};
    shader* s = skybox_shader;
    u16 atlas_location = s->uniforms[s->instance_sampler_indices[0]].index;
    shader_instance_resource_config instance_resource_config = {0};
    // Map count for this type is known.
    shader_instance_uniform_texture_config colour_texture = {0};
    colour_texture.uniform_location = atlas_location;
    colour_texture.texture_map_count = 1;
    colour_texture.texture_maps = maps;

    instance_resource_config.uniform_config_count = 1;
    instance_resource_config.uniform_configs = &colour_texture;
    if (!renderer_shader_instance_resources_acquire(skybox_shader, &instance_resource_config, &sb->instance_id)) {
        KFATAL("Unable to acquire shader resources for skybox texture.");
        return false;
    }

    return true;
}

b8 skybox_unload(skybox* sb) {
    if (!sb) {
        KERROR("skybox_unload requires a valid pointer to sb!");
        return false;
    }

    shader* skybox_shader = shader_system_get("Shader.Builtin.Skybox");  // TODO: allow configurable shader.
    renderer_shader_instance_resources_release(skybox_shader, sb->instance_id);
    sb->instance_id = INVALID_ID;
    renderer_texture_map_resources_release(&sb->cubemap);

    sb->render_frame_number = INVALID_ID_U64;

    geometry_system_config_dispose(&sb->config.g_config);
    if (sb->config.cubemap_name) {
        if (sb->cubemap.texture) {
            texture_system_release(sb->config.cubemap_name);
            sb->cubemap.texture = 0;
        }

        // u32 length = string_length(sb->config.cubemap_name);
        // kfree((void*)sb->config.cubemap_name, (length + 1) * sizeof(char), MEMORY_TAG_STRING);
        sb->config.cubemap_name = 0;
    }

    geometry_system_release(sb->g);

    return true;
}

/**
 * @brief Destroys the provided skybox.
 *
 * @param sb A pointer to the skybox to be destroyed.
 */
void skybox_destroy(skybox* sb) {
    if (!sb) {
        KERROR("skybox_destroy requires a valid pointer to sb!");
        return;
    }

    // If loaded, unload first, then destroy.
    if (sb->instance_id != INVALID_ID) {
        b8 result = skybox_unload(sb);
        if (!result) {
            KERROR("skybox_destroy() - Failed to successfully unload skybox before destruction.");
        }
    }
}
