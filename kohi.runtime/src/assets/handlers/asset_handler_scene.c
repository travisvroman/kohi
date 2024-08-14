#include "asset_handler_scene.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_scene_serializer.h>
#include <strings/kstring.h>

#include "assets/kasset_types.h"
#include "systems/asset_system.h"
#include "systems/material_system.h"

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
}

static void destroy_node(kasset_scene_node* node) {

    if (node->name) {
        string_free(node->name);
        node->name = 0;
    }

    // Attachments
    for (u32 i = 0; i < node->attachment_count; ++i) {
        kasset_scene_node_attachment* attachment = &node->attachments[i];

        switch (attachment->type) {

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_SKYBOX: {
            kasset_scene_node_attachment_skybox* typed_attachment = (kasset_scene_node_attachment_skybox*)attachment;
            if (typed_attachment->cubemap_image_asset_name) {
                string_free(typed_attachment->cubemap_image_asset_name);
                typed_attachment->cubemap_image_asset_name = 0;
            }
        } break;

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT: {
            kasset_scene_node_attachment_directional_light* typed_attachment = (kasset_scene_node_attachment_directional_light*)attachment;
            kzero_memory(typed_attachment, sizeof(kasset_scene_node_attachment_directional_light));
        } break;

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT: {
            kasset_scene_node_attachment_point_light* typed_attachment = (kasset_scene_node_attachment_point_light*)attachment;
            kzero_memory(typed_attachment, sizeof(kasset_scene_node_attachment_point_light));
        } break;

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH: {
            kasset_scene_node_attachment_static_mesh* typed_attachment = (kasset_scene_node_attachment_static_mesh*)attachment;
            if (typed_attachment->asset_name) {
                string_free(typed_attachment->asset_name);
                typed_attachment->asset_name = 0;
            }
        } break;

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_HEIGHTMAP_TERRAIN: {
            kasset_scene_node_attachment_heightmap_terrain* typed_attachment = (kasset_scene_node_attachment_heightmap_terrain*)attachment;
            if (typed_attachment->asset_name) {
                string_free(typed_attachment->asset_name);
                typed_attachment->asset_name = 0;
            }
        } break;

        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_WATER_PLANE:
            // NOTE: Does not have any properties in need of disposal.
            break;
        case KASSET_SCENE_NODE_ATTACHMENT_TYPE_COUNT:
            // NOTE: Here to keep the compiler for bleating.
            break;
        }
    }
    kfree(node->attachments, sizeof(kasset_scene_node_attachment) * node->attachment_count, MEMORY_TAG_ARRAY);

    // Destroy child nodes.
    for (u32 i = 0; i < node->child_count; ++i) {
        destroy_node(&node->children[i]);
    }
    kfree(node->children, sizeof(kasset_scene_node) * node->child_count, MEMORY_TAG_ARRAY);
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
        kfree(typed_asset->nodes, sizeof(kasset_scene_node) * typed_asset->node_count, MEMORY_TAG_ARRAY);
        typed_asset->nodes = 0;
        typed_asset->node_count = 0;
    }
}
