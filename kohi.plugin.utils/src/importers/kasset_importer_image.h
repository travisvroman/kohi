#pragma once

#include <assets/kasset_types.h>

typedef struct kasset_image_import_options {
    /** @brief Indicates if the image should be flipped on the y-axis when imported. */
    b8 flip_y;
    /** @brief The expected format of the image. */
    kasset_image_format format;
} kasset_image_import_options;

KAPI b8 kasset_importer_image_import(struct kasset_importer* self, u64 data_size, void* data, void* params, struct kasset* out_asset);
