/**
 * @file kasset_kson_serializer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Serializer for KSON assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "assets/kasset_types.h"

KAPI const char *kasset_kson_serialize (const kasset *asset);

KAPI b8 kasset_kson_deserialize (const char *file_text, kasset *out_asset);
