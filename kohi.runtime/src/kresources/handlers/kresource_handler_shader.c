#include "kresource_handler_shader.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_shader_serializer.h>
#include <strings/kname.h>

#include "containers/darray.h"
#include "core/engine.h"
#include "core_render_types.h"
#include "kresources/kresource_types.h"
#include "strings/kstring.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"
#include "utils/render_type_utils.h"

typedef struct shader_resource_handler_info {
    kresource_shader* typed_resource;
    kresource_handler* handler;
    kresource_shader_request_info* request_info;
    kasset_shader* asset;
} shader_resource_handler_info;

static void shader_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);
static void asset_to_resource(const kasset_shader* asset, kresource_shader* out_shader);

b8 kresource_handler_shader_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_shader_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_shader* typed_resource = (kresource_shader*)resource;
    kresource_shader_request_info* typed_request = (kresource_shader_request_info*)info;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    // Ensure that there is either one asset _or_ a shader config source string.
    if (info->assets.base.length != 1) {
        if (info->assets.base.length == 0 && typed_request->shader_config_source_text) {
            // Deserialize shader asset from provided source.
            kasset_shader shader_from_source = {0};
            if (!kasset_shader_deserialize(typed_request->shader_config_source_text, (kasset*)&shader_from_source)) {
                KERROR("Failed to deserialize shader from direct source upon resource request.");
                return false;
            }

            asset_to_resource(&shader_from_source, typed_resource);
            return true;
        } else {
            KERROR("kresource_handler_shader_request requires exactly one asset OR zero assets and shader source text.");
            return false;
        }
    }

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    shader_resource_handler_info* listener_inst = kallocate(sizeof(shader_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_shader_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_shader_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->asset = 0;

    // Proceed straight to loading state.
    //   typed_resource->base.state = KRESOURCE_STATE_INITIALIZED;
    typed_resource->base.state = KRESOURCE_STATE_LOADING;

    // Request the shader config asset.
    asset_request_info request_info = {0};
    request_info.type = KASSET_TYPE_SHADER;
    request_info.asset_name = resource->name;
    request_info.package_name = INVALID_KNAME;
    request_info.auto_release = true;
    request_info.listener_inst = listener_inst;
    request_info.callback = shader_kasset_on_result;
    request_info.synchronous = typed_request->base.synchronous;
    request_info.import_params_size = 0;
    request_info.import_params = 0;

    asset_system_request(self->asset_system, request_info);

    return true;
}

void kresource_handler_shader_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_shader* typed_resource = (kresource_shader*)resource;

        if (typed_resource->attribute_count && typed_resource->attributes) {
            KFREE_TYPE_CARRAY(typed_resource->attributes, shader_attribute_config, typed_resource->attribute_count);
        }

        if (typed_resource->uniform_count && typed_resource->uniforms) {
            KFREE_TYPE_CARRAY(typed_resource->uniforms, shader_uniform_config, typed_resource->uniform_count);
        }

        if (typed_resource->stage_count && typed_resource->stage_configs) {
            KFREE_TYPE_CARRAY(typed_resource->stage_configs, shader_stage_config, typed_resource->stage_count);
        }
    }
}

