#pragma once

#include "defines.h"
#include "math/math_types.h"

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

typedef struct kasset_metadata {
    // Size of the asset.
    u64 size;
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
    u64 uniqueid;
    /** @brief The short name of the asset. Ex: "Rock01" */
    const char* name;
    /** @brief The fully qualified name of the asset (<Package Name>.<Asset Type>.<Asset Name>). Ex: "Testbed.Texture.Rock01" */
    const char* fully_qualified_name;
    /** @brief The current state of the asset. */
    kasset_state state;
    /** @brief Metadata for the asset */
    kasset_metadata meta;
} kasset;
