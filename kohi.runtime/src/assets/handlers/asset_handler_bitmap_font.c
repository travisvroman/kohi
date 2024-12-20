#include "asset_handler_bitmap_font.h"

#include <assets/asset_handler_types.h>
#include <assets/kasset_utils.h>
#include <core/engine.h>
#include <debug/kassert.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <platform/vfs.h>
#include <serializers/kasset_bitmap_font_serializer.h>
#include <strings/kstring.h>

void asset_handler_bitmap_font_create(struct asset_handler* self, struct vfs_state* vfs) {
    KASSERT_MSG(self && vfs, "Valid pointers are required for 'self' and 'vfs'.");

    self->vfs = vfs;
    self->is_binary = false;
    self->request_asset = 0;
    self->release_asset = asset_handler_bitmap_font_release_asset;
    self->type = KASSET_TYPE_BITMAP_FONT;
    self->type_name = KASSET_TYPE_NAME_BITMAP_FONT;
    self->binary_serialize = kasset_bitmap_font_serialize;
    self->binary_deserialize = kasset_bitmap_font_deserialize;
    self->text_serialize = 0;
    self->text_deserialize = 0;
}

void asset_handler_bitmap_font_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_bitmap_font* typed_asset = (kasset_bitmap_font*)asset;

    array_kasset_bitmap_font_page_destroy(&typed_asset->pages);
    array_kasset_bitmap_font_glyph_destroy(&typed_asset->glyphs);
    array_kasset_bitmap_font_kerning_destroy(&typed_asset->kernings);

    kzero_memory(typed_asset, sizeof(kasset_bitmap_font));
}
