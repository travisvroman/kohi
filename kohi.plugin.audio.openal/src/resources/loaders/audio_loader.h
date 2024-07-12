
/**
 * @file audio_loader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A resource loader that handles binary resources.
 * @version 1.0
 * @date 2023-10-8
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */

#pragma once

#include "audio/audio_types.h"
#include "systems/resource_system.h"

typedef struct audio_resource_loader_params {
    audio_file_type type;
    u64 chunk_size;
} audio_resource_loader_params;

/**
 * @brief Creates and returns a audio resource loader.
 *
 * @return The newly created resource loader.
 */
KAPI resource_loader audio_resource_loader_create(void);
