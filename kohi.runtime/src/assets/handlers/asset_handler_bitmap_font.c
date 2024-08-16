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
    self->binary_serialize = 0;
    self->binary_deserialize = 0;
    self->text_serialize = 0; // NOTE: Intentionally not set as serializing to this format makes no sense.
    self->text_deserialize = kasset_bitmap_font_deserialize;
}

void asset_handler_bitmap_font_release_asset(struct asset_handler* self, struct kasset* asset) {
    kasset_bitmap_font* typed_asset = (kasset_bitmap_font*)asset;

    // LEFTOFF: Instead of this....
    //
    /* if (typed_asset->pages && typed_asset->page_count) {
        kfree(typed_asset->pages, sizeof(kasset_bitmap_font_page) * typed_asset->page_count, MEMORY_TAG_ARRAY);
        typed_asset->pages = 0;
        typed_asset->page_count = 0;
    }
    if (typed_asset->glyphs && typed_asset->glyph_count) {
        kfree(typed_asset->glyphs, sizeof(kasset_bitmap_font_glyph) * typed_asset->glyph_count, MEMORY_TAG_ARRAY);
        typed_asset->glyphs = 0;
        typed_asset->glyph_count = 0;
    }
    if (typed_asset->kernings && typed_asset->kerning_count) {
        kfree(typed_asset->kernings, sizeof(kasset_bitmap_font_kerning) * typed_asset->kerning_count, MEMORY_TAG_ARRAY);
        typed_asset->kernings = 0;
        typed_asset->kerning_count = 0;
    } */

    // LEFTOFF: we now have this...
    kasset_bitmap_font_page_array_destroy(&typed_asset->pages);
    kasset_bitmap_font_glyph_array_destroy(&typed_asset->glyphs);
    kasset_bitmap_font_kerning_array_destroy(&typed_asset->kernings);

    kzero_memory(typed_asset, sizeof(kasset_bitmap_font));
}
