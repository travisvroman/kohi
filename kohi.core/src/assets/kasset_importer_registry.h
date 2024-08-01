#pragma once

#include "assets/kasset_types.h"
#include "defines.h"

/**
 * Initializes the kasset importer registry.
 *
 * @returns True on success; otherwise false.
 */
KAPI b8 kasset_importer_registry_initialize(void);

/** @brief Shuts the kasset importer registry down. */
KAPI void kasset_importer_registry_shutdown(void);

/**
 * @brief Registers the provided importer as an importer for the given asset type.
 *
 * @param type The target asset type.
 * @param importer A copy of the importer to register.
 * @returns True on success; otherwise false.
 */
KAPI b8 kasset_importer_registry_register(kasset_type type, kasset_importer importer);

/**
 * Attempts to obtain an importer for the given asset and source types.
 *
 * @param type The target asset type.
 * @param source_type The source asset type (i.e. ".obj", ".png", etc.).
 * @returns A pointer to the importer on success; or 0 if not found.
 */
KAPI const kasset_importer* kasset_importer_registry_get_for_source_type(kasset_type type, const char* source_type);
