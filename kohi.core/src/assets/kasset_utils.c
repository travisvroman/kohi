#include "kasset_utils.h"

#include "assets/asset_handler_types.h"
#include "assets/kasset_importer_registry.h"
#include "assets/kasset_types.h"
#include "debug/kassert.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/vfs.h"
#include "strings/kname.h"
#include "strings/kstring.h"

// Static lookup table for kasset type strings.
static const char* kasset_type_strs[KASSET_TYPE_MAX] = {
    "Unknown",          // KASSET_TYPE_UNKNOWN,
    "Image",            // KASSET_TYPE_IMAGE,
    "Material",         // KASSET_TYPE_MATERIAL,
    "StaticMesh",       // KASSET_TYPE_STATIC_MESH,
    "HeightmapTerrain", // KASSET_TYPE_HEIGHTMAP_TERRAIN,
    "BitmapFont",       // KASSET_TYPE_BITMAP_FONT,
    "SystemFont",       // KASSET_TYPE_SYSTEM_FONT,
    "Text",             // KASSET_TYPE_TEXT,
    "Binary",           // KASSET_TYPE_BINARY,
    "Kson",             // KASSET_TYPE_KSON,
    "VoxelTerrain",     // KASSET_TYPE_VOXEL_TERRAIN,
    "SkeletalMesh",     // KASSET_TYPE_SKELETAL_MESH,
    "Audio",            // KASSET_TYPE_AUDIO,
    "Music"             // KASSET_TYPE_MUSIC,
};

// Ensure changes to asset types break this if it isn't also updated.
STATIC_ASSERT(KASSET_TYPE_MAX == (sizeof(kasset_type_strs) / sizeof(*kasset_type_strs)), "Asset type count does not match string lookup table count.");

kasset_type kasset_type_from_string(const char* type_str) {
    for (u32 i = 0; i < KASSET_TYPE_MAX; ++i) {
        if (strings_equali(type_str, kasset_type_strs[i])) {
            return (kasset_type)i;
        }
    }
    KWARN("kasset_type_from_string: Unrecognized type '%s'. Returning unknown.");
    return KASSET_TYPE_UNKNOWN;
}

const char* kasset_type_to_string(kasset_type type) {
    KASSERT_MSG(type < KASSET_TYPE_MAX, "Provided kasset_type is not valid.");
    return string_duplicate(kasset_type_strs[type]);
}

