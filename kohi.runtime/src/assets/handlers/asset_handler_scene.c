#include "asset_handler_scene.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <core_resource_types.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_scene_serializer.h>
#include <strings/kstring.h>

#include "assets/kasset_types.h"
#include "containers/darray.h"

void asset_handler_scene_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_scene_release_asset;
    self->type = KASSET_TYPE_SCENE;
    self->type_name = KASSET_TYPE_NAME_SCENE;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_scene_serialize;
    self->text_deserialize = kasset_scene_deserialize;
    self->size = sizeof(kasset_scene);
}

static void destroy_node(scene_node_config* node) {

    // Descroy attachments by type
    if (node->skybox_configs) {
        darray_destroy(node->skybox_configs);
        node->skybox_configs = 0;
    }

    if (node->dir_light_configs) {
        darray_destroy(node->dir_light_configs);
        node->dir_light_configs = 0;
    }
    if (node->point_light_configs) {
        darray_destroy(node->point_light_configs);
        node->point_light_configs = 0;
    }
    if (node->static_mesh_configs) {
        darray_destroy(node->static_mesh_configs);
        node->static_mesh_configs = 0;
    }
    if (node->heightmap_terrain_configs) {
        darray_destroy(node->heightmap_terrain_configs);
        node->heightmap_terrain_configs = 0;
    }
    if (node->water_plane_configs) {
        darray_destroy(node->water_plane_configs);
        node->water_plane_configs = 0;
    }

    // Destroy child nodes.
    for (u32 i = 0; i < node->child_count; ++i) {
        destroy_node(&node->children[i]);
    }
    kfree(node->children, sizeof(scene_node_config) * node->child_count, MEMORY_TAG_ARRAY);
    node->child_count = 0;
    node->children = 0;
}

void asset_handler_scene_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_scene* typed_asset = (kasset_scene*)asset;
    if (typed_asset->description) {
        string_free(typed_asset->description);
        typed_asset->description = 0;
    }
    if (typed_asset->node_count && typed_asset->nodes) {
        for (u32 i = 0; i < typed_asset->node_count; ++i) {
            destroy_node(&typed_asset->nodes[i]);
        }
        kfree(typed_asset->nodes, sizeof(scene_node_config) * typed_asset->node_count, MEMORY_TAG_ARRAY);
        typed_asset->nodes = 0;
        typed_asset->node_count = 0;
    }
}
