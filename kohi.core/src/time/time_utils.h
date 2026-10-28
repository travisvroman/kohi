/**
 * @file time_utils.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A collection of time utilities.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief Gets the formatted time as a string from the given number of seconds.
 * The format is "hh:mm:ss.xx"
 *
 * @param total_seconds The total amount of seconds to extract the string from.
 * @return const char* The string formatted time stamp "hh:mm:ss.xx"
 */
KAPI const char *time_as_string_from_seconds (f32 total_seconds);