#include "skybox.h"

#include "core/logger.h"
#include "renderer/renderer_frontend.h"
#include "systems/geometry_system.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

b8 skybox_create(const char* cubemap_name, skybox* out_skybox) {
    texture_map* cube_map = &out_skybox->cubemap;
    cube_map->filter_magnify = cube_map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    cube_map->repeat_u = cube_map->repeat_v = cube_map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    cube_map->use = TEXTURE_USE_MAP_CUBEMAP;
    if (!renderer_texture_map_acquire_resources(cube_map)) {
        KFATAL("Unable to acquire resources for cube map texture.");
        return false;
    }
    cube_map->texture = texture_system_acquire_cube("skybox", true); // TODO: name is hardcoded.
    geometry_config skybox_cube_config = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, cubemap_name, 0);
    // Clear out the material name.
    skybox_cube_config.material_name[0] = 0;
    out_skybox->g = geometry_system_acquire_from_config(skybox_cube_config, true);
    out_skybox->render_frame_number = INVALID_ID_U64;
    shader* skybox_shader = shader_system_get("Shader.Builtin.Skybox"); // TODO: allow configurable shader.
    texture_map* maps[1] = {cube_map};
    if (!renderer_shader_acquire_instance_resources(skybox_shader, maps, &out_skybox->instance_id)) {
        KFATAL("Unable to acquire shader resources for skybox texture.");
        return false;
    }
    return true;
}

/**
 * @brief Destroys the provided skybox.
 *
 * @param sb A pointer to the skybox to be destroyed.
 */
void skybox_destroy(skybox* sb) {
    if (sb) {
        renderer_texture_map_release_resources(&sb->cubemap);
    }
}
