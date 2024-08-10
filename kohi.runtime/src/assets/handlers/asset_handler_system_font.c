#include "asset_handler_system_font.h"
#include "assets/kasset_types.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_system_font_serializer.h>
#include <strings/kstring.h>

static void asset_handler_system_font_on_asset_loaded(struct vfs_state* vfs, const char* name, vfs_asset_data asset_data);

void asset_handler_system_font_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->request_asset = asset_handler_system_font_request_asset;
    self->release_asset = asset_handler_system_font_release_asset;
    self->type = KASSET_TYPE_SYSTEM_FONT;
    self->type_name = KASSET_TYPE_NAME_SYSTEM_FONT;
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = kasset_system_font_serialize;
    self->text_deserialize = kasset_system_font_deserialize;
}

void asset_handler_system_font_request_asset(struct asset_handler* self, struct kasset* asset, void* listener_instance, PFN_kasset_on_result user_callback) {
    struct vfs_state* vfs_state = engine_systems_get()->vfs_system_state;
    // Create and pass along a context.
    // NOTE: The VFS takes a copy of this context, so the lifecycle doesn't matter.
    asset_handler_request_context context = {0};
    context.asset = asset;
    context.handler = self;
    context.listener_instance = listener_instance;
    context.user_callback = user_callback;
    vfs_request_asset(vfs_state, &asset->meta, false, false, sizeof(asset_handler_request_context), &context, asset_handler_system_font_on_asset_loaded);
}

void asset_handler_system_font_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_system_font* typed_asset = (kasset_system_font*)asset;
    if (typed_asset->ttf_asset_name) {
        string_free(typed_asset->ttf_asset_name);
        typed_asset->ttf_asset_name = 0;
    }
    if (typed_asset->face_count && typed_asset->faces) {
        for (u32 i = 0; i < typed_asset->face_count; ++i) {
            const char* face_name = typed_asset->faces[i].name;
            if (face_name) {
                string_free(face_name);
                face_name = 0;
            }
        }
        kfree(typed_asset->faces, sizeof(const char*) * typed_asset->face_count, MEMORY_TAG_ARRAY);
        typed_asset->faces = 0;
        typed_asset->face_count = 0;
    }
}

static void asset_handler_system_font_on_asset_loaded(struct vfs_state* vfs, const char* name, vfs_asset_data asset_data) {
    /* asset_handler_base_on_asset_loaded(vfs, name, asset_data); */

    // This handler requires context.
    KASSERT_MSG(asset_data.context_size && asset_data.context, "asset_handler_base_on_asset_loaded requires valid context.");

    // Take a copy of the context first as it gets freed immediately upon return of this function.
    asset_handler_request_context context = *((asset_handler_request_context*)asset_data.context);

    // Process -
    // 0. Try to load binary asset first. If this succeeds then this is done.
    // 1. If binary load fails, check if there is a source_path defined for the asset. If not, this fails.
    // 2. If a source_path exists, check if there is an importer for this asset type/source file type. If not, this fails.
    // 3. If an importer exists, run it. If it fails, this fails.
    // 4. On success, attempt to load the binary asset again. Return result of that load request. NOTE: not currently doing this.

    if (asset_data.result == VFS_REQUEST_RESULT_SUCCESS) {
        KTRACE("Asset load from VFS successful.");

        // Default to an internal failure.
        asset_request_result result = ASSET_REQUEST_RESULT_INTERNAL_FAILURE;

        // Check if the file was loaded as primary or from source.
        b8 from_source = (asset_data.flags & VFS_ASSET_FLAG_FROM_SOURCE) ? true : false;
        if (from_source) {
            KERROR("There is no import process for system fonts. Secondary asset should not be used.");
        } else {
            KTRACE("Primary asset loaded.");
            // From primary file.
            // Deserialize directly. This either means that the primary asset already existed or was imported successfully.
            if (context.handler->binary_deserialize) {
                KTRACE("Using binary deserialization to read primary asset.");
                // Binary deserializaton.
                if (!context.handler->binary_deserialize(asset_data.size, asset_data.bytes, context.asset)) {
                    KERROR("Failed to deserialize binary asset data. Unable to fulfull asset request.");
                    result = ASSET_REQUEST_RESULT_PARSE_FAILED;
                } else {
                    result = ASSET_REQUEST_RESULT_SUCCESS;
                }
            } else if (context.handler->text_deserialize) {
                KTRACE("Using text deserialization to read primary asset.");
                // Text deserializaton
                if (!context.handler->text_deserialize(asset_data.text, context.asset)) {
                    KERROR("Failed to deserialize text asset data. Unable to fulfull asset request.");
                    result = ASSET_REQUEST_RESULT_PARSE_FAILED;
                } else {
                    result = ASSET_REQUEST_RESULT_SUCCESS;
                }
            }
        }
        // Load the ttf_asset_name (aka the font binary file)
        vfs_asset_data font_file_data = {0};
        kasset_metadata meta = {0};
        kasset_system_font* typed_asset = (kasset_system_font*)context.asset;
        // FIXME: Change away from names like this to just use the name provided.
        kasset_util_parse_name(string_format("Runtime.Binary.%s", typed_asset->ttf_asset_name), &meta.name);
        vfs_request_asset_sync(vfs, &context.asset->meta, true, false, 0, 0, &font_file_data);

        // Send over the result.
        context.user_callback(result, context.asset, context.listener_instance);
    } else {
        // If primary file doesn't exist, try importing the source file instead.
        if (asset_data.result == VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST) {
            KERROR("Failed to load primary asset. Operation failed.");
            context.user_callback(ASSET_REQUEST_RESULT_VFS_REQUEST_FAILED, context.asset, context.listener_instance);
        }
    }
}
