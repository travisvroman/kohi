/**
 * @file kasset_importer_image.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Importer for image assets.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "core_render_types.h"
#include "defines.h"

KAPI b8 kasset_image_import (const char *source_path, const char *target_path, b8 flip_y, kpixel_format output_format);
