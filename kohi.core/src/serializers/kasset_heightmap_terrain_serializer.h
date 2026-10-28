/**
 * @file kasset_heightmap_terrain_serializer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Serializer for heightmap terrain assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "assets/kasset_types.h"

KAPI const char *kasset_heightmap_terrain_serialize (const kasset_heightmap_terrain *asset);

KAPI b8 kasset_heightmap_terrain_deserialize (const char *file_text, kasset_heightmap_terrain *out_asset);
