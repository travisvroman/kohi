#pragma once

#include "defines.h"

typedef struct asset_manifest_asset {
    const char* name;
    const char* type;
    // TODO: If loaded from binary, this might be null?
    const char* path;
} asset_manifest_asset;

/**
 * @brief A reference to another package in an asset manifest.
 */
typedef struct asset_manifest_reference {
    const char* name;
    const char* path;
} asset_manifest_reference;

typedef struct asset_manifest {
    const char* name;
    // Path to .kpackage file. Null if loading from disk.
    const char* path;

    // darray
    asset_manifest_asset* assets;

    // darray
    asset_manifest_reference* references;
} asset_manifest;

struct kpackage_internal;

typedef struct kpackage {
    const char* name;
    b8 is_binary;
    struct kpackage_internal* internal_data;
} kpackage;

KAPI b8 kpackage_create_from_manifest(const asset_manifest* manifest, kpackage* out_package);
KAPI b8 kpackage_create_from_binary(u64 size, void* bytes, kpackage* out_package);
KAPI void kpackage_destroy(kpackage* package);

KAPI void* kpackage_asset_bytes_get(const kpackage* package, const char* type, const char* name, u64* out_size);
KAPI const char* kpackage_asset_text_get(const kpackage* package, const char* type, const char* name, u64* out_size);

KAPI b8 kpackage_parse_manifest_file_content(const char* path, asset_manifest* out_manifest);
KAPI void kpackage_manifest_destroy(asset_manifest* manifest);
