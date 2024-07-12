/**
 * @file bitmap_font_loader.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The loader for bitmap fonts.
 * @version 1.0
 * @date 2022-09-12
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */
#pragma once

#include "systems/resource_system.h"

/**
 * @brief Creates and returns a bitmap font resource loader.
 * 
 * @return The newly created resource loader.
 */
resource_loader bitmap_font_resource_loader_create(void);
