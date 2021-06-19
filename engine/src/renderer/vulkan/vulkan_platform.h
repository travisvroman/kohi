#pragma once

#include "defines.h"

/**
 * Appends the names of required extensions for this platform to
 * the names_darray, which should be created and passed in.
 */
void platform_get_required_extension_names(const char*** names_darray);