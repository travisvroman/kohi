#include "kpackage.h"
#include "containers/darray.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "platform/filesystem.h"
#include "strings/kstring.h"

typedef struct asset_entry {
    const char* name;
    // If loaded from binary, this will be null.
    const char* path;

    // If loaded from binary, these define where the asset is in the blob.
    u64 offset;
    u64 size;
} asset_entry;

typedef struct asset_type_lookup {
    const char* name;
    // darray
    asset_entry* entries;
} asset_type_lookup;

typedef struct kpackage_internal {
    // darray
    asset_type_lookup* types;
} kpackage_internal;

b8 kpackage_create_from_manifest(const asset_manifest* manifest, kpackage* out_package) {
    if (!manifest || !out_package) {
        KERROR("kpackage_create_from_manifest requires valid pointers to manifest and out_package.");
        return false;
    }

    kzero_memory(out_package, sizeof(kpackage));

    if (manifest->name) {
        out_package->name = string_duplicate(manifest->name);
    } else {
        KERROR("Manifest must contain a name.");
        return false;
    }

    out_package->is_binary = false;

    out_package->internal_data = kallocate(sizeof(kpackage_internal), MEMORY_TAG_RESOURCE);
    out_package->internal_data->types = darray_create(asset_type_lookup);

    // Process manifest
    u32 asset_count = darray_length(manifest->assets);
    for (u32 i = 0; i < asset_count; ++i) {
        asset_manifest_asset* asset = &manifest->assets[i];

        asset_entry new_entry = {0};
        new_entry.name = string_duplicate(asset->name);
        new_entry.path = string_duplicate(asset->path);

        // NOTE: Size and offset don't get filled out/used with a manifest version of a package.
        b8 type_found = false;
        // Search for a list of the given type.
        u32 type_count = darray_length(out_package->internal_data->types);
        for (u32 j = 0; j < type_count; ++j) {
            asset_type_lookup* lookup = &out_package->internal_data->types[j];
            if (strings_equali(lookup->name, asset->type)) {
                // Push to existing type list.
                darray_push(lookup->entries, new_entry);
                type_found = true;
                break;
            }
        }

        // If the type was not found, create a new one to push to.
        if (!type_found) {
            // Create the type lookup list.
            asset_type_lookup new_lookup = {0};
            new_lookup.name = string_duplicate(asset->type);
            new_lookup.entries = darray_create(asset_entry);
            // Push the asset to it.
            darray_push(new_lookup.entries, new_entry);

            // Push the list to the types list.
            darray_push(out_package->internal_data->types, new_lookup);
        }
    }

    return true;
}

b8 kpackage_create_from_binary(u64 size, void* bytes, kpackage* out_package) {
    if (!size || !bytes || !out_package) {
        KERROR("kpackage_create_from_binary requires valid pointers to bytes and out_package, and size must be nonzero.");
        return false;
    }

    out_package->is_binary = false;

    // Process manifest
    // TODO: the thing.
    KERROR("kpackage_create_from_binary not yet supported.");
    return false;
}

void kpackage_destroy(kpackage* package) {
    if (package) {
        if (package->name) {
            string_free(package->name);
        }

        // Clear type lookups.
        if (package->internal_data->types) {
            u32 type_count = darray_length(package->internal_data->types);
            for (u32 i = 0; i < type_count; ++i) {
                asset_type_lookup* lookup = &package->internal_data->types[i];
                if (lookup->name) {
                    string_free(lookup->name);
                }
                // Clear entries.
                if (lookup->entries) {
                    u32 entry_count = darray_length(lookup->entries);
                    for (u32 j = 0; j < entry_count; ++j) {
                        asset_entry* entry = &lookup->entries[j];
                        if (entry->name) {
                            string_free(entry->name);
                        }
                        if (entry->path) {
                            string_free(entry->path);
                        }
                    }
                    darray_destroy(lookup->entries);
                }
            }
            darray_destroy(package->internal_data->types);
        }

        if (package->internal_data) {
            kfree(package->internal_data, sizeof(kpackage_internal), MEMORY_TAG_RESOURCE);
        }

        kzero_memory(package, sizeof(kpackage_internal));
    }
}

