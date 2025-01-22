#include "kresource_handler_audio.h"
#include "assets/kasset_types.h"
#include "audio/audio_frontend.h"
#include "containers/array.h"
#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"
#include "utils/audio_utils.h"

typedef struct audio_resource_handler_info {
    kresource_audio* typed_resource;
    kresource_handler* handler;
    kresource_audio_request_info* request_info;
    u32 loaded_count;
} audio_resource_handler_info;

static void audio_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst);

kresource* kresource_handler_audio_allocate(void) {
    return (kresource*)kallocate(sizeof(kresource_audio), MEMORY_TAG_RESOURCE);
}

b8 kresource_handler_audio_request(struct kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_audio_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_audio* typed_resource = (kresource_audio*)resource;
    kresource_audio_request_info* typed_request = (kresource_audio_request_info*)info;

    // NOTE: dynamically allocating this so lifetime isn't a concern.
    audio_resource_handler_info* listener_inst = kallocate(sizeof(audio_resource_handler_info), MEMORY_TAG_RESOURCE);
    // Take a copy of the typed request info.
    listener_inst->request_info = kallocate(sizeof(kresource_audio_request_info), MEMORY_TAG_RESOURCE);
    kcopy_memory(listener_inst->request_info, typed_request, sizeof(kresource_audio_request_info));
    listener_inst->typed_resource = typed_resource;
    listener_inst->handler = self;
    listener_inst->loaded_count = 0;

    if (info->assets.base.length != 1) {
        KERROR("kresource_handler_audio requires exactly one asset. Request failed.");
        return false;
    }

    // Load the asset.
    kresource_asset_info* asset_info = &info->assets.data[0];
    if (asset_info->type == KASSET_TYPE_AUDIO) {
        asset_request_info request_info = {0};
        request_info.type = asset_info->type;
        request_info.asset_name = asset_info->asset_name;
        request_info.package_name = asset_info->package_name;
        request_info.auto_release = true;
        request_info.listener_inst = listener_inst;
        request_info.callback = audio_kasset_on_result;
        request_info.synchronous = false;
        request_info.import_params_size = 0;
        request_info.import_params = 0;

        asset_system_request(self->asset_system, request_info);
    } else {
        KERROR("kresource_handler_audio asset must be of audio type");
        return false;
    }

    return true;
}

void kresource_handler_audio_release(struct kresource_handler* self, kresource* resource) {
    if (resource) {
        if (resource->type != KRESOURCE_TYPE_AUDIO) {
            KERROR("Attempted to release non-audio resource '%s' via audio resource handler. Resource not released.");
            return;
        }

        kresource_audio* t = (kresource_audio*)resource;
        if (t->pcm_data_size && t->pcm_data) {
            kfree(t->pcm_data, t->pcm_data_size, MEMORY_TAG_AUDIO);
        }
        t->pcm_data = 0;
        t->pcm_data_size = 0;
        if (t->mono_pcm_data && t->downmixed_size) {
            kfree(t->mono_pcm_data, t->total_sample_count * sizeof(i16), MEMORY_TAG_AUDIO);
        }
        t->mono_pcm_data = 0;
        t->downmixed_size = 0;

        // TODO: release backend data
    }
}

static void audio_kasset_on_result(asset_request_result result, const struct kasset* asset, void* listener_inst) {
    audio_resource_handler_info* listener = (audio_resource_handler_info*)listener_inst;
    if (result == ASSET_REQUEST_RESULT_SUCCESS) {
        kasset_audio* typed_asset = (kasset_audio*)asset;
        // Convert asset to resource.
        listener->typed_resource->channels = typed_asset->channels;
        listener->typed_resource->sample_rate = typed_asset->sample_rate;
        listener->typed_resource->total_sample_count = typed_asset->total_sample_count;
        listener->typed_resource->pcm_data_size = typed_asset->pcm_data_size;
        listener->typed_resource->pcm_data = kallocate(listener->typed_resource->pcm_data_size, MEMORY_TAG_AUDIO);
        kcopy_memory(listener->typed_resource->pcm_data, typed_asset->pcm_data, listener->typed_resource->pcm_data_size);
        // If the asset is stereo, get a downmixed version of the audio so it can be used
        // as a "2D" sound if need be.
        if (listener->typed_resource->channels == 2) {
            listener->typed_resource->mono_pcm_data = kaudio_downmix_stereo_to_mono(listener->typed_resource->pcm_data, listener->typed_resource->total_sample_count);
            listener->typed_resource->downmixed_size = listener->typed_resource->total_sample_count * sizeof(i16);
        } else {
            // Asset was already mono, just point to the pcm data.
            listener->typed_resource->mono_pcm_data = listener->typed_resource->pcm_data;
            listener->typed_resource->downmixed_size = 0; // Set to zero to indicate this shouldn't be freed separately.
        }

        listener->typed_resource->base.state = KRESOURCE_STATE_LOADED;

        // Invoke the user callback if provided.
        if (listener->request_info->base.user_callback) {
            listener->request_info->base.user_callback((kresource*)listener->typed_resource, listener->request_info->base.listener_inst);
        }

        // Release the asset reference as we are done with it.
        asset_system_release(engine_systems_get()->asset_state, asset->name, asset->package_name);
    } else {
        KERROR("Failed to load a required asset for audio resource '%s'. Resource may not work correctly when used.", kname_string_get(listener->typed_resource->base.name));
    }

    // Destroy the request.
    array_kresource_asset_info_destroy(&listener->request_info->base.assets);
    kfree(listener->request_info, sizeof(kresource_audio_request_info), MEMORY_TAG_RESOURCE);
    // Free the listener itself.
    kfree(listener, sizeof(audio_resource_handler_info), MEMORY_TAG_RESOURCE);
}
