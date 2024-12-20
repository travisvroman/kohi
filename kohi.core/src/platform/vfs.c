#include "vfs.h"

#include "assets/kasset_types.h"
#include "containers/darray.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/filesystem.h"
#include "platform/kpackage.h"
#include "platform/platform.h"
#include "strings/kname.h"
#include "strings/kstring.h"

static b8 process_manifest_refs(vfs_state* state, const asset_manifest* manifest);
static void vfs_watcher_deleted_callback(u32 watcher_id, void* context);
static void vfs_watcher_written_callback(u32 watcher_id, void* context);

b8 vfs_initialize(u64* memory_requirement, vfs_state* state, const vfs_config* config) {
    if (!memory_requirement) {
        KERROR("vfs_initialize requires a valid pointer to memory_requirement.");
        return false;
    }

    *memory_requirement = sizeof(vfs_state);
    if (!state) {
        return true;
    } else if (!config) {
        KERROR("vfs_initialize requires a valid pointer to config.");
        return false;
    }

    state->packages = darray_create(kpackage);
    state->watched_assets = darray_create(vfs_asset_data);

    // TODO: For release builds, look at binary file.
    // FIXME: hardcoded rubbish. Add to app config, pass to config and read in here.
    const char* file_path = "../testbed.kapp/asset_manifest.kson";
    asset_manifest manifest = {0};
    if (!kpackage_parse_manifest_file_content(file_path, &manifest)) {
        KERROR("Failed to parse primary asset manifest. See logs for details.");
        return false;
    }

    kpackage primary_package = {0};
    if (!kpackage_create_from_manifest(&manifest, &primary_package)) {
        KERROR("Failed to create package from primary asset manifest. See logs for details.");
        return false;
    }

    darray_push(state->packages, primary_package);

    // Examine primary package references and load them as needed.
    if (!process_manifest_refs(state, &manifest)) {
        KERROR("Failed to process manifest reference. See logs for details.");
        kpackage_manifest_destroy(&manifest);
        return false;
    }

    kpackage_manifest_destroy(&manifest);

    // Register platform watcher callbacks.
    platform_register_watcher_deleted_callback(vfs_watcher_deleted_callback, state);
    platform_register_watcher_written_callback(vfs_watcher_written_callback, state);

    return true;
}

void vfs_shutdown(vfs_state* state) {
    if (state) {
        if (state->packages) {
            u32 package_count = darray_length(state->packages);
            for (u32 i = 0; i < package_count; ++i) {
                kpackage_destroy(&state->packages[i]);
            }
            darray_destroy(state->packages);
            state->packages = 0;
        }
    }
}

void vfs_hot_reload_callbacks_register(vfs_state* state, PFN_asset_hot_reloaded_callback hot_reloaded_callback, PFN_asset_deleted_callback deleted_callback) {
    if (state) {
        state->hot_reloaded_callback = hot_reloaded_callback;
        state->deleted_callback = deleted_callback;
    }
}

void vfs_request_asset(vfs_state* state, vfs_request_info info) {
    if (!state) {
        KERROR("vfs_request_asset requires state to be provided.");
    }

    // TODO: Jobify this call.
    vfs_asset_data data = vfs_request_asset_sync(state, info);

    // TODO: This should be the job result
    //
    // Issue the callback with the data, if present.
    if (info.vfs_callback) {
        info.vfs_callback(state, data);
    }

    // Cleanup context and import params if _not_ watching.
    if (!info.watch_for_hot_reload) {
        if (data.context && data.context_size) {
            kfree(data.context, data.context_size, MEMORY_TAG_PLATFORM);
            data.context = 0;
            data.context_size = 0;
        }
        if (data.import_params && data.import_params_size) {
            kfree(data.import_params, data.import_params_size, MEMORY_TAG_PLATFORM);
            data.import_params = 0;
            data.import_params_size = 0;
        }
    }
}