static void shader_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    shader_resource_handler_info* listener = (shader_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        // Save off the asset pointer to the array.
        listener->asset = (kasset_shader*)asset;

        asset_to_resource(listener->asset, listener->typed_resource);
    } else {
        KERROR("Failed to load a required asset for shader resource '%s'. Resource may be incorrect.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_shader_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(shader_resource_handler_info), MEMORY_TAG_RESOURCE);
}

static void asset_to_resource(const kasset_shader* asset, kresource_shader* out_shader_resource) {
    // Take a copy of all of the asset properties.

    out_shader_resource->cull_mode = asset->cull_mode;
    out_shader_resource->max_groups = asset->max_groups;
    out_shader_resource->max_per_draw_count = asset->max_draw_ids;
    out_shader_resource->topology_types = asset->topology_types;

    // Attributes.
    out_shader_resource->attribute_count = asset->attribute_count;
    out_shader_resource->attributes = KALLOC_TYPE_CARRAY(shader_attribute_config, out_shader_resource->attribute_count);
    for (u32 i = 0; i < out_shader_resource->attribute_count; ++i) {
        kasset_shader_attribute* a = &asset->attributes[i];
        shader_attribute_config* config = &out_shader_resource->attributes[i];
        config->type = a->type;
        config->size = size_from_shader_attribute_type(a->type);
        config->name = kname_create(a->name);
    }

    // Uniforms
    out_shader_resource->uniform_count = asset->uniform_count;
    out_shader_resource->uniforms = KALLOC_TYPE_CARRAY(shader_uniform_config, out_shader_resource->uniform_count);
    for (u32 i = 0; i < out_shader_resource->uniform_count; ++i) {
        kasset_shader_uniform* u = &asset->uniforms[i];
        shader_uniform_config* config = &out_shader_resource->uniforms[i];
        config->type = u->type;
        if (config->type == SHADER_UNIFORM_TYPE_STRUCT || config->type == SHADER_UNIFORM_TYPE_CUSTOM) {
            config->size = u->size;
        } else {
            config->size = size_from_shader_uniform_type(u->type);
        }
        config->name = kname_create(u->name);
        config->array_length = u->array_size;
        config->frequency = u->frequency;
    }

    // Stages
    out_shader_resource->stage_count = asset->stage_count;
    out_shader_resource->stage_configs = KALLOC_TYPE_CARRAY(shader_stage_config, out_shader_resource->stage_count);
    for (u32 i = 0; i < out_shader_resource->stage_count; ++i) {
        kasset_shader_stage* a = &asset->stages[i];
        shader_stage_config* target = &out_shader_resource->stage_configs[i];
        target->stage = a->type;
        target->resource_name = kname_create(a->source_asset_name);
        target->package_name = kname_create(a->package_name);

        // Request the shader stage text resource from the resource system. Shader source files should be loaded as text.
        kresource_request_info request = {0};
        request.type = KRESOURCE_TYPE_TEXT;
        request.listener_inst = 0;
        request.user_callback = 0;
        request.synchronous = true; // Shader file requests need to be synchronous.

        // One text asset
        request.assets = array_kresource_asset_info_create(1);
        kresource_asset_info* asset_info = &request.assets.data[0];
        asset_info->type = KASSET_TYPE_TEXT;
        asset_info->package_name = target->package_name;
        asset_info->asset_name = target->resource_name;
        asset_info->watch_for_hot_reload = true;

        // Request the resource. Text resources are always loaded synchronously, so this is available immediately.
        kresource_text* text_resource = (kresource_text*)kresource_system_request(engine_systems_get()->kresource_state, target->resource_name, &request);
        if (!text_resource) {
            KERROR("Failed to properly request shader stage resource '%s' for shader '%s'.", kname_string_get(target->resource_name), kname_string_get(out_shader_resource->base.name));
            target->resource = 0;
            return;
        }
        if (text_resource->text) {
            target->resource = text_resource;
        } else {
            KWARN("Loaded shader source asset '%s' has no source.", kname_string_get(text_resource->base.name));
        }
    }

    // Build up flags.
    out_shader_resource->flags = SHADER_FLAG_NONE_BIT;
    if (asset->depth_test) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_DEPTH_TEST_BIT, true);
    }
    if (asset->depth_write) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_DEPTH_WRITE_BIT, true);
    }

    if (asset->stencil_test) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_STENCIL_TEST_BIT, true);
    }
    if (asset->stencil_write) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_STENCIL_WRITE_BIT, true);
    }

    if (asset->colour_read) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_COLOUR_READ_BIT, true);
    }
    if (asset->colour_write) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_COLOUR_WRITE_BIT, true);
    }

    if (asset->supports_wireframe) {
        out_shader_resource->flags = FLAG_SET(out_shader_resource->flags, SHADER_FLAG_WIREFRAME_BIT, true);
    }

    out_shader_resource->base.state = KRESOURCE_STATE_LOADED;
}
