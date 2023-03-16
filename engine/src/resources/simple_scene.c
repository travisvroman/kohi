#include "simple_scene.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "core/frame_data.h"
#include "containers/darray.h"
#include "math/transform.h"
#include "resources/resource_types.h"
#include "resources/skybox.h"
#include "resources/mesh.h"
#include "renderer/renderer_types.inl"
#include "systems/render_view_system.h"

#include "systems/light_system.h"

static u32 global_scene_id = 0;

b8 simple_scene_create(void* config, simple_scene* out_scene) {
    if (!out_scene) {
        KERROR(("simple_scene_create(): A valid pointer to out_scene is required."));
        return false;
    }

    kzero_memory(out_scene, sizeof(simple_scene));

    out_scene->enabled = false;
    out_scene->state = SIMPLE_SCENE_STATE_UNINITIALIZED;
    out_scene->scene_transform = transform_create();
    global_scene_id++;
    out_scene->id = global_scene_id;

    // Internal "lists" of renderable objects.
    out_scene->dir_light = 0;
    out_scene->point_lights = darray_create(point_light*);
    out_scene->meshes = darray_create(mesh*);
    out_scene->sb = 0;

    // TODO: process scene config.

    return true;
}

b8 simple_scene_destroy(simple_scene* scene) {
    if (!scene) {
        KERROR("simple_scene_destroy requires a valid pointer to a scene.");
        return false;
    }

    if (scene->state == SIMPLE_SCENE_STATE_LOADED) {
        b8 result = simple_scene_unload(scene);
        if (!result) {
            KERROR("simple_scene_destroy, failed to unload scene before destruction.");
            return false;
        }
    }

    if (scene->point_lights) {
        darray_destroy(scene->point_lights);
    }

    if (scene->meshes) {
        darray_destroy(scene->meshes);
    }

    kzero_memory(scene, sizeof(simple_scene));

    return true;
}

b8 simple_scene_initialize(simple_scene* scene) {
    if (!scene) {
        KERROR("simple_scene_initialize requires a valid pointer to a scene.");
        return false;
    }

    // TODO: Process configuration and setup hierarchy.

    if (scene->sb) {
        if (!skybox_initialize(scene->sb)) {
            KERROR("Skybox failed to initialize.");
            scene->sb = 0;
            return false;
        }
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (!mesh_initialize(scene->meshes[i])) {
            KERROR("Mesh failed to initialize.");
            return false;
        }
    }

    return true;
}

b8 simple_scene_load(simple_scene* scene) {
    if (!scene) {
        return false;
    }

    if (scene->sb) {
        if (scene->sb->instance_id == INVALID_ID) {
            if (!skybox_load(scene->sb)) {
                KERROR("Skybox failed to load.");
                scene->sb = 0;
                return false;
            }
        }
    }

    u32 mesh_count = darray_length(scene->meshes);
    for (u32 i = 0; i < mesh_count; ++i) {
        if (!mesh_load(scene->meshes[i])) {
            KERROR("Mesh failed to load.");
            return false;
        }
    }

    return true;
}

b8 simple_scene_unload(simple_scene* scene) {
    return true;
}

b8 simple_scene_update(simple_scene* scene, const struct frame_data* p_frame_data) {
    return true;
}

b8 simple_scene_populate_render_packet(simple_scene* scene, const struct frame_data* p_frame_data, struct render_packet* packet) {
    if (!scene || !packet) {
        return false;
    }

    // TODO: cache this somewhere so that we don't have to check this every time.
    for (u32 i = 0; i < packet->view_count; ++i) {
        render_view_packet* view_packet = &packet->views[i];
        const render_view* view = view_packet->view;
        if (view->type == RENDERER_VIEW_KNOWN_TYPE_SKYBOX) {
            // Skybox
            skybox_packet_data skybox_data = {};
            skybox_data.sb = scene->sb;
            if (!render_view_system_build_packet(view, p_frame_data->frame_allocator, &skybox_data, view_packet)) {
                KERROR("Failed to build packet for view 'skybox'.");
                return false;
            }
        }
    }

    return true;
}

b8 simple_scene_add_directional_light(simple_scene* scene, struct directional_light* light) {
    return true;
}

b8 simple_scene_add_point_light(simple_scene* scene, struct point_light* light) {
    return true;
}

b8 simple_scene_add_mesh(simple_scene* scene, struct mesh* m) {
    if (!scene || !m) {
        return false;
    }

    if (scene->state > SIMPLE_SCENE_STATE_INITIALIZED) {
        if (!mesh_initialize(m)) {
            KERROR("Mesh failed to initialize.");
            return false;
        }
    }

    if (scene->state > SIMPLE_SCENE_STATE_LOADED) {
        if (!mesh_load(m)) {
            KERROR("Mesh failed to load.");
            return false;
        }
    }

    darray_push(scene->meshes, m);

    return true;
}

b8 simple_scene_add_skybox(simple_scene* scene, struct skybox* sb) {
    if (!scene) {
        return false;
    }

    // TODO: if one already exists, do we do anything with it?
    scene->sb = sb;
    if (scene->state > SIMPLE_SCENE_STATE_INITIALIZED) {
        if (!skybox_initialize(sb)) {
            KERROR("Skybox failed to initialize.");
            scene->sb = 0;
            return false;
        }
    }

    if (scene->state > SIMPLE_SCENE_STATE_LOADED) {
        if (!skybox_load(sb)) {
            KERROR("Skybox failed to load.");
            scene->sb = 0;
            return false;
        }
    }

    return true;
}

b8 simple_scene_remove_directional_light(simple_scene* scene, struct directional_light* light) {
    return true;
}

b8 simple_scene_remove_point_light(simple_scene* scene, struct point_light* light) {
    return true;
}

b8 simple_scene_remove_mesh(simple_scene* scene, struct mesh* m) {
    return true;
}

b8 simple_scene_remove_skybox(simple_scene* scene, struct skybox* sb) {
    return true;
}