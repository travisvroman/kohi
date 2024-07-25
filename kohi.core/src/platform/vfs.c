#include "vfs.h"

#include "containers/darray.h"
#include "logger.h"
#include "platform/kpackage.h"
#include "strings/kstring.h"

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
                if (strings_equali(state->packages[j].name, ref->name)) {
                    KTRACE("Package '%s' already loaded, skipping.", ref->name);
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }

            asset_manifest new_manifest;
            if (!kpackage_parse_manifest_file_content(ref->path, &new_manifest)) {
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
                success = false;
                kpackage_manifest_destroy(&new_manifest);
                break;
            }
        }
    }

    return success;
}

b8 vfs_initialize(const vfs_config* config, vfs_state* out_state) {
    if (!config || !out_state) {
        KERROR("vfs_initialize requires valid pointers to config and out_state.");
        return false;
    }

    out_state->packages = darray_create(kpackage);

    // TODO: For release builds, look at binary file.
    const char* file_path = "./asset_manifest";
    asset_manifest manifest;
    if (!kpackage_parse_manifest_file_content(file_path, &manifest)) {
        KERROR("Failed to parse primary asset manifest. See logs for details.");
        return false;
    }

    kpackage primary_package = {0};
    if (!kpackage_create_from_manifest(&manifest, &primary_package)) {
        KERROR("Failed to create package from primary asset manifest. See logs for details.");
        return false;
    }

    darray_push(out_state->packages, primary_package);

    // Examine primary package references and load them as needed.
    if (!process_manifest_refs(out_state, &manifest)) {
        KERROR("Failed to process manifest reference. See logs for details.");
        kpackage_manifest_destroy(&manifest);
        return false;
    }

    return true;
}

void vfs_shutdown(vfs_state* state) {
}

void vfs_request_asset(const char* name, PFN_on_asset_loaded_callback callback) {
}
