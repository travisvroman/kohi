#include "vfs.h"
#include "core/event.h"

#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <debug/kassert.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <platform/kpackage.h>
#include <platform/platform.h>
#include <strings/kname.h>
#include <strings/kstring.h>
#include <systems/job_system.h>

static b8 process_manifest_refs(vfs_state* state, const asset_manifest* manifest);

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

    // TODO: For release builds, look at binary file.
    asset_manifest manifest = {0};
    if (!kpackage_parse_manifest_file_content(config->manifest_file_path, &manifest)) {
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

typedef struct vfs_asset_job_params {
    vfs_state* state;
    vfs_request_info info;
} vfs_asset_job_params;

typedef struct vfs_asset_job_result {
    vfs_state* state;
    vfs_asset_data data;
    vfs_request_info info;
} vfs_asset_job_result;

b8 vfs_asset_job_start(void* params, void* out_result_data) {
    vfs_asset_job_params* job_params = (vfs_asset_job_params*)params;
    vfs_asset_job_result* out_result = (vfs_asset_job_result*)out_result_data;
    out_result->data = vfs_request_asset_sync(job_params->state, job_params->info);
    out_result->state = job_params->state;
    out_result->info = job_params->info;

    return out_result->data.result == VFS_REQUEST_RESULT_SUCCESS;
}

// Invoked on asset job success.
void vfs_asset_job_success(void* result_params) {
    vfs_asset_job_result* result = result_params;

    // Issue the callback with the data, if present.
    if (result->info.vfs_callback) {
        result->info.vfs_callback(result->state, result->data);
    }

    // Cleanup context.
    if (result->data.context && result->data.context_size) {
        kfree(result->data.context, result->data.context_size, MEMORY_TAG_PLATFORM);
        result->data.context = 0;
        result->data.context_size = 0;
    }
}

// Invoked on asset job failure.
void vfs_asset_job_fail(void* result_params) {
    vfs_asset_job_result* result = result_params;

    // FIXME: notify?
    KERROR("VFS asset (name='%s', package='%s') load failed. See logs for details.", kname_string_get(result->info.asset_name), kname_string_get(result->info.package_name));
    if (result) {
        //
    }
}

void vfs_request_asset(vfs_state* state, vfs_request_info info) {
    if (!state) {
        KERROR("vfs_request_asset requires state to be provided.");
    }

    // Async asset requests are jobifyed.
    vfs_asset_job_params job_params = {
        .info = info,
        .state = state};
    job_info job = job_create(vfs_asset_job_start, vfs_asset_job_success, vfs_asset_job_fail, &job_params, sizeof(vfs_asset_job_params), sizeof(vfs_asset_job_result));
    job_system_submit(job);
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

    const char* asset_name_str = kname_string_get(info.asset_name);

    u32 package_count = darray_length(state->packages);
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];

        if (info.package_name == INVALID_KNAME || package->name == info.package_name) {

            KDEBUG("Attempting to load asset '%s' from package '%s'...", asset_name_str, kname_string_get(package->name));

            // Determine if the asset type is text.
            kpackage_result result = KPACKAGE_RESULT_INTERNAL_FAILURE;
            if (info.is_binary) {
                result = kpackage_asset_bytes_get(package, info.asset_name, &out_data.size, &out_data.bytes);
                out_data.flags |= VFS_ASSET_FLAG_BINARY_BIT;
            } else {
                result = kpackage_asset_text_get(package, info.asset_name, &out_data.size, &out_data.text);
            }

            // Translate the result to VFS layer and send on up.
            if (result != KPACKAGE_RESULT_SUCCESS) {
                KTRACE("Failed to load binary asset. See logs for details.");
                switch (result) {
                case KPACKAGE_RESULT_ASSET_GET_FAILURE:
                    out_data.result = VFS_REQUEST_RESULT_FILE_DOES_NOT_EXIST;
                    break;
                default:
                case KPACKAGE_RESULT_INTERNAL_FAILURE:
                    out_data.result = VFS_REQUEST_RESULT_INTERNAL_FAILURE;
                    break;
                }
            } else {
                out_data.result = VFS_REQUEST_RESULT_SUCCESS;
                // Keep the package name in case an importer needs it later.
                out_data.package_name = package->name;
                out_data.path = kpackage_path_for_asset(package, info.asset_name);
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

    const char* filename = string_filename_no_extension_from_path(path);
    out_data->asset_name = kname_create(filename);
    string_free(filename);
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

    out_data->result = VFS_REQUEST_RESULT_SUCCESS;
}

b8 vfs_asset_write_binary(vfs_state* state, kname asset_name, kname package_name, u64 size, const void* data) {
    KASSERT_DEBUG(state);
    u32 package_count = darray_length(state->packages);
    if (package_name == INVALID_KNAME) {
        KERROR("%s: Unable to write asset because it does not have a package name: '%s'.", __FUNCTION__, kname_string_get(asset_name));
        return false;
    }
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];
        if (package->name == package_name) {
            return kpackage_asset_bytes_write(package, asset_name, size, data);
        }
    }

    KERROR("%s: Unable to find package named '%s'.", __FUNCTION__, kname_string_get(package_name));
    return false;
}

