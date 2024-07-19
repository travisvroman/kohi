#include "kpackage.h"
#include "containers/darray.h"
#include "logger.h"
#include "memory/kmemory.h"
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

// Types of assets to be treated as text.
#define TEXT_ASSET_TYPE_COUNT 3
static const char* text_asset_types[TEXT_ASSET_TYPE_COUNT] = {
    "Material",
    "Scene",
    "Terrain"};

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
    u32 asset_count = manifest->asset_count;
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

static b8 treat_type_as_text(const char* type) {
    for (u32 i = 0; i < TEXT_ASSET_TYPE_COUNT; ++i) {
        if (strings_equali(type, text_asset_types[i])) {
            return true;
        }
    }
    return false;
}

void* kpackage_asset_bytes_get(const kpackage* package, const char* type, const char* name, u64* out_size) {
    if (!package || !type || !name || !out_size) {
        KERROR("kpackage_asset_bytes_get requires valid pointers to package, type, name and out_size.");
        return 0;
    }

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
                        b8 is_text = treat_type_as_text(type);
                        // load the file content from disk.
                        file_handle f;
                        if (!filesystem_open(entry->path, FILE_MODE_READ, !is_text, &f)) {
                            KERROR("Package '%s': Failed to open asset '%s' file at path: '%s'.", package->name, name, entry->path);
                            return 0;
                        }

                        if (!filesystem_size(&f, out_size)) {
                            KERROR("Package '%s': Failed to get size for asset '%s' file at path: '%s'.", package->name, name, entry->path);
                            return 0;
                        }

                        void* file_content = kallocate(*out_size, MEMORY_TAG_RESOURCE);

                        if (is_text) {
                            // Load as text
                            if (!filesystem_read_all_text(&f, file_content, out_size)) {
                                KERROR("Package '%s': Failed to read asset '%s' as text, at file at path: '%s'.", package->name, name, entry->path);
                                return 0;
                            }
                        } else {
                            // Load as binary
                            if (!filesystem_read_all_bytes(&f, file_content, out_size)) {
                                KERROR("Package '%s': Failed to read asset '%s' as binary, at file at path: '%s'.", package->name, name, entry->path);
                                return 0;
                            }
                        }

                        // Success!
                        return file_content;
                    }
                }
            }
            KERROR("Package '%s': No entry called '%s' exists of type '%s'.", package->name, name, type);
            return 0;
        }
    }

    KERROR("Package '%s' does not contain an asset type of '%s'.", package->name, type);
    return 0;
}
