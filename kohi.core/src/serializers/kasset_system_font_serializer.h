/**
 * @file kasset_system_font_serializer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Serializer for system font assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */


#include "assets/kasset_types.h"

KAPI const char *kasset_system_font_serialize (const kasset *asset);

KAPI b8 kasset_system_font_deserialize (const char *file_text, kasset_system_font *out_asset);
