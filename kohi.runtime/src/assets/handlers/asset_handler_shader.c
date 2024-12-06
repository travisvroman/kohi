#include "asset_handler_shader.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_shader_serializer.h>
#include <strings/kstring.h>

#include "assets/kasset_types.h"

void asset_handler_shader_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_shader_release_asset;
    self->type = KASSET_TYPE_SHADER;
    self->type_name = KASSET_TYPE_NAME_SHADER;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_shader_serialize;
    self->text_deserialize = kasset_shader_deserialize;
}

void asset_handler_shader_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_shader* typed_asset = (kasset_shader*)asset;
    // Stages
    if (typed_asset->stages && typed_asset->stage_count) {
        for (u32 i = 0; i < typed_asset->stage_count; ++i) {
            kasset_shader_stage* stage = &typed_asset->stages[i];
            if (stage->source_asset_name) {
                string_free(stage->source_asset_name);
            }
            if (stage->package_name) {
                string_free(stage->package_name);
            }
        }
        kfree(typed_asset->stages, sizeof(kasset_shader_stage*) * typed_asset->stage_count, MEMORY_TAG_ARRAY);
        typed_asset->stages = 0;
        typed_asset->stage_count = 0;
    }

    // Attributes
    if (typed_asset->attributes && typed_asset->attribute_count) {
        for (u32 i = 0; i < typed_asset->attribute_count; ++i) {
            kasset_shader_attribute* attrib = &typed_asset->attributes[i];
            if (attrib->name) {
                string_free(attrib->name);
            }
        }
        kfree(typed_asset->attributes, sizeof(kasset_shader_stage*) * typed_asset->attribute_count, MEMORY_TAG_ARRAY);
        typed_asset->attributes = 0;
        typed_asset->attribute_count = 0;
    }

    // Uniforms
    if (typed_asset->uniforms && typed_asset->uniform_count) {
        for (u32 i = 0; i < typed_asset->uniform_count; ++i) {
            kasset_shader_uniform* attrib = &typed_asset->uniforms[i];
            if (attrib->name) {
                string_free(attrib->name);
            }
        }
        kfree(typed_asset->uniforms, sizeof(kasset_shader_stage*) * typed_asset->uniform_count, MEMORY_TAG_ARRAY);
        typed_asset->uniforms = 0;
        typed_asset->uniform_count = 0;
    }
}
