#pragma once

#include "defines.h"

/**
 * @brief Gets the formatted time as a string from the given number of seconds.
 * The format is "hh:mm:ss.xx"
 * 
 * @param total_seconds The total amount of seconds to extract the string from.
 * @return const char* The string formatted time stamp "hh:mm:ss.xx"
 */
KAPI const char* time_as_string_from_seconds(f32 total_seconds);