static const char* asset_resolve(const kpackage* package, b8 is_binary, const char* type, const char* name, file_handle* out_handle, u64* out_size) {

    // Search for a list of the given type.
    u32 type_count = darray_length(package->internal_data->types);
    for (u32 j = 0; j < type_count; ++j) {
        asset_type_lookup* lookup = &package->internal_data->types[j];
        if (strings_equali(lookup->name, type)) {
            // Search the type lookup's entries for the matching name.
            u32 entry_count = darray_length(lookup->entries);
            for (u32 j = 0; j < entry_count; ++j) {
                asset_entry* entry = &lookup->entries[j];
                if (strings_equali(entry->name, name)) {
                    if (package->is_binary) {
                        KERROR("binary packages not yet supported.");
                        return 0;
                    } else {
                        // load the file content from disk.
                        if (!filesystem_open(entry->path, FILE_MODE_READ, is_binary, out_handle)) {
                            KERROR("Package '%s': Failed to open asset '%s' file at path: '%s'.", package->name, name, entry->path);
                            return 0;
                        }
                    }

                    if (!filesystem_size(out_handle, out_size)) {
                        KERROR("Package '%s': Failed to get size for asset '%s' file at path: '%s'.", package->name, name, entry->path);
                        return 0;
                    }

                    return string_duplicate(entry->path);
                }
            }
            KERROR("Package '%s': No entry called '%s' exists of type '%s'.", package->name, name, type);
            return 0;
        }
    }

    KERROR("Package '%s': No entry called '%s' exists.", package->name, name);
    return 0;
}

void* kpackage_asset_bytes_get(const kpackage* package, const char* type, const char* name, u64* out_size) {
    if (!package || !type || !name || !out_size) {
        KERROR("kpackage_asset_bytes_get requires valid pointers to package, type, name and out_size.");
        return 0;
    }

    b8 success = false;
    file_handle f;
    u64 size;
    const char* asset_path = asset_resolve(package, true, type, name, &f, &size);
    if (!asset_path) {
        KERROR("kpackage_asset_bytes_get failed to find asset.");
        goto kpackage_asset_bytes_get_cleanup;
    }
    void* file_content = kallocate(size, MEMORY_TAG_RESOURCE);

    // Load as binary
    if (!filesystem_read_all_bytes(&f, file_content, out_size)) {
        KERROR("Package '%s': Failed to read asset '%s' as binary, at file at path: '%s'.", package->name, name, asset_path);
        goto kpackage_asset_bytes_get_cleanup;
    }

    success = true;

kpackage_asset_bytes_get_cleanup:
    filesystem_close(&f);

    if (success) {
        string_free(asset_path);
    } else {
        KERROR("Package '%s' does not contain an asset type of '%s'.", package->name, type);
    }
    return success ? file_content : 0;
}

const char* kpackage_asset_text_get(const kpackage* package, const char* type, const char* name, u64* out_size) {
    if (!package || !type || !name || !out_size) {
        KERROR("kpackage_asset_text_get requires valid pointers to package, type, name and out_size.");
        return 0;
    }

    void* file_content = kallocate(*out_size, MEMORY_TAG_RESOURCE);

    b8 success = false;
    file_handle f;
    u64 size;
    const char* asset_path = asset_resolve(package, false, type, name, &f, &size);
    if (!asset_path) {
        KERROR("kpackage_asset_bytes_get failed to find asset.");
        goto kpackage_asset_text_get_cleanup;
    }

    // Load as text
    if (!filesystem_read_all_text(&f, file_content, out_size)) {
        KERROR("Package '%s': Failed to read asset '%s' as text, at file at path: '%s'.", package->name, name, asset_path);
        goto kpackage_asset_text_get_cleanup;
    }

    success = true;

kpackage_asset_text_get_cleanup:
    filesystem_close(&f);

    if (success) {
        string_free(asset_path);
    } else {
        KERROR("Package '%s' does not contain an asset type of '%s'.", package->name, type);
    }
    return success ? file_content : 0;
}

