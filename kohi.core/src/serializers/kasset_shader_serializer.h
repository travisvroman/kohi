/**
 * @file kasset_shader_serializer.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Serializer for shader assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "assets/kasset_types.h"

KAPI const char *kasset_shader_serialize (const kasset_shader *asset);

KAPI b8 kasset_shader_deserialize (const char *file_text, kasset_shader *out_asset);
