#include "kresource_handler_bitmap_font.h"

#include <assets/kasset_types.h>
#include <defines.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <serializers/kasset_bitmap_font_serializer.h>
#include <strings/kname.h>

#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "systems/asset_system.h"
#include "systems/kresource_system.h"

b8 kresource_handler_bitmap_font_request(kresource_handler* self, kresource* resource, const struct kresource_request_info* info) {
    if (!self || !resource) {
        KERROR("kresource_handler_bitmap_font_request requires valid pointers to self and resource.");
        return false;
    }

    kresource_bitmap_font* typed_resource = (kresource_bitmap_font*)resource;
    typed_resource->base.state = KRESOURCE_STATE_UNINITIALIZED;

    if (info->assets.base.length < 1) {
        KERROR("kresource_handler_bitmap_font_request requires exactly one asset.");
        return false;
    }

    // Load the asset from disk synchronously.
    kasset_bitmap_font* asset = asset_system_request_bitmap_font_from_package_sync(
        engine_systems_get()->asset_state,
        kname_string_get(info->assets.data[0].package_name),
        kname_string_get(info->assets.data[0].asset_name));

    kresource_bitmap_font* out_bitmap_font = typed_resource;
    // Take a copy of all of the asset properties.

    out_bitmap_font->size = asset->size;
    out_bitmap_font->face = asset->face;
    out_bitmap_font->baseline = asset->baseline;
    out_bitmap_font->line_height = asset->line_height;
    out_bitmap_font->atlas_size_x = asset->atlas_size_x;
    out_bitmap_font->atlas_size_y = asset->atlas_size_y;

    // Glyphs
    if (asset->glyphs.base.length) {
        out_bitmap_font->glyphs = array_font_glyph_create(asset->glyphs.base.length);
        for (u32 i = 0; i < asset->glyphs.base.length; ++i) {
            kasset_bitmap_font_glyph* src = &asset->glyphs.data[i];
            font_glyph* dest = &out_bitmap_font->glyphs.data[i];

            dest->x = src->x;
            dest->y = src->y;
            dest->width = src->width;
            dest->height = src->height;
            dest->x_offset = src->x_offset;
            dest->y_offset = src->y_offset;
            dest->x_advance = src->x_advance;
            dest->codepoint = src->codepoint;
            dest->page_id = src->page_id;
        }
    }

    // Kernings
    if (asset->kernings.base.length) {
        out_bitmap_font->kernings = array_font_kerning_create(asset->glyphs.base.length);
        for (u32 i = 0; i < asset->kernings.base.length; ++i) {
            kasset_bitmap_font_kerning* src = &asset->kernings.data[i];
            font_kerning* dest = &out_bitmap_font->kernings.data[i];

            dest->amount = src->amount;
            dest->codepoint_0 = src->codepoint_0;
            dest->codepoint_1 = src->codepoint_1;
        }
    }

    // Pages
    if (asset->pages.base.length) {
        out_bitmap_font->pages = array_font_page_create(asset->pages.base.length);
        for (u32 i = 0; i < asset->pages.base.length; ++i) {
            kasset_bitmap_font_page* src = &asset->pages.data[i];
            font_page* dest = &out_bitmap_font->pages.data[i];

            dest->image_asset_name = src->image_asset_name;
        }
    }

    out_bitmap_font->base.state = KRESOURCE_STATE_LOADED;

    // Destroy the request.
    array_kresource_asset_info_destroy((array_kresource_asset_info*)&info->assets);

    return true;
}

void kresource_handler_bitmap_font_release(kresource_handler* self, kresource* resource) {
    if (resource) {
        kresource_bitmap_font* typed_resource = (kresource_bitmap_font*)resource;

        array_font_glyph_destroy(&typed_resource->glyphs);
        array_font_kerning_destroy(&typed_resource->kernings);
        array_font_page_destroy(&typed_resource->pages);
    }
}
