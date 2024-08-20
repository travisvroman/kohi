#pragma once

#include <assets/kasset_types.h>

#include "kresource_types.h"

KAPI kresource_texture_format image_format_to_texture_format(kasset_image_format format);

KAPI kasset_image_format texture_format_to_image_format(kresource_texture_format format);
