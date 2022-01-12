/**
 * @file binary_loader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A resource loader that handles binary resources.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "systems/resource_system.h"

/**
 * @brief Creates and returns a binary resource loader.
 * 
 * @return The newly created resource loader.
 */
resource_loader binary_resource_loader_create();
