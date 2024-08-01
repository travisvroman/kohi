#pragma once

#include "defines.h"
#include "identifiers/identifier.h"
#include "math/math_types.h"

/** @brief A magic number indicating the file as a kohi binary asset file. */
#define ASSET_MAGIC 0xcafebabe

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
    KASSET_TYPE_SCENE,
    KASSET_TYPE_BITMAP_FONT,
    KASSET_TYPE_SYSTEM_FONT,
    KASSET_TYPE_TEXT,
    KASSET_TYPE_BINARY,
    KASSET_TYPE_KSON,
    KASSET_TYPE_VOXEL_TERRAIN,
    KASSET_TYPE_SKELETAL_MESH,
    KASSET_TYPE_AUDIO,
    KASSET_TYPE_MUSIC,
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
    // The asset version.
    u32 version;
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

struct kasset;
struct kasset_importer;

/**
 * @brief Imports an asset according to the provided params and the importer's internal logic.
 * NOTE: Some importers (i.e. .obj for static meshes) can also trigger imports of other assets. Those assets are immediately
 * serialized to disk/package and not returned here though.
 *
 * @param self A pointer to the importer itself.
 * @param data_size The size of the data being imported.
 * @param data A block of memory containing the data being imported.
 * @param params A block of memory containing parameters for the import. Optional in general, but required by some importers.
 * @param out_asset A pointer to the asset being imported.
 * @returns True on success; otherwise false.
 */
typedef b8 (*PFN_kasset_importer_import)(struct kasset_importer* self, u64 data_size, void* data, void* params, struct kasset* out_asset);

/**
 * @brief Represents the interface point for an importer.
 */
typedef struct kasset_importer {
    /** @brief The file type supported by the importer. */
    const char* source_type;
    /**
     * @brief Imports an asset according to the provided params and the importer's internal logic.
     * NOTE: Some importers (i.e. .obj for static meshes) can also trigger imports of other assets. Those assets are immediately
     * serialized to disk/package and not returned here though.
     *
     * @param self A pointer to the importer itself.
     * @param data_size The size of the data being imported.
     * @param data A block of memory containing the data being imported.
     * @param params A block of memory containing parameters for the import. Optional in general, but required by some importers.
     * @param out_asset A pointer to the asset being imported.
     * @returns True on success; otherwise false.
     */
    PFN_kasset_importer_import import;
} kasset_importer;

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

    /** @brief The size of the data block in bytes. */
    u64 data_size;

    union {
        /** The asset data as a string, if a text asset. Zero-terminated. */
        const char* text;
        /** The binary asset data, if a binary asset. */
        void* bytes;
    };
} kasset;

#define KASSET_TYPE_NAME_HEIGHTMAP_TERRAIN "HeightmapTerrain"

typedef struct kasset_heightmap_terrain {
    kasset base;
    const char* heightmap_filename;
    u16 chunk_size;
    vec3 tile_scale;
    u8 material_count;
    const char** material_names;
} kasset_heightmap_terrain;

typedef enum kasset_image_format {
    KASSET_IMAGE_FORMAT_UNDEFINED = 0,
    // 4 channel, 8 bits per channel
    KASSET_IMAGE_FORMAT_RGBA8 = 1
    // TODO: additional formats
} kasset_image_format;

typedef struct kasset_image {
    kasset base;
    u32 width;
    u32 height;
    u8 channel_count;
    u8 mip_levels;
    kasset_image_format format;
} kasset_image;