b8 vfs_asset_write_text(vfs_state* state, kname asset_name, kname package_name, const char* text) {
    KASSERT_DEBUG(state);
    u32 package_count = darray_length(state->packages);
    if (package_name == INVALID_KNAME) {
        KERROR("%s: Unable to write asset because it does not have a package name: '%s'.", __FUNCTION__, kname_string_get(asset_name));
        return false;
    }
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];
        if (package->name == package_name) {
            return kpackage_asset_text_write(package, asset_name, string_length(text) + 1, text);
        }
    }

    KERROR("%s: Unable to find package named '%s'.", __FUNCTION__, kname_string_get(package_name));
    return false;
}

void vfs_asset_data_cleanup(vfs_asset_data* data) {
    if (data) {
        if (data->size && data->bytes) {
            kfree((void*)data->bytes, data->size, MEMORY_TAG_ASSET);
        }
        if (data->context || data->context_size) {
            KWARN("%s - Possible memory leak - context/context_size found on vfs_asset_data.", __FUNCTION__);
        }
        kzero_memory(data, sizeof(vfs_asset_data));
    }
}

#if KOHI_HOT_RELOAD
static void file_deleted(u32 watcher_id, void* context) {
    vfs_state* state = (vfs_state*)context;
    KTRACE("VFS: File associated with watch id %u has been deleted. Watch will be removed.", watcher_id);

    // Remove watch.
    vfs_asset_unwatch(state, watcher_id);

    // Fire off an event.
    event_context evt_context = {
        .data.u32[0] = watcher_id};
    event_fire(EVENT_CODE_VFS_FILE_DELETED_FROM_DISK, 0, evt_context);
}

static void file_written(u32 watcher_id, const char* file_path, b8 is_binary, void* context) {
    vfs_state* state = (vfs_state*)context;

    KTRACE("VFS: File associated with watch id %u has been written to.", watcher_id);

    vfs_asset_data asset_data = {
        .path = file_path};

    // NOTE: asset and package name won't be available for hot reloaded assets.

    // Re-read file.
    vfs_request_direct_from_disk_sync(state, file_path, is_binary, 0, 0, &asset_data);

    // Fire off an event.
    event_context evt_context = {
        .data.u32[0] = watcher_id,
    };
    event_fire(EVENT_CODE_VFS_FILE_WRITTEN_TO_DISK, &asset_data, evt_context);

    // Cleanup asset data.
    vfs_asset_data_cleanup(&asset_data);
}

u32 vfs_asset_watch(vfs_state* state, kname asset_name, kname package_name, b8 is_binary) {
    u32 out_watch_id = INVALID_ID_U32;

    u32 package_count = darray_length(state->packages);
    for (u32 i = 0; i < package_count; ++i) {
        kpackage* package = &state->packages[i];
        if (package->name == package_name) {
            const char* asset_path = kpackage_path_for_asset(package, asset_name);
            if (!platform_watch_file(asset_path, is_binary, file_written, state, file_deleted, state, &out_watch_id)) {
                KWARN("VFS: Unable to watch file '%s'.", asset_path);
                return INVALID_ID_U32;
            }
            return out_watch_id;
        }
    }

    return INVALID_ID_U32;
}

b8 vfs_asset_unwatch(vfs_state* state, u32 watch_id) {
    return platform_unwatch_file(watch_id);
}
#endif

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
