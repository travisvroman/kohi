#pragma once

#include "defines.h"
#include "identifiers/identifier.h"

// The maximum length of the string representation of an asset type.
#define KASSET_TYPE_MAX_LENGTH 64
// The maximum name of an asset.
#define KASSET_NAME_MAX_LENGTH 256
// The maximum name length for a kpackage.
#define KPACKAGE_NAME_MAX_LENGTH 128

// The maximum length of a fully-qualified asset name, including the '.' between parts.
#define KASSET_FULLY_QUALIFIED_NAME_MAX_LENGTH = (KPACKAGE_NAME_MAX_LENGTH + KASSET_TYPE_MAX_LENGTH + KASSET_NAME_MAX_LENGTH + 2)

/** @brief Indicates where an asset is in its lifecycle. */
typedef enum kasset_state {
    /**
     * @brief No load operations have happened whatsoever
     * for the asset.
     * The asset is NOT in a drawable state.
     */
    KASSET_STATE_UNINITIALIZED,
    /**
     * @brief The CPU-side of the asset resources have been
     * loaded, but no GPU uploads have happened.
     * The asset is NOT in a drawable state.
     */
    KASSET_STATE_INITIALIZED,
    /**
     * @brief The GPU-side of the asset resources are in the
     * process of being uploaded, but are not yet complete.
     * The asset is NOT in a drawable state.
     */
    KASSET_STATE_LOADING,
    /**
     * @brief The GPU-side of the asset resources are finished
     * with the process of being uploaded.
     * The asset IS in a drawable state.
     */
    KASSET_STATE_LOADED
} kasset_state;

typedef enum kasset_type {
    KASSET_TYPE_UNKNOWN,
    /** An image, typically (but not always) used as a texture. */
    KASSET_TYPE_IMAGE,
    KASSET_TYPE_MATERIAL,
    KASSET_TYPE_STATIC_MESH,
    KASSET_TYPE_HEIGHTMAP_TERRAIN,
    KASSET_TYPE_BITMAP_FONT,
    KASSET_TYPE_SYSTEM_FONT,
    KASSET_TYPE_TEXT,
    KASSET_TYPE_BINARY,
    KASSET_TYPE_KSON,
    KASSET_TYPE_VOXEL_TERRAIN,
    KASSET_TYPE_SKELETAL_MESH,
    KASSET_TYPE_MAX
} kasset_type;

/**
 * @brief Represents the name of an asset, complete with all
 * parts of the name along with the fully-qualified name.
 */
typedef struct kasset_name {
    /** @brief The fully-qualified name in the format "<PackageName>.<AssetType>.<AssetName>". */
    const char* fully_qualified_name;
    /** @brief The package name the asset belongs to. */
    char package_name[KPACKAGE_NAME_MAX_LENGTH];
    /** @brief The asset type in string format. */
    char asset_type[KASSET_TYPE_MAX_LENGTH];
    /** @brief The asset name. */
    char asset_name[KASSET_NAME_MAX_LENGTH];
} kasset_name;

typedef struct kasset_metadata {
    // Size of the asset.
    u64 size;
    // Asset name info.
    kasset_name name;
    /** @brief The asset type */
    kasset_type asset_type;
    /** @brief The path of the originally imported file used to create this asset. */
    const char* source_file_path;
    // TODO: Listing of asset-type-specific metadata

} kasset_metadata;

/**
 * @brief a structure meant to be included as the first member in the
 * struct of all asset types for quick casting purposes.
 */
typedef struct kasset {
    /** @brief A system-wide unique identifier for the asset. */
    identifier id;
    /** @brief Increments every time the asset is loaded/reloaded. Otherwise INVALID_ID. */
    u32 generation;
    /** @brief The current state of the asset. */
    kasset_state state;
    /** @brief Metadata for the asset */
    kasset_metadata meta;
} kasset;