vfs_asset_data vfs_request_asset_sync(vfs_state* state, vfs_request_info info) {
    vfs_asset_data out_data = {0};

    if (!state) {
        KERROR("vfs_request_asset_sync requires state to be provided.");
        out_data.result = VFS_REQUEST_RESULT_INTERNAL_FAILURE;
        return out_data;
    }

    kzero_memory(&out_data, sizeof(vfs_asset_data));

    out_data.asset_name = info.asset_name;
    out_data.package_name = info.package_name;

    // Take a copy of the context if provided. This will need to be freed by the caller.
    if (info.context_size) {
        KASSERT_MSG(info.context, "Called vfs_request_asset with a context_size, but not a context. Check yourself before you wreck yourself.");
        out_data.context_size = info.context_size;
        out_data.context = kallocate(info.context_size, MEMORY_TAG_PLATFORM);
        kcopy_memory(out_data.context, info.context, out_data.context_size);
    } else {
        out_data.context_size = 0;
        out_data.context = 0;
    }

    // Take a copy of the import params. This will need to be freed by the caller.
    if (info.import_params_size) {
        KASSERT_MSG(info.context, "Called vfs_request_asset with a import_params_size, but not a import_params. Check yourself before you wreck yourself.");
        out_data.import_params_size = info.context_size;
        out_data.import_params = kallocate(info.context_size, MEMORY_TAG_PLATFORM);
        kcopy_memory(out_data.import_params, info.import_params, out_data.import_params_size);
    } else {
        out_data.import_params_size = 0;
        out_data.import_params = 0;
    }

    const char* asset_name_str = kname_string_get(info.asset_name);

    u32 package_count = darray_length(state->packages);
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];

        if (info.package_name == INVALID_KNAME || package->name == info.package_name) {

            const char* package_name_str = kname_string_get(package->name);
            KDEBUG("Attempting to load asset '%s' from package '%s'...", asset_name_str, package_name_str);

            // Determine if the asset type is text.
            kpackage_result result = KPACKAGE_RESULT_INTERNAL_FAILURE;
            if (info.is_binary) {
                result = kpackage_asset_bytes_get(package, info.asset_name, info.get_source, &out_data.size, &out_data.bytes);
                out_data.flags |= VFS_ASSET_FLAG_BINARY_BIT;
            } else {
                result = kpackage_asset_text_get(package, info.asset_name, info.get_source, &out_data.size, &out_data.text);
            }

            // Indicate this was loaded from source, if appropriate.
            if (info.get_source) {
                out_data.flags |= VFS_ASSET_FLAG_FROM_SOURCE;
            }

            // Translate the result to VFS layer and send on up.
            if (result != KPACKAGE_RESULT_SUCCESS) {
                KTRACE("Failed to load binary asset. See logs for details.");
                switch (result) {
                case KPACKAGE_RESULT_PRIMARY_GET_FAILURE:
                    out_data.result = VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST;
                    break;
                case KPACKAGE_RESULT_SOURCE_GET_FAILURE:
                    out_data.result = VFS_REQUEST_RESULT_SOURCE_FILE_DOES_NOT_EXIST;
                    break;
                default:
                case KPACKAGE_RESULT_INTERNAL_FAILURE:
                    out_data.result = VFS_REQUEST_RESULT_INTERNAL_FAILURE;
                    break;
                }
            } else {
                out_data.result = VFS_REQUEST_RESULT_SUCCESS;
                // Include a copy of the asset path.
                if (info.get_source) {
                    // Keep the package name in case an importer needs it later.
                    out_data.package_name = package->name;
                    out_data.path = kpackage_source_path_for_asset(package, info.asset_name);
                } else {
                    out_data.path = kpackage_path_for_asset(package, info.asset_name);
                }
            }

            // If set to watch, add to the list and watch.
            if (result == KPACKAGE_RESULT_SUCCESS && info.watch_for_hot_reload) {

                // Watch the asset.
                if (out_data.path) {
                    kpackage_asset_watch(package, out_data.path, &out_data.file_watch_id);
                    KTRACE("Watching asset for hot reload: package='%s', name='%s', file_watch_id=%u, path='%s'", package_name_str, kname_string_get(info.asset_name), out_data.file_watch_id, out_data.path);

                    darray_push(state->watched_assets, out_data);
                } else {
                    KERROR("Asset set to watch for hot reloading but not asset path is available.");
                }
            }

            // Boot out only if success OR looking through all packages (no package name provided)
            if (result == KPACKAGE_RESULT_SUCCESS || info.package_name != INVALID_KNAME) {
                return out_data;
            }
        }
    }

    KERROR("No asset named '%s' exists in any package. Nothing was done.", asset_name_str);
    // out_data->result = VFS_REQUEST_RESULT_PACKAGE_DOES_NOT_EXIST;
    return out_data;
}

const char* vfs_path_for_asset(vfs_state* state, kname package_name, kname asset_name) {
    u32 package_count = darray_length(state->packages);
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];

        if (package->name == package_name) {
            return kpackage_path_for_asset(package, asset_name);
        }
    }

    return 0;
}

