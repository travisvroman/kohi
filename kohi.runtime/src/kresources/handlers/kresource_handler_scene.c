#include "kresource_handler_scene.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_scene_serializer.h>
#include <strings/kname.h>

#include "containers/darray.h"
#include "core_resource_types.h"
#include "kresources/kresource_types.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

typedef struct scene_resource_handler_info {
    kresource_scene* typed_resource;
    kresource_handler* handler;
    kresource_scene_request_info* request_info;
    kasset_scene* asset;
} scene_resource_handler_info;

static void scene_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_scene* asset, kresource_scene* out_scene_resource);

kresource* kresource_handler_scene_allocate(void) {
    return (kresource*)KALLOC_TYPE(kresource_scene, MEMORY_TAG_RESOURCE);
}

b8 kresource_handler_scene_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_scene_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_scene* typed_resource = (kresource_scene*)resource;
    kresource_scene_request_info* typed_request = (kresource_scene_request_info*)info;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length == 0) {
        KERROR("kresource_handler_scene_request requires exactly one asset.");
        return false;
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    scene_resource_handler_info* listener_inst = kallocate(sizeof(scene_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_scene_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_scene_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->asset = 0;

    // Proceed straight to loading state.
    //   typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    // Request the scene config asset.
    kresource_asset_info* asset = &info->assets.data[0];

    asset_request_info request_info = {0};
    request_info.type = KASSET_TYPE_SCENE;
    request_info.asset_name = asset->asset_name;
    request_info.package_name = asset->package_name;
    request_info.auto_release = true;
    request_info.listener_inst = listener_inst;
    request_info.callback = scene_kasset_on_result;
    request_info.synchronous = typed_request->base.synchronous;
    request_info.hot_reload_callback = 0; // Don't need hot-reloading on the scene config.
    request_info.hot_reload_context = 0;
    request_info.import_params_size = 0;
    request_info.import_params = 0;

    asset_system_request(self->asset_system, request_info);

    return true;
}

static void destroy_scene_node(scene_node_config* root) {
    if (root) {

        if (root->xform_source) {
            string_free(root->xform_source);
            root->xform_source = 0;
        }

        // Attachment configs.
        {
            if (root->skybox_configs) {
                darray_destroy(root->skybox_configs);
                root->skybox_configs = 0;
            }
            if (root->dir_light_configs) {
                darray_destroy(root->dir_light_configs);
                root->dir_light_configs = 0;
            }
            if (root->point_light_configs) {
                darray_destroy(root->point_light_configs);
                root->point_light_configs = 0;
            }
            if (root->static_mesh_configs) {
                darray_destroy(root->static_mesh_configs);
                root->static_mesh_configs = 0;
            }
            if (root->heightmap_terrain_configs) {
                darray_destroy(root->heightmap_terrain_configs);
                root->heightmap_terrain_configs = 0;
            }
            if (root->water_plane_configs) {
                darray_destroy(root->water_plane_configs);
                root->water_plane_configs = 0;
            }
        }

        if (root->xform_source) {
            string_free(root->xform_source);
            root->xform_source = 0;
        }

        if (root->children && root->child_count) {
            for (u32 i = 0; i < root->child_count; ++i) {
                destroy_scene_node(&root->children[i]);
            }
            KFREE_TYPE_CARRAY(root->children, scene_node_config, root->child_count);
            root->child_count = 0;
            root->children = 0;
        }
    }
}

void kresource_handler_scene_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_scene* typed_resource = (kresource_scene*)resource;

        if (typed_resource->nodes && typed_resource->node_count) {
            for (u32 i = 0; i < typed_resource->node_count; ++i) {
                destroy_scene_node(&typed_resource->nodes[i]);
            }
        }
    }
}

static void scene_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    scene_resource_handler_info* listener = (scene_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Save off the asset pointer to the array.
        listener->asset = (kasset_scene*)asset;

        asset_to_resource(listener->asset, listener->typed_resource);
    } else {
        KERROR("Failed to load a required asset for scene resource '%s'. Resource may be incorrect.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_scene_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(scene_resource_handler_info), MEMORY_TAG_RESOURCE);
}

static void copy_scene_node(const scene_node_config* source, scene_node_config* target) {

    // Take a copy of attachment configs.
    {
        if (source->skybox_configs) {
            target->skybox_configs = darray_duplicate(scene_node_attachment_skybox_config, source->skybox_configs);
        }
        if (source->dir_light_configs) {
            target->dir_light_configs = darray_duplicate(scene_node_attachment_directional_light_config, source->dir_light_configs);
        }
        if (source->point_light_configs) {
            target->point_light_configs = darray_duplicate(scene_node_attachment_point_light_config, source->point_light_configs);
        }
        if (source->static_mesh_configs) {
            target->static_mesh_configs = darray_duplicate(scene_node_attachment_static_mesh_config, source->static_mesh_configs);
        }
        if (source->heightmap_terrain_configs) {
            target->heightmap_terrain_configs = darray_duplicate(scene_node_attachment_heightmap_terrain_config, source->heightmap_terrain_configs);
        }
        if (source->water_plane_configs) {
            target->water_plane_configs = darray_duplicate(scene_node_attachment_water_plane_config, source->water_plane_configs);
        }
    }

    target->child_count = source->child_count;
    if (source->child_count && source->children) {
        target->children = KALLOC_TYPE_CARRAY(scene_node_config, target->child_count);
        for (u32 i = 0; i < source->child_count; ++i) {
            copy_scene_node(&source->children[i], &target->children[i]);
        }
    }

    if (source->xform_source) {
        target->xform_source = string_duplicate(source->xform_source);
    }
}

static void asset_to_resource(const kasset_scene* asset, kresource_scene* out_scene_resource) {
    // Take a copy of all of the asset properties.

    if (asset->description) {
        out_scene_resource->description = string_duplicate(asset->description);
    }

    out_scene_resource->node_count = asset->node_count;

    if (asset->nodes && asset->node_count) {
        out_scene_resource->nodes = KALLOC_TYPE_CARRAY(scene_node_config, asset->node_count);

        for (u32 i = 0; i < asset->node_count; ++i) {
            copy_scene_node(&asset->nodes[i], &out_scene_resource->nodes[i]);
        }
    }

    out_scene_resource->base.state = KRESOURCE_STATE_LOADED;
}