b8 kpackage_parse_manifest_file_content(const char* path, asset_manifest* out_manifest) {
    if (!path || !out_manifest) {
        KERROR("kpackage_parse_manifest_file_content requires valid pointers to path and out_manifest, ya dingus!");
        return false;
    }

    if (!filesystem_exists(path)) {
        KERROR("File does not exist '%s'.", path);
        return false;
    }

    file_handle f;
    if (!filesystem_open(path, FILE_MODE_READ, false, &f)) {
        KERROR("Failed to load asset manifest '%s'.", path);
        return false;
    }

    b8 success = false;

    u64 size = 0;
    char* file_content = 0;
    if (!filesystem_size(&f, &size) || size == 0) {
        KERROR("Failed to get system file size.");
        goto kpackage_parse_cleanup;
    }

    file_content = kallocate(size + 1, MEMORY_TAG_STRING);
    u64 out_size = 0;
    if (!filesystem_read_all_text(&f, file_content, &out_size)) {
        KERROR("Failed to read all text for asset manifest '%s'.", path);
        goto kpackage_parse_cleanup;
    }

    // Parse manifest
    kson_tree tree;
    if (!kson_tree_from_string(file_content, &tree)) {
        KERROR("Failed to parse asset manifest file '%s'. See logs for details.", path);
        return false;
    }

    // Extract properties from file.
    if (!kson_object_property_value_get_string(&tree.root, "package_name", &out_manifest->name)) {
        KERROR("Asset manifest format - 'package_name' is required but not found.");
        goto kpackage_parse_cleanup;
    }

    // Process references.
    kson_array references = {0};
    b8 contains_references = kson_object_property_value_get_object(&tree.root, "references", &references);
    if (contains_references) {
        u32 reference_array_count = 0;
        if (!kson_array_element_count_get(&references, &reference_array_count)) {
            KWARN("Failed to get array count for references. Skipping.");
        } else {

            // Stand up a darray for references.
            out_manifest->references = darray_create(asset_manifest_reference);

            for (u32 i = 0; i < reference_array_count; ++i) {
                kson_object ref_obj = {0};
                if (!kson_array_element_value_get_object(&references, i, &ref_obj)) {
                    KWARN("Failed to get object at array index %u. Skipping.", i);
                    continue;
                }

                asset_manifest_reference ref = {0};
                if (!kson_object_property_value_get_string(&ref_obj, "name", &ref.name)) {
                    KWARN("Failed to get reference name at array index %u. Skipping.", i);
                    continue;
                }
                if (!kson_object_property_value_get_string(&ref_obj, "path", &ref.path)) {
                    KWARN("Failed to get reference path at array index %u. Skipping.", i);
                    if (ref.name) {
                        string_free(ref.name);
                    }
                    continue;
                }

                // Add to references
                darray_push(out_manifest->references, ref);
            }
        }
    }

    // Process assets.
    kson_array assets = {0};
    b8 contains_assets = kson_object_property_value_get_object(&tree.root, "assets", &assets);
    if (contains_assets) {
        u32 asset_array_count = 0;
        if (!kson_array_element_count_get(&assets, &asset_array_count)) {
            KWARN("Failed to get array count for assets. Skipping.");
        } else {

            // Stand up a darray for assets.
            out_manifest->assets = darray_create(asset_manifest_asset);

            for (u32 i = 0; i < asset_array_count; ++i) {
                kson_object asset_obj = {0};
                if (!kson_array_element_value_get_object(&assets, i, &asset_obj)) {
                    KWARN("Failed to get object at array index %u. Skipping.", i);
                    continue;
                }

                asset_manifest_asset asset = {0};
                if (!kson_object_property_value_get_string(&asset_obj, "name", &asset.name)) {
                    KWARN("Failed to get asset name at array index %u. Skipping.", i);
                    continue;
                }
                if (!kson_object_property_value_get_string(&asset_obj, "path", &asset.path)) {
                    KWARN("Failed to get asset path at array index %u. Skipping.", i);
                    if (asset.name) {
                        string_free(asset.name);
                    }
                    continue;
                }
                if (!kson_object_property_value_get_string(&asset_obj, "type", &asset.type)) {
                    KWARN("Failed to get asset type at array index %u. Skipping.", i);
                    if (asset.name) {
                        string_free(asset.name);
                    }
                    if (asset.path) {
                        string_free(asset.path);
                    }
                    continue;
                }

                // Add to assets
                darray_push(out_manifest->assets, asset);
            }
        }
    }

    success = true;
kpackage_parse_cleanup:
    filesystem_close(&f);
    if (file_content) {
        string_free(file_content);
    }
    kson_tree_cleanup(&tree);
    if (!success) {
        // Clean up out_manifest->
        if (out_manifest->references) {
            u32 ref_count = darray_length(out_manifest->references);
            for (u32 i = 0; i < ref_count; ++i) {
                if (out_manifest->references[i].name) {
                    string_free(out_manifest->references[i].name);
                }
                if (out_manifest->references[i].path) {
                    string_free(out_manifest->references[i].path);
                }
            }
            darray_destroy(out_manifest->references);
            out_manifest->references = 0;
        }
    }
    return success;
}

void kpackage_manifest_destroy(asset_manifest* manifest) {
    if (manifest) {
        if (manifest->name) {
            string_free(manifest->name);
        }
        if (manifest->path) {
            string_free(manifest->path);
        }
        if (manifest->references) {
            u32 ref_count = darray_length(manifest->references);
            for (u32 i = 0; i < ref_count; ++i) {
                asset_manifest_reference* ref = &manifest->references[i];
                if (ref->name) {
                    string_free(ref->name);
                }
                if (ref->path) {
                    string_free(ref->path);
                }
            }
            darray_destroy(manifest->references);
        }

        if (manifest->assets) {
            u32 ref_count = darray_length(manifest->assets);
            for (u32 i = 0; i < ref_count; ++i) {
                asset_manifest_asset* asset = &manifest->assets[i];
                if (asset->name) {
                    string_free(asset->name);
                }
                if (asset->path) {
                    string_free(asset->path);
                }
                if (asset->type) {
                    string_free(asset->type);
                }
            }
            darray_destroy(manifest->assets);
        }
        kzero_memory(manifest, sizeof(asset_manifest));
    }
}
