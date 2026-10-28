/**
 * @file kasset_material_serializer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Serializer for material assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "assets/kasset_types.h"

KAPI const char *kasset_material_serialize (const kasset_material *asset);

KAPI b8 kasset_material_deserialize (const char *file_text, kasset_material *out_asset);
