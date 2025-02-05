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

/**
 * @brief Returns the number of milliseconds contained within the provided seconds.
 *
 * @param seconds The number of seconds to obtain milliseconds for. Can be fractional seconds.
 *
 * @return The number of milliseconds.
 */
KAPI u64 milliseconds_from_seconds_f64(f64 seconds);
