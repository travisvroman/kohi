#include "skybox.h"

#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/geometry.h"
#include "renderer/renderer_frontend.h"
#include "strings/kname.h"
#include "systems/shader_system.h"
#include "systems/texture_system.h"

b8 skybox_create(skybox_config config, skybox* out_skybox) {
    if (!out_skybox) {
        KERROR("skybox_create requires a valid pointer to out_skybox!");
        return false;
    }

    out_skybox->cubemap_name = kname_create(config.cubemap_name);
    out_skybox->state = SKYBOX_STATE_CREATED;
    out_skybox->cubemap = 0;

    return true;
}

b8 skybox_initialize(skybox* sb) {
    if (!sb) {
        KERROR("skybox_initialize requires a valid pointer to sb!");
        return false;
    }

    sb->group_id = INVALID_ID;

    sb->state = SKYBOX_STATE_INITIALIZED;

    return true;
}

b8 skybox_load(skybox* sb) {
    if (!sb) {
        KERROR("skybox_load requires a valid pointer to sb!");
        return false;
    }
    sb->state = SKYBOX_STATE_LOADING;

    sb->geometry = geometry_generate_cube(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, sb->cubemap_name);
    if (!renderer_geometry_upload(&sb->geometry)) {
        KERROR("Failed to upload skybox geometry.");
    }

    sb->cubemap = texture_system_request_cube(sb->cubemap_name, true, false, 0, 0);

    sb->render_frame_number = INVALID_ID_U64;

    khandle skybox_shader = shader_system_get(kname_create("Shader.DefaultSkybox")); // TODO: allow configurable shader.
    if (!renderer_shader_per_group_resources_acquire(engine_systems_get()->renderer_system, skybox_shader, &sb->group_id)) {
        KFATAL("Unable to acquire shader group resources for skybox.");
        return false;
    }
    sb->state = SKYBOX_STATE_LOADED;

    return true;
}

b8 skybox_unload(skybox* sb) {
    if (!sb) {
        KERROR("skybox_unload requires a valid pointer to sb!");
        return false;
    }
    sb->state = SKYBOX_STATE_UNDEFINED;

    khandle skybox_shader = shader_system_get(kname_create("Shader.DefaultSkybox")); // TODO: allow configurable shader.
    if (!renderer_shader_per_group_resources_release(engine_systems_get()->renderer_system, skybox_shader, sb->group_id)) {
        KWARN("Unable to release shader group resources for skybox.");
        return false;
    }

    sb->render_frame_number = INVALID_ID_U64;

    renderer_geometry_destroy(&sb->geometry);
    geometry_destroy(&sb->geometry);

    if (sb->cubemap_name) {
        if (sb->cubemap) {
            texture_system_release_resource((kresource_texture*)sb->cubemap);
            sb->cubemap = 0;
        }

        sb->cubemap_name = 0;
    }

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
    sb->state = SKYBOX_STATE_UNDEFINED;

    // If loaded, unload first, then destroy.
    if (sb->group_id != INVALID_ID) {
        b8 result = skybox_unload(sb);
        if (!result) {
            KERROR("skybox_destroy() - Failed to successfully unload skybox before destruction.");
        }
    }
}