void asset_handler_base_on_asset_loaded(struct vfs_state* vfs, vfs_asset_data asset_data) {

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
        KTRACE("Asset '%s' load from VFS successful.", kname_string_get(asset_data.asset_name));

        // Default to an internal failure.
        asset_request_result result = ASSET_REQUEST_RESULT_INTERNAL_FAILURE;

        // Check if the file was loaded as primary or from source.
        b8 from_source = (asset_data.flags & VFS_ASSET_FLAG_FROM_SOURCE) ? true : false;
        if (from_source) {
            KTRACE("Source asset loaded.");
            // Import it, write the binary version to disk and request the primary again.
            // Choose the importer by getting the file extension (minus the '.').
            const char* extension = string_extension_from_path(asset_data.path, false);
            if (!extension) {
                KERROR("No file extension is provided on source asset, thus an importer cannot be chosen.");
                result = ASSET_REQUEST_RESULT_NO_HANDLER;
                goto from_source_cleanup;
            }
            const kasset_importer* importer = kasset_importer_registry_get_for_source_type(context.asset->type, extension);
            if (!importer) {
                KERROR("No handler registered for extension '%s'.", extension);
                result = ASSET_REQUEST_RESULT_NO_HANDLER;
                goto from_source_cleanup;
            }

            context.asset->package_name = asset_data.package_name;
            context.asset->name = asset_data.asset_name;
            if (!importer->import(importer, asset_data.size, asset_data.bytes, asset_data.import_params, context.asset)) {
                KERROR("Automatic asset import failed. See logs for details.");
                result = ASSET_REQUEST_RESULT_AUTO_IMPORT_FAILED;
                goto from_source_cleanup;
            }

            if (context.handler->binary_serialize) {
                KTRACE("Using binary serialization to write primary asset.");
                // Serialize and write the binary typed asset out to disk.
                // No need to boot out if any of this fails since the import was successful.
                u64 binary_asset_data_size = 0;
                void* binary_asset_data = context.handler->binary_serialize(context.asset, &binary_asset_data_size);
                if (!binary_asset_data) {
                    KWARN("Failed to serialize asset data after automatic import. Binary asset won't be written to disk.");
                } else if (!vfs_asset_write(vfs, context.asset, true, binary_asset_data_size, binary_asset_data)) {
                    KWARN("Failed to write asset data to disk after automatic import.");
                }
                kfree(binary_asset_data, binary_asset_data_size, MEMORY_TAG_SERIALIZER);

                // TODO: Do we _really_ need to reload the asset at this time, since it is already loaded?
                // Technically it will just load the next time the asset is requested.
                result = ASSET_REQUEST_RESULT_SUCCESS;
            } else if (context.handler->text_serialize) {
                KTRACE("Using text serialization to write primary asset.");
                // Serialize and write the text typed asset out to disk.
                // No need to boot out if any of this fails since the import was successful.
                const char* text_asset_data = context.handler->text_serialize(context.asset);
                if (!text_asset_data) {
                    KWARN("Failed to serialize asset data after automatic import. Text asset won't be written to disk.");
                } else if (!vfs_asset_write(vfs, context.asset, false, string_length(text_asset_data), text_asset_data)) {
                    KWARN("Failed to write asset data to disk after automatic import.");
                }
                result = ASSET_REQUEST_RESULT_SUCCESS;
            } else {
                // Every handler must have some sort of serializer, even if it's not much more than a passthough.
                KERROR("No serializer configured for asset type '%s'.", kasset_type_to_string(context.asset->type));
            }

        from_source_cleanup:
            if (extension) {
                string_free(extension);
            }

        } else {
            KTRACE("Primary asset '%s' loaded.", kname_string_get(asset_data.asset_name));
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
            } else {
                // Every handler must have some sort of deserializer, even if it's not much more than a passthough.
                KERROR("No deserializer configured for asset type '%s'.", kasset_type_to_string(context.asset->type));
            }
        }

        // Take a copy of the file watch id.
        if (result == ASSET_REQUEST_RESULT_SUCCESS) {
            context.asset->file_watch_id = asset_data.file_watch_id;
        }

        // Send over the result.
        context.user_callback(result, context.asset, context.listener_instance);
    } else {
        // If primary file doesn't exist, try importing the source file instead.
        if (asset_data.result == VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST) {
            // Request the source asset. Can reuse the passed-in context.
            vfs_request_info request_info = {0};
            request_info.package_name = context.asset->package_name;
            request_info.asset_name = context.asset->name;
            request_info.is_binary = true;
            request_info.get_source = true;
            request_info.context_size = sizeof(asset_handler_request_context);
            request_info.context = &context;
            request_info.import_params_size = asset_data.import_params_size;
            request_info.import_params = asset_data.import_params;
            request_info.watch_for_hot_reload = asset_data.watch_for_hot_reload;
            request_info.vfs_callback = asset_handler_base_on_asset_loaded;
            // FIXME: If the original request was synchronous, this probably should be too.
            vfs_request_asset(vfs, request_info);
        } else if (asset_data.result == VFS_REQUEST_RESULT_SOURCE_FILE_DOES_NOT_EXIST) {
            KERROR("Source file does not exist to be imported. Asset handler failed to load anything for asset '%s'", kname_string_get(asset_data.asset_name));
            context.user_callback(ASSET_REQUEST_RESULT_VFS_REQUEST_FAILED, context.asset, context.listener_instance);
        }
    }
}

u8 channel_count_from_image_format(kasset_image_format format) {
    switch (format) {
    case KASSET_IMAGE_FORMAT_RGBA8:
        return 4;
    default:
        return 4;
    }
}
