#include "skybox.h"

#include <defines.h>
#include <logger.h>
#include <math/geometry.h>

#include "renderer/renderer_frontend.h"
#include "systems/texture_system.h"

b8 skybox_create (skybox_config config, skybox *out_skybox) {
	if (!out_skybox) {
		KERROR("skybox_create requires a valid pointer to out_skybox!");
		return false;
	}

	out_skybox->cubemap_name = config.cubemap_name;
	out_skybox->state = SKYBOX_STATE_CREATED;
	out_skybox->cubemap = 0;

	return true;
}

b8 skybox_initialize (skybox *sb) {
	if (!sb) {
		KERROR("skybox_initialize requires a valid pointer to sb!");
		return false;
	}

	sb->shader_set0_instance_id = INVALID_ID;

	sb->state = SKYBOX_STATE_INITIALIZED;

	return true;
}

b8 skybox_load (skybox *sb) {
	if (!sb) {
		KERROR("skybox_load requires a valid pointer to sb!");
		return false;
	}
	sb->state = SKYBOX_STATE_LOADING;

	sb->geometry = geometry_generate_cube(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, sb->cubemap_name);
	if (!renderer_geometry_upload(&sb->geometry)) {
		KERROR("Failed to upload skybox geometry.");
	}

	sb->cubemap = texture_cubemap_acquire_sync(sb->cubemap_name);

	sb->state = SKYBOX_STATE_LOADED;

	return true;
}

b8 skybox_unload (skybox *sb) {
	if (!sb) {
		KERROR("skybox_unload requires a valid pointer to sb!");
		return false;
	}
	sb->state = SKYBOX_STATE_UNDEFINED;

	renderer_geometry_destroy(&sb->geometry);
	geometry_destroy(&sb->geometry);

	if (sb->cubemap_name) {
		if (sb->cubemap != INVALID_KTEXTURE) {
			texture_release(sb->cubemap);
			sb->cubemap = INVALID_KTEXTURE;
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
void skybox_destroy (skybox *sb) {
	if (!sb) {
		KERROR("skybox_destroy requires a valid pointer to sb!");
		return;
	}
	sb->state = SKYBOX_STATE_UNDEFINED;

	// If loaded, unload first, then destroy.
	if (sb->shader_set0_instance_id != INVALID_ID) {
		b8 result = skybox_unload(sb);
		if (!result) {
			KERROR("skybox_destroy() - Failed to successfully unload skybox before destruction.");
		}
	}
}
