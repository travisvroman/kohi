#pragma once

#include "defines.h"
#include "strings/kname.h"

typedef struct asset_manifest_asset {
    kname name;
    // TODO: If loaded from binary, this might be null?
    const char* path;
    const char* source_path;
} asset_manifest_asset;

/**
 * @brief A reference to another package in an asset manifest.
 */
typedef struct asset_manifest_reference {
    kname name;
    const char* path;
} asset_manifest_reference;

typedef struct asset_manifest {
    kname name;
    // Path to .kpackage file. Null if loading from disk.
    const char* file_path;
    // Path containing the .kpackage file, without the filename itself.
    const char* path;

    // darray
    asset_manifest_asset* assets;

    // darray
    asset_manifest_reference* references;
} asset_manifest;

struct kpackage_internal;

typedef struct kpackage {
    kname name;
    b8 is_binary;
    struct kpackage_internal* internal_data;
} kpackage;

typedef enum kpackage_result {
    KPACKAGE_RESULT_SUCCESS = 0,
    KPACKAGE_RESULT_ASSET_GET_FAILURE,
    KPACKAGE_RESULT_INTERNAL_FAILURE
} kpackage_result;

KAPI b8 kpackage_create_from_manifest(const asset_manifest* manifest, kpackage* out_package);
KAPI b8 kpackage_create_from_binary(u64 size, void* bytes, kpackage* out_package);
KAPI void kpackage_destroy(kpackage* package);

KAPI kpackage_result kpackage_asset_bytes_get(const kpackage* package, kname name, u64* out_size, const void** out_data);
KAPI kpackage_result kpackage_asset_text_get(const kpackage* package, kname name, u64* out_size, const char** out_text);

/**
 * Attempts to retrieve the path string for the given asset within the provided package.
 * NOTE: If found, returns a _copy_ of the string (dynamically allocated) which must be freed by the caller.
 *
 * @param package A constant pointer to the package to search.
 * @param name The name of the asset to search for.
 * @returns A copy of the path string, if found. Otherwise 0/null.
 */
KAPI const char* kpackage_path_for_asset(const kpackage* package, kname name);

/**
 * Attempts to retrieve the source path string for the given asset within the provided package.
 * NOTE: If found, returns a _copy_ of the string (dynamically allocated) which must be freed by the caller.
 *
 * @param package A constant pointer to the package to search.
 * @param name The name of the asset to search for.
 * @returns A copy of the source path string, if found. Otherwise 0/null.
 */
KAPI const char* kpackage_source_path_for_asset(const kpackage* package, kname name);

KAPI b8 kpackage_asset_bytes_write(kpackage* package, kname name, u64 size, const void* bytes);
KAPI b8 kpackage_asset_text_write(kpackage* package, kname name, u64 size, const char* text);

KAPI b8 kpackage_parse_manifest_file_content(const char* path, asset_manifest* out_manifest);
KAPI void kpackage_manifest_destroy(asset_manifest* manifest);
