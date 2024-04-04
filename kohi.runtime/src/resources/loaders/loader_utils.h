/**
 * @file loader_utils.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains resource loader utility functions.
 * @version 1.0
 * @date 2022-01-11
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "kmemory.h"
#include "defines.h"
#include "resources/resource_types.h"

struct resource_loader;

/**
 * @brief Unloads a resource using the appropriate registered loader.
 *
 * @param self A pointer to the resource loader to be used.
 * @param resource A pointer to the resource to be unloaded.
 * @param tag The memory tag that was used by the resource loader, and should be used to unload the resource.
 * @return True on success; otherwise false.
 */
KAPI b8 resource_unload(struct resource_loader* self, resource* resource, memory_tag tag);
