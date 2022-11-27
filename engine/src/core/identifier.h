/**
 * @file identifier.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains a system for creating numeric identifiers.
 * @version 1.0
 * @date 2022-09-22
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief Acquires a new identifier for the given owner.
 *
 * @param owner The owner of the identifier.
 * @return The new identifier.
 */
KAPI u32 identifier_aquire_new_id(void* owner);

/**
 * @brief Releases the given identifier, which can then be used
 * again.
 *
 * @param id The identifier to be released.
 */
KAPI void identifier_release_id(u32 id);
