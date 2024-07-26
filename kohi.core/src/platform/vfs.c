#include "vfs.h"

#include "containers/darray.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "platform/kpackage.h"
#include "strings/kstring.h"

// Types of assets to be treated as text.
#define TEXT_ASSET_TYPE_COUNT 3
static const char* text_asset_types[TEXT_ASSET_TYPE_COUNT] = {
    "Material",
    "Scene",
    "Terrain"};

static b8 treat_type_as_text(const char* type);
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
    // FIXME: hardcoded rubbish.
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

void vfs_request_asset(vfs_state* state, const char* name, PFN_on_asset_loaded_callback callback) {
    if (state && name && callback) {
        // "Runtime.Texture.Rock.01"
        char package_name[VFS_PACKAGE_NAME_MAX_LENGTH] = {0};
        char asset_type[VFS_ASSET_TYPE_MAX_LENGTH] = {0};
        char asset_name[VFS_ASSET_NAME_MAX_LENGTH] = {0};
        char* parts[3] = {package_name, asset_type, asset_name};

        // Get the UTF-8 string length
        u32 text_length_utf8 = string_utf8_length(name);
        u32 char_length = string_length(name);

        if (text_length_utf8 < 1) {
            KERROR("vfs_request_asset was passed an empty string for name. Nothing to be done.");
            return;
        }

        u8 part_index = 0;
        u32 part_loc = 0;
        for (u32 c = 0; c < char_length;) {
            i32 codepoint = name[c];
            u8 advance = 1;

            // Ensure the propert UTF-8 codepoint is being used.
            if (!bytes_to_codepoint(name, c, &codepoint, &advance)) {
                KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
                codepoint = -1;
            }

            if (part_index < 2 && codepoint == '.') {
                // null terminate and move on.
                parts[part_index][part_loc] = 0;
                part_index++;
                part_loc = 0;
            }

            // Add to the current part string.
            for (u32 i = 0; i < advance; ++i) {
                parts[part_index][part_loc] = c + i;
                part_loc++;
            }

            c += advance;
        }
        parts[part_index][part_loc] = 0;

        KDEBUG("Loading asset '%s' of type '%s' from package '%s'...", asset_name, asset_type, package_name);

        u32 package_count = darray_length(state->packages);
        for (u32 i = 0; i < package_count; ++i) {
            kpackage* package = &state->packages[i];
            if (strings_equali(package->name, package_name)) {
                // Determine if the asset type is text.

                vfs_asset_data data = {0};
                if (treat_type_as_text(asset_type)) {
                    data.text = kpackage_asset_text_get(package, asset_type, asset_name, &data.size);
                    if (!data.text) {
                        KERROR("Failed to text load asset. See logs for details.");
                        data.success = false;
                        return;
                    }
                } else {
                    data.bytes = kpackage_asset_bytes_get(package, asset_type, asset_name, &data.size);
                    if (!data.bytes) {
                        KERROR("Failed to load binary asset. See logs for details.");
                        data.success = false;
                        data.flags |= VFS_ASSET_FLAG_BINARY_BIT;
                        return;
                    }
                }

                data.success = true;
                callback(asset_name, data);

                return;
            }
        }

        KERROR("No package named '%s' exists. Nothing was done.", package_name);

    } else {
        KERROR("vfs_request_asset requires state, name and callback to be provided.");
    }
}

static b8 treat_type_as_text(const char* type) {
    for (u32 i = 0; i < TEXT_ASSET_TYPE_COUNT; ++i) {
        if (strings_equali(type, text_asset_types[i])) {
            return true;
        }
    }
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
                if (strings_equali(state->packages[j].name, ref->name)) {
                    KTRACE("Package '%s' already loaded, skipping.", ref->name);
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }

            asset_manifest new_manifest = {0};
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
