/**
 * @file scene_loader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Loader for scene files.
 * @version 2.0
 * @date 2023-03-29
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "systems/resource_system.h"

/**
 * @brief Creates and returns a simple scene resource loader.
 *
 * @return The newly created resource loader.
 */
resource_loader scene_resource_loader_create(void);