const char* vfs_source_path_for_asset(vfs_state* state, kname package_name, kname asset_name) {
    u32 package_count = darray_length(state->packages);
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];

        if (package->name == package_name) {
            return kpackage_source_path_for_asset(package, asset_name);
        }
    }

    return 0;
}

void vfs_request_direct_from_disk(vfs_state* state, const char* path, b8 is_binary, u32 context_size, const void* context, PFN_on_asset_loaded_callback callback) {
    if (!state || !path || !callback) {
        KERROR("vfs_request_direct_from_disk requires state, path and callback to be provided.");
    }

    // TODO: Jobify this call.
    vfs_asset_data data = {0};
    vfs_request_direct_from_disk_sync(state, path, is_binary, context_size, context, &data);

    // TODO: This should be the job result
    // Issue the callback with the data.
    callback(state, data);

    // Cleanup the context.
    if (data.context && data.context_size) {
        kfree(data.context, data.context_size, MEMORY_TAG_PLATFORM);
        data.context = 0;
        data.context_size = 0;
    }
}

void vfs_request_direct_from_disk_sync(vfs_state* state, const char* path, b8 is_binary, u32 context_size, const void* context, vfs_asset_data* out_data) {
    if (!out_data) {
        KERROR("vfs_request_direct_from_disk_sync requires a valid pointer to out_data. Nothing can or will be done.");
        return;
    }
    if (!state || !path) {
        KERROR("VFS request direct from disk requires valid pointers to state, path and out_data.");
        return;
    }

    kzero_memory(out_data, sizeof(vfs_asset_data));

    char filename[512] = {0};
    string_filename_no_extension_from_path(filename, path);
    out_data->asset_name = kname_create(filename);
    out_data->package_name = 0;
    out_data->path = string_duplicate(path);

    // Take a copy of the context if provided. This will need to be freed by the caller.
    if (context_size) {
        KASSERT_MSG(context, "Called vfs_request_asset with a context_size, but not a context. Check yourself before you wreck yourself.");
        out_data->context_size = context_size;
        out_data->context = kallocate(context_size, MEMORY_TAG_PLATFORM);
        kcopy_memory(out_data->context, context, out_data->context_size);
    } else {
        out_data->context_size = 0;
        out_data->context = 0;
    }

    if (!filesystem_exists(path)) {
        KERROR("vfs_request_direct_from_disk_sync: File does not exist: '%s'.", path);
        out_data->result = VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST;
        return;
    }

    if (is_binary) {
        out_data->bytes = filesystem_read_entire_binary_file(path, &out_data->size);
        if (!out_data->bytes) {
            out_data->size = 0;
            KERROR("vfs_request_direct_from_disk_sync: Error reading from file: '%s'.", path);
            out_data->result = VFS_REQUEST_RESULT_READ_ERROR;
            return;
        }
        out_data->flags |= VFS_ASSET_FLAG_BINARY_BIT;
    } else {
        out_data->text = filesystem_read_entire_text_file(path);
        if (!out_data->text) {
            out_data->size = 0;
            KERROR("vfs_request_direct_from_disk_sync: Error reading from file: '%s'.", path);
            out_data->result = VFS_REQUEST_RESULT_READ_ERROR;
            return;
        }
        out_data->size = sizeof(char) * (string_length(out_data->text) + 1);
    }

    // Take a copy of the context if provided. This will be freed immediately after the callback is made below.
    // This means the context should be immediately consumed by the callback before any async
    // work is done.
    if (context_size) {
        KASSERT_MSG(context, "Called vfs_request_asset with a context_size, but not a context. Check yourself before you wreck yourself.");
        out_data->context_size = context_size;
        out_data->context = kallocate(context_size, MEMORY_TAG_PLATFORM);
        kcopy_memory(out_data->context, context, out_data->context_size);
    } else {
        out_data->context_size = 0;
        out_data->context = 0;
    }

    out_data->result = VFS_REQUEST_RESULT_SUCCESS;
}

b8 vfs_asset_write(vfs_state* state, const kasset* asset, b8 is_binary, u64 size, const void* data) {
    KASSERT_DEBUG(state);
    u32 package_count = darray_length(state->packages);
    if (asset->package_name == 0) {
        KERROR("Unable to write asset because it does not have a package name: '%s'.", kname_string_get(asset->name));
        return false;
    }
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];
        if (package->name == asset->package_name) {
            if (is_binary) {
                return kpackage_asset_bytes_write(package, asset->name, size, data);
            } else {
                return kpackage_asset_text_write(package, asset->name, size, data);
            }
        }
    }

    KERROR("vfs_asset_write: Unable to find package named '%s'.", kname_string_get(asset->package_name));
    return false;
}

static b8 process_manifest_refs(vfs_state* state, const asset_manifest* manifest) {
    b8 success = true;
    if (manifest->references) {
        u32 ref_count = darray_length(manifest->references);
        for (u32 i = 0; i < ref_count; ++i) {
            asset_manifest_reference* ref = &manifest->references[i];

            // Don't load the same package more than once.
            b8 exists = false;
            u32 package_count = darray_length(state->packages);
            for (u32 j = 0; j < package_count; ++j) {
                if (state->packages[j].name == ref->name) {
                    KTRACE("Package '%s' already loaded, skipping.", kname_string_get(ref->name));
                    exists = true;
                    break;
                }
                // TODO: Should probably also check the reference manifest's path against existing in case the name is wrong.
            }
            if (exists) {
                continue;
            }

            asset_manifest new_manifest = {0};
            const char* manifest_file_path = string_format("%sasset_manifest.kson", ref->path);
            b8 manifest_result = kpackage_parse_manifest_file_content(manifest_file_path, &new_manifest);
            string_free(manifest_file_path);
            if (!manifest_result) {
                KERROR("Failed to parse asset manifest. See logs for details.");
                return false;
            }

            kpackage package = {0};
            if (!kpackage_create_from_manifest(&new_manifest, &package)) {
                KERROR("Failed to create package from asset manifest. See logs for details.");
                return false;
            }

            darray_push(state->packages, package);

            // Process references.
            if (!process_manifest_refs(state, &new_manifest)) {
                KERROR("Failed to process manifest reference. See logs for details.");
                kpackage_manifest_destroy(&new_manifest);
                success = false;
                break;
            } else {
                kpackage_manifest_destroy(&new_manifest);
            }
        }
    }

    return success;
}

static void vfs_watcher_deleted_callback(u32 watcher_id, void* context) {
    vfs_state* state = (vfs_state*)context;

    if (!state->deleted_callback) {
        return;
    }

    u32 watched_asset_count = darray_length(state->watched_assets);
    for (u32 i = 0; i < watched_asset_count; ++i) {
        vfs_asset_data* asset_data = &state->watched_assets[i];
        if (asset_data->file_watch_id == watcher_id) {
            KTRACE("The VFS has been notified that the asset '%s' in package '%s' was deleted from disk.", kname_string_get(asset_data->asset_name), kname_string_get(asset_data->package_name));
            // Inform that the asset was deleted.
            state->deleted_callback(state, watcher_id);
            // TODO: Does the asset watch end here, or do we try to reinstate it if/when the asset comes back?
            break;
        }
    }
}

static void vfs_watcher_written_callback(u32 watcher_id, void* context) {
    vfs_state* state = (vfs_state*)context;

    if (!state->hot_reloaded_callback) {
        return;
    }

    u32 watched_asset_count = darray_length(state->watched_assets);
    for (u32 i = 0; i < watched_asset_count; ++i) {
        vfs_asset_data* asset_data = &state->watched_assets[i];
        if (asset_data->file_watch_id == watcher_id) {
            KTRACE("The VFS has been notified that the asset '%s' in package '%s' was updated on disk.", kname_string_get(asset_data->asset_name), kname_string_get(asset_data->package_name));
            b8 is_binary = FLAG_GET(asset_data->flags, VFS_ASSET_FLAG_BINARY_BIT);
            // Wipe out the old data.
            if (is_binary) {
                if (asset_data->bytes && asset_data->size) {
                    kfree((void*)asset_data->bytes, asset_data->size, MEMORY_TAG_ASSET);
                    asset_data->bytes = 0;
                }
            } else {
                if (asset_data->text && asset_data->size) {
                    kfree((void*)asset_data->text, asset_data->size, MEMORY_TAG_ASSET);
                    asset_data->text = 0;
                }
            }
            asset_data->size = 0;
            // Reload the asset synchronously
            vfs_request_direct_from_disk_sync(state, asset_data->path, is_binary, asset_data->context_size, asset_data->context, asset_data);

            // Inform that the asset has been hot-reloaded, passing along the new data.
            state->hot_reloaded_callback(state, asset_data);
            break;
        }
    }
}
