#include "font_system.h"

#include <assets/kasset_types.h>
#include <containers/darray.h>
#include <debug/kassert.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <parsers/kson_parser.h>
#include <strings/kname.h>
#include <strings/kstring.h>

#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "renderer/renderer_frontend.h"
#include "systems/kresource_system.h"
#include "systems/texture_system.h"

// The minumum value that can be used for "max_bitmap_font_count"
#define BITMAP_FONT_MAX_COUNT_MIN 1U
// The maxumum value that can be used for "max_bitmap_font_count"
#define BITMAP_FONT_MAX_COUNT_MAX U8_MAX
// The minumum value that can be used for "max_system_font_count"
#define SYSTEM_FONT_MAX_COUNT_MIN 1U
// The maxumum value that can be used for "max_system_font_count"
#define SYSTEM_FONT_MAX_COUNT_MAX U8_MAX

// The minumum number of bitmap fonts that can be configured.
#define BITMAP_FONT_COUNT_MIN 0U
// The maximum number of bitmap fonts that can be configured.
#define BITMAP_FONT_COUNT_MAX U8_MAX
// The minumum number of system fonts that can be configured.
#define SYSTEM_FONT_COUNT_MIN 0U
// The maximum number of system fonts that can be configured.
#define SYSTEM_FONT_COUNT_MAX U8_MAX

#define SYSTEM_FONT_DEFAULT_SIZE 20
#define SYSTEM_FONT_SIZE_MIN 1U
#define SYSTEM_FONT_SIZE_MAX U16_MAX

// For system fonts.
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"
#include <runtime_defines.h>

// Represents individual font data, used for a bitmap font or a system font variant.
typedef struct font_data {
    kname face_name;
    u32 size;
    i32 line_height;
    i32 baseline;
    i32 atlas_size_x;
    i32 atlas_size_y;
    u32 glyph_count;
    font_glyph* glyphs;
    u32 kerning_count;
    font_kerning* kernings;
    f32 tab_x_advance;
} font_data;

typedef struct bitmap_font_page {
    kresource_texture* atlas;
} bitmap_font_page;

typedef struct bitmap_font_lookup {
    // Used for handle lookups to determine stale handles.
    u64 uniqueid;
    font_data data;
    u32 page_count;
    bitmap_font_page* pages;
} bitmap_font_lookup;

typedef struct system_font_variant_data {
    // Used for handle lookups to determine stale handles.
    u64 uniqueid;
    // darray
    i32* codepoints;
    f32 scale;
    font_data data;
    kresource_texture* atlas;
} system_font_variant_data;

typedef struct system_font_lookup {
    // Used for making sure handles within the base aren't stale.
    u64 uniqueid;
    // darray
    system_font_variant_data* size_variants;
    // A copy of all this is kept for each for convenience.
    u64 binary_size;
    kname face;
    void* font_binary;
    i32 offset;
    i32 index;
    stbtt_fontinfo info;
} system_font_lookup;

typedef struct font_system_state {
    font_system_config config;
    bitmap_font_lookup* bitmap_fonts;
    system_font_lookup* system_fonts;
} font_system_state;

static bitmap_font_lookup* get_bitmap_font_lookup(font_system_state* state, khandle base_font);
static bitmap_font_lookup* get_bitmap_font_lookup_by_name(font_system_state* state, kname font_name, u64* out_resource_index);
static system_font_lookup* get_system_font_lookup(font_system_state* state, khandle base_font);
static system_font_lookup* get_system_font_lookup_by_name(font_system_state* state, kname font_name, u64* out_resource_index);
static system_font_variant_data* get_system_font_variant_by_handle(font_system_state* state, system_font_lookup* base_font, khandle variant);
static system_font_variant_data* get_system_font_variant_by_size(font_system_state* state, system_font_lookup* base_font, u16 size, b8 auto_create, u64* out_resource_index);
static vec2 measure_string(font_data* font, const char* text);
static void setup_tab_xadvance(font_data* font);
static void cleanup_font_data(font_data* font);
static b8 create_system_font_variant(system_font_lookup* lookup, u16 size, kname font_name, system_font_variant_data* out_variant);
static b8 rebuild_system_font_variant_atlas(system_font_lookup* lookup, system_font_variant_data* variant);
static b8 verify_system_font_size_variant(system_font_lookup* lookup, system_font_variant_data* variant, const char* text);
static void bitmap_font_release(font_system_state* state, bitmap_font_lookup* lookup);
static void system_font_release(font_system_state* state, system_font_lookup* lookup);
static font_glyph* glyph_from_codepoint(const font_data* font, i32 codepoint);
static font_kerning* kerning_from_codepoints(const font_data* font, i32 codepoint_0, i32 codepoint_1);
static b8 generate_font_geometry(const font_data* data, font_type type, const char* text, font_geometry* pending_data);

b8 font_system_deserialize_config(const char* config_str, font_system_config* out_config) {
    if (!config_str || !out_config) {
        KERROR("Font system config requires both a configuration string and a valid pointer to hold the config.");
        return false;
    }

    kson_tree tree = {0};
    if (!kson_tree_from_string(config_str, &tree)) {
        KERROR("Failed to parse font system config.");
        return false;
    }

    // Get max bitmap font count.
    {
        i64 max_bitmap_font_count = 25; // default is 25
        kson_object_property_value_get_int(&tree.root, "max_bitmap_font_count", &max_bitmap_font_count);
        if (max_bitmap_font_count < BITMAP_FONT_MAX_COUNT_MIN || max_bitmap_font_count > BITMAP_FONT_MAX_COUNT_MAX) {
            KWARN("Max bitmap font count is outside acceptable range of %u-%u and will be clamped.", BITMAP_FONT_MAX_COUNT_MIN, BITMAP_FONT_MAX_COUNT_MAX);
        }
        out_config->max_bitmap_font_count = (u8)KCLAMP(max_bitmap_font_count, BITMAP_FONT_MAX_COUNT_MIN, BITMAP_FONT_MAX_COUNT_MAX);
    }

    // Get max system font count.
    {
        i64 max_system_font_count = 25; // default is 25
        if (max_system_font_count < SYSTEM_FONT_MAX_COUNT_MIN || max_system_font_count > SYSTEM_FONT_MAX_COUNT_MAX) {
            KWARN("Max system font count is outside acceptable range of %u-%u and will be clamped.", SYSTEM_FONT_MAX_COUNT_MIN, SYSTEM_FONT_MAX_COUNT_MAX);
        }
        kson_object_property_value_get_int(&tree.root, "max_system_font_count", &max_system_font_count);
        out_config->max_system_font_count = (u8)KCLAMP(max_system_font_count, SYSTEM_FONT_MAX_COUNT_MIN, SYSTEM_FONT_MAX_COUNT_MAX);
    }

    // Get configured bitmap fonts.
    {
        kson_array bitmap_fonts_array;
        if (kson_object_property_value_get_array(&tree.root, "bitmap_fonts", &bitmap_fonts_array)) {
            u32 bitmap_font_count = 0;
            kson_array_element_count_get(&bitmap_fonts_array, &bitmap_font_count);

            if (bitmap_font_count) {
                if (bitmap_font_count < BITMAP_FONT_COUNT_MIN || bitmap_font_count > BITMAP_FONT_COUNT_MAX) {
                    KWARN("Bitmap font configured count is outside acceptable range of %u-%u and will be clamped, meaning only the list may be clipped.", BITMAP_FONT_COUNT_MIN, BITMAP_FONT_COUNT_MAX);
                }
                out_config->bitmap_font_count = (u8)KCLAMP(bitmap_font_count, BITMAP_FONT_COUNT_MIN, BITMAP_FONT_COUNT_MAX);
                out_config->bitmap_fonts = KALLOC_TYPE_CARRAY(font_system_bitmap_font_config, out_config->bitmap_font_count);
                for (u8 i = 0; i < out_config->bitmap_font_count; ++i) {
                    kson_object src = {0};
                    if (!kson_array_element_value_get_object(&bitmap_fonts_array, i, &src)) {
                        KWARN("Failed to get object at array index %u. Skipping.", i);
                        continue;
                    }
                    font_system_bitmap_font_config* target = &out_config->bitmap_fonts[i];

                    // Resource name is required.
                    if (!kson_object_property_value_get_string_as_kname(&src, "resource_name", &target->resource_name)) {
                        KERROR("resource_name is required. Bitmap font config will be skipped.");
                        continue;
                    }

                    // Package name is required.
                    if (!kson_object_property_value_get_string_as_kname(&src, "package_name", &target->package_name)) {
                        KERROR("package_name is required. Bitmap font config will be skipped.");
                        continue;
                    }
                }
            }
        }
    }

    // Get configured system fonts.
    {
        kson_array system_fonts_array;
        if (kson_object_property_value_get_array(&tree.root, "system_fonts", &system_fonts_array)) {
            u32 system_font_count = 0;
            kson_array_element_count_get(&system_fonts_array, &system_font_count);

            if (system_font_count) {
                if (system_font_count < SYSTEM_FONT_COUNT_MIN || system_font_count > SYSTEM_FONT_COUNT_MAX) {
                    KWARN("System font configured count is outside acceptable range of %u-%u and will be clamped, meaning only the list may be clipped.", SYSTEM_FONT_COUNT_MIN, SYSTEM_FONT_COUNT_MAX);
                }
                out_config->system_font_count = (u8)KCLAMP(system_font_count, SYSTEM_FONT_COUNT_MIN, SYSTEM_FONT_COUNT_MAX);
                out_config->system_fonts = KALLOC_TYPE_CARRAY(font_system_system_font_config, out_config->system_font_count);
                for (u8 i = 0; i < out_config->system_font_count; ++i) {
                    kson_object src = {0};
                    if (!kson_array_element_value_get_object(&system_fonts_array, i, &src)) {
                        KWARN("Failed to get object at array index %u. Skipping.", i);
                        continue;
                    }
                    font_system_system_font_config* target = &out_config->system_fonts[i];

                    // Resource name is required.
                    if (!kson_object_property_value_get_string_as_kname(&src, "resource_name", &target->resource_name)) {
                        KERROR("resource_name is required. System font config will be skipped.");
                        continue;
                    }

                    // Package name is required.
                    if (!kson_object_property_value_get_string_as_kname(&src, "package_name", &target->package_name)) {
                        KERROR("package_name is required. System font config will be skipped.");
                        continue;
                    }

                    // Default size is not required.
                    target->default_size = SYSTEM_FONT_DEFAULT_SIZE;
                    i64 default_size = 0;
                    if (kson_object_property_value_get_int(&src, "default_size", &default_size)) {
                        if (default_size < SYSTEM_FONT_SIZE_MIN || default_size > SYSTEM_FONT_SIZE_MAX) {
                            KWARN("System font default size is outside acceptable range of %u-%u and will be clamped.", SYSTEM_FONT_SIZE_MIN, SYSTEM_FONT_SIZE_MAX);
                        }
                        target->default_size = (u16)KCLAMP(default_size, SYSTEM_FONT_SIZE_MIN, SYSTEM_FONT_SIZE_MAX);
                    }
                }
            }
        }
    }

    kson_tree_cleanup(&tree);

    return true;
}

b8 font_system_initialize(u64* memory_requirement, void* memory, font_system_config* config) {
    font_system_config* typed_config = (font_system_config*)config;

    // Block of memory will contain state structure, then blocks for arrays, then blocks for hashtables.
    u64 struct_requirement = sizeof(font_system_state);
    u64 bmp_array_requirement = sizeof(bitmap_font_lookup) * config->max_bitmap_font_count;
    u64 sys_array_requirement = sizeof(system_font_lookup) * config->max_system_font_count;
    *memory_requirement = struct_requirement + bmp_array_requirement + sys_array_requirement;

    if (!memory) {
        return true;
    }

    font_system_state* state = (font_system_state*)memory;
    kzero_memory(state, sizeof(font_system_state));
    state->config = *typed_config;

    // The array blocks are after the state. Already allocated, so just set the pointer.
    void* bmp_array_block = (void*)(((u8*)memory) + struct_requirement);
    void* sys_array_block = (void*)(((u8*)bmp_array_block) + bmp_array_requirement);

    state->bitmap_fonts = bmp_array_block;
    state->system_fonts = sys_array_block;

    // Invalidate all entries in both arrays.
    kzero_memory(state->bitmap_fonts, sizeof(bitmap_font_lookup) * config->max_bitmap_font_count);
    for (u32 i = 0; i < config->max_bitmap_font_count; ++i) {
        state->bitmap_fonts[i].uniqueid = INVALID_ID_U64;
    }
    kzero_memory(state->system_fonts, sizeof(system_font_lookup) * config->max_system_font_count);
    for (u32 i = 0; i < config->max_system_font_count; ++i) {
        state->system_fonts[i].uniqueid = INVALID_ID_U64;
    }

    // Load configured bitmap fonts.
    {
        for (u32 i = 0; i < state->config.bitmap_font_count; ++i) {
            font_system_bitmap_font_config* c = &state->config.bitmap_fonts[i];
            if (!font_system_bitmap_font_load(state, c->resource_name, c->package_name)) {
                KERROR("Failed to load configured bitmap font (resource_name='%s', package_name='%s'. See logs for details.)", kname_string_get(c->resource_name), kname_string_get(c->package_name));
            }
        }
    }

    // Load configured system fonts.
    {
        for (u32 i = 0; i < state->config.system_font_count; ++i) {
            font_system_system_font_config* c = &state->config.system_fonts[i];
            if (!font_system_system_font_load(state, c->resource_name, c->package_name, c->default_size)) {
                KERROR("Failed to load configured system font (resource_name='%s', package_name='%s'. See logs for details.)", kname_string_get(c->resource_name), kname_string_get(c->package_name));
            }
        }
    }

    return true;
}

void font_system_shutdown(font_system_state* state) {
    if (!state) {
        return;
    }
    // Cleanup bitmap fonts.
    for (u16 i = 0; i < state->config.max_bitmap_font_count; ++i) {
        bitmap_font_lookup* lookup = &state->bitmap_fonts[i];
        if (lookup->uniqueid != INVALID_ID_U64) {
            bitmap_font_release(state, lookup);
        }
    }

    // Allocated as part of the state block, so won't need freeing here.
    state->bitmap_fonts = 0;

    // Cleanup system fonts.
    for (u16 i = 0; i < state->config.max_system_font_count; ++i) {
        system_font_lookup* lookup = &state->system_fonts[i];
        if (lookup->uniqueid != INVALID_ID_U64) {
            system_font_release(state, lookup);
        }
    }
    // Allocated as part of the state block, so won't need freeing here.
    state->system_fonts = 0;
}

b8 font_system_bitmap_font_acquire(font_system_state* state, kname font_name, khandle* out_font) {
    if (!out_font) {
        KERROR("A valid pointer to hold a system font variant is required.");
        return false;
    }

    // Return if it exists.
    u64 resource_index = INVALID_ID_U64;
    bitmap_font_lookup* lookup = get_bitmap_font_lookup_by_name(state, font_name, &resource_index);
    if (lookup) {
        *out_font = khandle_create_with_u64_identifier(resource_index, state->bitmap_fonts[resource_index].uniqueid);
        return true;
    }

    KERROR("A bitmap font named '%s' is not registered with the font system. Did you add it to your app_config's systems.font.config.bitmap_fonts array?", kname_string_get(font_name));
    return false;
}

b8 font_system_bitmap_font_load(font_system_state* state, kname resource_name, kname package_name) {
    khandle out_handle = khandle_invalid();

    // Font not found, need to load, so start by finding a free slot.
    for (u32 i = 0; i < state->config.max_bitmap_font_count; ++i) {
        if (state->bitmap_fonts[i].uniqueid == INVALID_ID_U64) {
            out_handle = khandle_create(i);
            state->bitmap_fonts[i].uniqueid = out_handle.unique_id.uniqueid;
            break;
        }
    }

    if (khandle_is_invalid(out_handle)) {
        KERROR("A new bitmap font could not be loaded since all slots are occupied. Increase your app_config's systems.font.config.max_bitmap_font_count to allow more to be loaded.");
        return false;
    }

    // Get the lookup.
    bitmap_font_lookup* lookup = get_bitmap_font_lookup(state, out_handle);

    // Request the resource synchronously.
    kresource_bitmap_font_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_BITMAP_FONT;
    request.base.synchronous = true; // Always load fonts synchronously.
    request.base.assets = array_kresource_asset_info_create(1);
    kresource_asset_info* asset = &request.base.assets.data[0];
    asset->type = KASSET_TYPE_BITMAP_FONT;
    asset->asset_name = resource_name;
    asset->package_name = package_name;
    asset->watch_for_hot_reload = false;
    kresource_bitmap_font* font_resource = (kresource_bitmap_font*)kresource_system_request(engine_systems_get()->kresource_state, resource_name, (kresource_request_info*)&request);
    if (!font_resource) {
        KERROR("Failed to load bitmap font resource. See logs for details.");
        return false;
    }

    KTRACE("Loading bitmap font '%s'...", kname_string_get(font_resource->face));

    // Take base properties.
    lookup->data.face_name = font_resource->face;
    lookup->data.baseline = font_resource->baseline;
    lookup->data.line_height = font_resource->line_height;
    lookup->data.size = font_resource->size;
    lookup->data.atlas_size_x = font_resource->atlas_size_x;
    lookup->data.atlas_size_y = font_resource->atlas_size_y;

    // Take a copy of the glyphs.
    lookup->data.glyph_count = font_resource->glyphs.base.length;
    if (font_resource->glyphs.base.length) {
        lookup->data.glyphs = KALLOC_TYPE_CARRAY(font_glyph, font_resource->glyphs.base.length);
        kcopy_memory(lookup->data.glyphs, font_resource->glyphs.data, sizeof(font_glyph) * font_resource->glyphs.base.length);
    }

    // Take a copy of the kernings.
    lookup->data.kerning_count = font_resource->kernings.base.length;
    if (font_resource->kernings.base.length) {
        lookup->data.kernings = KALLOC_TYPE_CARRAY(font_kerning, font_resource->kernings.base.length);
        kcopy_memory(lookup->data.kernings, font_resource->kernings.data, sizeof(font_kerning) * font_resource->kernings.base.length);
    }

    // Setup pages, request atlas textures for each.
    lookup->page_count = font_resource->pages.base.length;
    if (font_resource->pages.base.length) {
        lookup->pages = KALLOC_TYPE_CARRAY(bitmap_font_page, font_resource->pages.base.length);
        for (u32 i = 0; i < lookup->page_count; ++i) {
            lookup->pages[i].atlas = texture_system_request(font_resource->pages.data[i].image_asset_name, INVALID_KNAME, 0, 0);
            // If lookup fails, use default texture instead.
            if (!lookup->pages[i].atlas) {
                KWARN("Failed to request bitmap font atlas texture. Using a default texture instead, but text will not render correctly.");
                lookup->pages[i].atlas = texture_system_request(kname_create(DEFAULT_TEXTURE_NAME), INVALID_KNAME, 0, 0);
            }
        }
    }

    // Setup the font data.
    setup_tab_xadvance(&lookup->data);

    // Release the font resource.
    kresource_system_release(engine_systems_get()->kresource_state, font_resource->base.name);

    return true;
}

b8 font_system_bitmap_font_measure_string(struct font_system_state* state, khandle font, const char* text, vec2* out_size) {
    if (!out_size) {
        KERROR("font_system_bitmap_font_measure_string requires a valid pointer to out_size");
        return false;
    }

    bitmap_font_lookup* base_font = get_bitmap_font_lookup(state, font);
    if (!base_font) {
        KERROR("font_system_bitmap_font_measure_string: Unable to find bitmap font. Cannot measure.");
        return false;
    }

    *out_size = measure_string(&base_font->data, text);
    return true;
}

kresource_texture* font_system_bitmap_font_atlas_get(struct font_system_state* state, khandle font) {
    bitmap_font_lookup* base_font = get_bitmap_font_lookup(state, font);
    if (!base_font) {
        KERROR("font_system_bitmap_font_measure_string: Unable to find bitmap font. Cannot measure.");
        return 0;
    }

    // FIXME: Need to handle multiple pages... eventually.
    return base_font->pages[0].atlas;
}

f32 font_system_bitmap_font_line_height_get(struct font_system_state* state, khandle font) {
    bitmap_font_lookup* base_font = get_bitmap_font_lookup(state, font);
    if (!base_font) {
        KERROR("font_system_bitmap_font_measure_string: Unable to find bitmap font. Cannot measure.");
        return 0;
    }

    return (f32)base_font->data.line_height;
}

b8 font_system_bitmap_font_generate_geometry(struct font_system_state* state, khandle font, const char* text, font_geometry* out_geometry) {
    bitmap_font_lookup* base_font = get_bitmap_font_lookup(state, font);
    if (!base_font) {
        return false;
    }

    return generate_font_geometry(&base_font->data, FONT_TYPE_BITMAP, text, out_geometry);
}

b8 font_system_system_font_acquire(font_system_state* state, kname font_name, u16 font_size, system_font_variant* out_variant) {
    if (!out_variant) {
        KERROR("A valid pointer to hold a system font variant is required.");
        return false;
    }

    // See if the base font exists first.
    u64 base_font_resource_index = INVALID_ID_U64;
    system_font_lookup* base_font = get_system_font_lookup_by_name(state, font_name, &base_font_resource_index);
    if (base_font) {
        u64 variant_resource_index = INVALID_ID_U64;
        // Attempt to get the size variant. Create if does not exist.
        system_font_variant_data* variant = get_system_font_variant_by_size(state, base_font, font_size, true, &variant_resource_index);
        if (!variant) {
            KERROR("Failed to find and/or create size variant within system font '%s', font_size=%hu", kname_string_get(font_name), font_size);
            return false;
        }

        // Setup the handles.
        out_variant->base_font = khandle_create_with_u64_identifier(base_font_resource_index, base_font->uniqueid);
        out_variant->variant = khandle_create_with_u64_identifier(variant_resource_index, variant->uniqueid);
        return true;
    }

    KERROR("A base system font named '%s' was not found nor could it be loaded. Font acquisition failed.", kname_string_get(font_name));

    return false;
}

b8 font_system_system_font_load(font_system_state* state, kname resource_name, kname package_name, u16 default_size) {

    // Request the resource synchronously.
    kresource_system_font_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_SYSTEM_FONT;
    request.base.synchronous = true; // Always load fonts synchronously.
    request.base.assets = array_kresource_asset_info_create(1);
    kresource_asset_info* asset = &request.base.assets.data[0];
    asset->type = KASSET_TYPE_SYSTEM_FONT;
    asset->asset_name = resource_name;
    asset->package_name = package_name;
    asset->watch_for_hot_reload = false;
    kresource_system_font* font_resource = (kresource_system_font*)kresource_system_request(engine_systems_get()->kresource_state, resource_name, (kresource_request_info*)&request);
    if (!font_resource) {
        KERROR("Failed to load system font resource. See logs for details.");
        return false;
    }

    // Loop through the faces and create one lookup for each, as well as a default size
    // variant for each lookup.
    for (u32 source_face_idx = 0; source_face_idx < font_resource->face_count; ++source_face_idx) {
        u32 sys_font_count = darray_length(state->system_fonts);
        kname source_face = font_resource->faces[source_face_idx];
        b8 face_already_exists = false;
        for (u32 f = 0; f < sys_font_count; ++f) {
            // Make sure a font with this name doesn't already exist. If it does move on to the next.
            if (state->system_fonts[f].face == source_face) {
                KWARN("A font named '%s' already exists and will not be loaded again.", kname_string_get(source_face));
                face_already_exists = true;
                break;
            }
        }

        // Proceed with the load otherwise.
        if (!face_already_exists) {
            system_font_lookup* lookup = 0;

            // Start by finding a free slot.
            for (u32 i = 0; i < state->config.max_system_font_count; ++i) {
                if (state->system_fonts[i].uniqueid == INVALID_ID_U64) {
                    lookup = &state->system_fonts[i];
                    break;
                }
            }

            if (lookup) {
                KTRACE("Loading system font '%s'...", kname_string_get(source_face));
                lookup->face = source_face;
                lookup->binary_size = font_resource->font_binary_size;
                lookup->index = source_face_idx;
                // Take a copy of the binary data.
                // FIXME: Maybe only keep one copy of this in a table, with an id, and look it up.
                lookup->font_binary = kallocate(lookup->binary_size, MEMORY_TAG_SYSTEM_FONT);
                kcopy_memory(lookup->font_binary, font_resource->font_binary, lookup->binary_size);

                // The offset
                lookup->offset = stbtt_GetFontOffsetForIndex(lookup->font_binary, lookup->index);
                i32 result = stbtt_InitFont(&lookup->info, lookup->font_binary, lookup->offset);
                // Zero indicates failure.
                if (result == 0) {
                    KERROR("Failed to init system font face '%s' at index %i. Skipping it.", kname_string_get(lookup->face), lookup->index);

                    // Reset the lookup before moving on.
                    kzero_memory(lookup, sizeof(system_font_lookup));
                    lookup->uniqueid = INVALID_ID_U64;
                    continue;
                }

                // Create a default size variant.
                {
                    system_font_variant_data default_variant = {0};
                    if (!create_system_font_variant(lookup, default_size, lookup->face, &default_variant)) {
                        KERROR("Failed to create system font '%s' default size variant: %hu, index %u. Skipping it.", kname_string_get(lookup->face), default_size, lookup->index);

                        // Reset the lookup before moving on.
                        kzero_memory(lookup, sizeof(system_font_lookup));
                        lookup->uniqueid = INVALID_ID_U64;
                        continue;
                    }

                    // To hold the size variants.
                    lookup->size_variants = darray_create(system_font_variant_data);

                    // Add to the lookup's size variants.
                    darray_push(lookup->size_variants, default_variant);
                }

                // Create a handle for the lookup and sync ids.
                khandle face_handle = khandle_create(lookup->index);
                lookup->uniqueid = face_handle.unique_id.uniqueid;
            } else {
                KERROR("Failed to find an empty slot for a new system font. Increase app_config.systems.font.config.max_system_font_count to fit more.");
                return false;
            }
        }
    }

    // Release the resource.
    kresource_system_release(engine_systems_get()->kresource_state, font_resource->base.name);

    return true;
}

b8 font_system_system_font_verify_atlas(font_system_state* state, system_font_variant variant, const char* text) {
    system_font_lookup* base_font = get_system_font_lookup(state, variant.base_font);
    if (!base_font) {
        KERROR("font_system_verify_system_font_atlas: Unable to find base system font. Cannot verify.");
        return false;
    }

    system_font_variant_data* v = get_system_font_variant_by_handle(state, base_font, variant.variant);
    if (!v) {
        KERROR("font_system_verify_system_font_atlas: Unable to find system font size variant. Cannot verify.");
        return false;
    }

    return verify_system_font_size_variant(base_font, v, text);
}

b8 font_system_system_font_measure_string(struct font_system_state* state, system_font_variant variant, const char* text, vec2* out_size) {
    if (!out_size) {
        KERROR("font_system_system_font_measure_string requires a valid pointer to out_size");
        return false;
    }

    system_font_lookup* base_font = get_system_font_lookup(state, variant.base_font);
    if (!base_font) {
        KERROR("font_system_system_font_measure_string: Unable to find base system font. Cannot measure.");
        return false;
    }

    system_font_variant_data* v = get_system_font_variant_by_handle(state, base_font, variant.variant);
    if (!v) {
        KERROR("font_system_system_font_measure_string: Unable to find system font size variant. Cannot verify.");
        return false;
    }

    *out_size = measure_string(&v->data, text);
    return true;
}

f32 font_system_system_font_line_height_get(struct font_system_state* state, system_font_variant variant) {
    system_font_lookup* base_font = get_system_font_lookup(state, variant.base_font);
    if (!base_font) {
        return 0;
    }

    system_font_variant_data* var = get_system_font_variant_by_handle(state, base_font, variant.variant);
    if (!var) {
        return 0;
    }

    return var->data.line_height;
}

b8 font_system_system_font_generate_geometry(struct font_system_state* state, system_font_variant variant, const char* text, font_geometry* out_geometry) {
    system_font_lookup* base_font = get_system_font_lookup(state, variant.base_font);
    if (!base_font) {
        return false;
    }

    system_font_variant_data* var = get_system_font_variant_by_handle(state, base_font, variant.variant);
    if (!var) {
        return false;
    }

    return generate_font_geometry(&var->data, FONT_TYPE_SYSTEM, text, out_geometry);
}

kresource_texture* font_system_system_font_atlas_get(struct font_system_state* state, system_font_variant variant) {
    system_font_lookup* base_font = get_system_font_lookup(state, variant.base_font);
    if (!base_font) {
        return 0;
    }

    system_font_variant_data* var = get_system_font_variant_by_handle(state, base_font, variant.variant);
    if (!var) {
        return 0;
    }

    return var->atlas;
}

static bitmap_font_lookup* get_bitmap_font_lookup(font_system_state* state, khandle base_font) {
    KASSERT_MSG(state, "state is required");

    if (khandle_is_valid(base_font) && khandle_is_pristine(base_font, state->bitmap_fonts[base_font.handle_index].uniqueid)) {
        return &state->bitmap_fonts[base_font.handle_index];
    }

    KERROR("Attempted to get bitmap font lookup using an invalid or stale handle. Null will be returned. (handle_index=%llu, unique_id=%llu)", base_font.handle_index, base_font.unique_id.uniqueid);
    return 0;
}

static bitmap_font_lookup* get_bitmap_font_lookup_by_name(font_system_state* state, kname font_name, u64* out_resource_index) {
    KASSERT_MSG(state, "state is required");
    KASSERT_MSG(out_resource_index, "out_resource_index is required");

    for (u32 i = 0; i < state->config.max_bitmap_font_count; ++i) {
        bitmap_font_lookup* lookup = &state->bitmap_fonts[i];
        if (lookup->data.face_name == font_name) {
            *out_resource_index = i;
            return lookup;
        }
    }

    *out_resource_index = INVALID_ID_U64;
    KERROR("Failed to find a bitmap font named '%s'. Null will be returned.", kname_string_get(font_name));
    return 0;
}

static system_font_lookup* get_system_font_lookup(font_system_state* state, khandle base_font) {
    KASSERT_MSG(state, "state is required");

    if (khandle_is_valid(base_font) && khandle_is_pristine(base_font, state->system_fonts[base_font.handle_index].uniqueid)) {
        return &state->system_fonts[base_font.handle_index];
    }

    KERROR("Attempted to get system font lookup using an invalid or stale handle. Null will be returned. (handle_index=%llu, unique_id=%llu)", base_font.handle_index, base_font.unique_id.uniqueid);
    return 0;
}

static system_font_lookup* get_system_font_lookup_by_name(font_system_state* state, kname font_name, u64* out_resource_index) {
    KASSERT_MSG(state, "state is required");
    KASSERT_MSG(out_resource_index, "out_resource_index is required");

    for (u32 i = 0; i < state->config.max_system_font_count; ++i) {
        system_font_lookup* lookup = &state->system_fonts[i];
        if (lookup->face == font_name) {
            *out_resource_index = i;
            return lookup;
        }
    }

    *out_resource_index = INVALID_ID_U64;
    KERROR("Failed to find a base system font named '%s'. Null will be returned.", kname_string_get(font_name));
    return 0;
}

static system_font_variant_data* get_system_font_variant_by_handle(font_system_state* state, system_font_lookup* base_font, khandle variant) {
    KASSERT_MSG(state, "state is required");
    KASSERT_MSG(base_font, "base_font is required");

    if (khandle_is_valid(variant) && khandle_is_pristine(variant, base_font->size_variants[variant.handle_index].uniqueid)) {
        return &base_font->size_variants[variant.handle_index];
    }

    KERROR("Attempted to get system font variant using an invalid or stale handle. Null will be returned. (handle_index=%llu, unique_id=%llu)", variant.handle_index, variant.unique_id.uniqueid);
    return 0;
}

static system_font_variant_data* get_system_font_variant_by_size(font_system_state* state, system_font_lookup* base_font, u16 size, b8 auto_create, u64* out_resource_index) {
    KASSERT_MSG(state, "state is required");
    KASSERT_MSG(base_font, "base_font is required");
    KASSERT_MSG(out_resource_index, "out_resource_index is required");

    // Found the font, attempt to get the variant.
    u32 variant_count = darray_length(base_font->size_variants);
    for (u32 v = 0; v < variant_count; ++v) {
        system_font_variant_data* variant = &base_font->size_variants[v];
        if (variant->data.size == size) {
            // Found the right variant.
            *out_resource_index = v;
            return variant;
        }
    }

    // No variant exists for that size. Create one?
    if (auto_create) {

        system_font_variant_data variant;
        if (!create_system_font_variant(base_font, size, base_font->face, &variant)) {
            KERROR("Failed to create system_font size variant - nothing will be returned. (face='%s', index=%u, size=%u)", base_font->face, base_font->index, size);
            return 0;
        }

        *out_resource_index = darray_length(base_font->size_variants);
        // Add to the lookup's size variants.
        darray_push(base_font->size_variants, variant);

        return &base_font->size_variants[(*out_resource_index)];
    }

    *out_resource_index = INVALID_ID_U64;
    KERROR("Attempted to get system font variant which was not found. Null will be returned. (face='%s', size=%hu, auto_create=%s)", kname_string_get(base_font->face), size, bool_to_string(auto_create));
    return 0;
}

static vec2 measure_string(font_data* font, const char* text) {
    vec2 extents = {0};

    u32 char_length = string_length(text);
    u32 text_length_utf8 = string_utf8_length(text);

    f32 x = 0;
    f32 y = 0;

    // Take the length in chars and get the correct codepoint from it.
    for (u32 c = 0; c < char_length; ++c) {
        i32 codepoint = text[c];

        // Continue to next line for newline.
        if (codepoint == '\n') {
            if (x > extents.x) {
                extents.x = x;
            }
            x = 0;
            y += font->line_height;
            continue;
        }

        if (codepoint == '\t') {
            x += font->tab_x_advance;
            continue;
        }

        // NOTE: UTF-8 codepoint handling.
        u8 advance = 0;
        if (!bytes_to_codepoint(text, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        font_glyph* g = 0;
        for (u32 i = 0; i < font->glyph_count; ++i) {
            if (font->glyphs[i].codepoint == codepoint) {
                g = &font->glyphs[i];
                break;
            }
        }

        if (!g) {
            // If not found, use the codepoint -1
            codepoint = -1;
            for (u32 i = 0; i < font->glyph_count; ++i) {
                if (font->glyphs[i].codepoint == codepoint) {
                    g = &font->glyphs[i];
                    break;
                }
            }
        }

        if (g) {
            // Try to find kerning
            i32 kerning = 0;

            // Get the offset of the next character. If there is no advance, move forward one,
            // otherwise use advance as-is.
            u32 offset = c + advance; //(advance < 1 ? 1 : advance);
            if (offset < text_length_utf8 - 1) {
                // Get the next codepoint.
                i32 next_codepoint = 0;
                u8 advance_next = 0;

                if (!bytes_to_codepoint(text, offset, &next_codepoint, &advance_next)) {
                    KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
                    codepoint = -1;
                } else {
                    for (u32 i = 0; i < font->kerning_count; ++i) {
                        font_kerning* k = &font->kernings[i];
                        if (k->codepoint_0 == codepoint && k->codepoint_1 == next_codepoint) {
                            kerning = k->amount;
                        }
                    }
                }
            }

            x += g->x_advance + kerning;
        } else {
            KERROR("Unable to find unknown codepoint. Skipping.");
            continue;
        }

        // Now advance c
        c += advance - 1; // Subtracting 1 because the loop always increments once for single-byte anyway.
    }

    // One last check in case of no more newlines.
    if (x > extents.x) {
        extents.x = x;
    }

    // Since y starts 0-based, we need to add one more to make it 1-line based.
    y += font->line_height;
    extents.y = y;

    return extents;
}

static void setup_tab_xadvance(font_data* font) {

    // Check for a t{ab glyph, as there may not always be one exported. If there is, store its
    // x_advance and just use that. If there is not, then create one based off spacex4
    if (!font->tab_x_advance) {
        for (u32 i = 0; i < font->glyph_count; ++i) {
            if (font->glyphs[i].codepoint == '\t') {
                font->tab_x_advance = font->glyphs[i].x_advance;
                break;
            }
        }
        // If still not found, use space x 4.
        if (!font->tab_x_advance) {
            for (u32 i = 0; i < font->glyph_count; ++i) {
                // Search for space
                if (font->glyphs[i].codepoint == ' ') {
                    font->tab_x_advance = font->glyphs[i].x_advance * 4;
                    break;
                }
            }
            if (!font->tab_x_advance) {
                // If _still_ not there, then a space wasn't present either, so just
                // hardcode something, in this case font size * 4.
                font->tab_x_advance = font->size * 4;
            }
        }
    }
}

static void cleanup_font_data(font_data* font) {

    if (font->glyphs && font->glyph_count) {
        KFREE_TYPE_CARRAY(font->glyphs, font_glyph, font->glyph_count);
    }

    if (font->kernings && font->kerning_count) {
        KFREE_TYPE_CARRAY(font->kernings, font_kerning, font->kerning_count);
    }
}

static b8 create_system_font_variant(system_font_lookup* lookup, u16 size, kname font_name, system_font_variant_data* out_variant) {
    kzero_memory(out_variant, sizeof(system_font_variant_data));
    out_variant->data.atlas_size_x = 1024; // TODO: configurable size
    out_variant->data.atlas_size_y = 1024;
    out_variant->data.size = size;
    out_variant->data.face_name = font_name;

    // Push default codepoints (ascii 32-127) always, plus a -1 for unknown.
    out_variant->codepoints = darray_reserve(i32, 96);
    darray_push(out_variant->codepoints, -1); // push invalid char
    for (i32 i = 0; i < 95; ++i) {
        out_variant->codepoints[i + 1] = i + 32;
    }
    darray_length_set(out_variant->codepoints, 96);

    // Create texture.
    const char* font_tex_name = string_format("__system_text_atlas_%s_i%i_sz%i__", kname_string_get(font_name), lookup->index, size);

    out_variant->atlas = texture_system_request_writeable(
        kname_create(font_tex_name),
        out_variant->data.atlas_size_x,
        out_variant->data.atlas_size_y,
        TEXTURE_FORMAT_RGBA8,
        true,
        false);
    string_free(font_tex_name);
    font_tex_name = 0;

    if (out_variant->atlas) {
        // Obtain some metrics
        out_variant->scale = stbtt_ScaleForPixelHeight(&lookup->info, (f32)size);
        i32 ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&lookup->info, &ascent, &descent, &line_gap);
        out_variant->data.line_height = (ascent - descent + line_gap) * out_variant->scale;

        // Also perform tab xadvance setup for the variant
        setup_tab_xadvance(&out_variant->data);

        // Build the variant atlas.
        return rebuild_system_font_variant_atlas(lookup, out_variant);
    }

    KERROR("Request for writeable font texture atlas resource failed. See logs for details.");
    return false;
}

static b8 rebuild_system_font_variant_atlas(system_font_lookup* lookup, system_font_variant_data* variant) {
    system_font_variant_data* internal_data = variant;

    u32 pack_image_size = variant->data.atlas_size_x * variant->data.atlas_size_y * sizeof(u8);
    u8* pixels = kallocate(pack_image_size, MEMORY_TAG_ARRAY);
    u32 codepoint_count = darray_length(internal_data->codepoints);
    stbtt_packedchar* packed_chars = kallocate(sizeof(stbtt_packedchar) * codepoint_count, MEMORY_TAG_ARRAY);

    // Begin packing all known characters into the atlas. This
    // creates a single-channel image with rendered glyphs at the
    // given size.
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, pixels, variant->data.atlas_size_x, variant->data.atlas_size_y, 0, 1, 0)) {
        KERROR("stbtt_PackBegin failed");
        return false;
    }

    // Fit all codepoints into a single range for packing.
    stbtt_pack_range range;
    range.first_unicode_codepoint_in_range = 0;
    range.font_size = variant->data.size;
    range.num_chars = codepoint_count;
    range.chardata_for_range = packed_chars;
    range.array_of_unicode_codepoints = internal_data->codepoints;
    if (!stbtt_PackFontRanges(&context, lookup->font_binary, lookup->index, &range, 1)) {
        KERROR("stbtt_PackFontRanges failed");
        return false;
    }

    stbtt_PackEnd(&context);
    // Packing complete.

    // Convert from single-channel to RGBA, or pack_image_size * 4.
    u8* rgba_pixels = kallocate(pack_image_size * 4, MEMORY_TAG_ARRAY);
    for (u32 j = 0; j < pack_image_size; ++j) {
        rgba_pixels[(j * 4) + 0] = pixels[j];
        rgba_pixels[(j * 4) + 1] = pixels[j];
        rgba_pixels[(j * 4) + 2] = pixels[j];
        rgba_pixels[(j * 4) + 3] = pixels[j];
    }

    // Write texture data to atlas.
    if (!renderer_texture_write_data(
            engine_systems_get()->renderer_system,
            variant->atlas->renderer_texture_handle,
            0, pack_image_size * 4, rgba_pixels)) {
        KERROR("Failed to write data to system font variant texture");
        return false;
    }

    // Free pixel/rgba_pixel data.
    kfree(pixels, pack_image_size, MEMORY_TAG_ARRAY);
    kfree(rgba_pixels, pack_image_size * 4, MEMORY_TAG_ARRAY);

    // Regenerate glyphs
    if (variant->data.glyphs && variant->data.glyph_count) {
        kfree(variant->data.glyphs, sizeof(font_glyph) * variant->data.glyph_count, MEMORY_TAG_ARRAY);
    }
    variant->data.glyph_count = codepoint_count;
    variant->data.glyphs = kallocate(sizeof(font_glyph) * codepoint_count, MEMORY_TAG_ARRAY);
    for (u16 i = 0; i < variant->data.glyph_count; ++i) {
        stbtt_packedchar* pc = &packed_chars[i];
        font_glyph* g = &variant->data.glyphs[i];
        g->codepoint = internal_data->codepoints[i];
        g->page_id = 0;
        g->x_offset = pc->xoff;
        g->y_offset = pc->yoff;
        g->x = pc->x0; // xmin;
        g->y = pc->y0;
        g->width = pc->x1 - pc->x0;
        g->height = pc->y1 - pc->y0;
        g->x_advance = pc->xadvance;
    }

    // Regenerate kernings
    if (variant->data.kernings && variant->data.kerning_count) {
        kfree(variant->data.kernings, sizeof(font_kerning) * variant->data.kerning_count, MEMORY_TAG_ARRAY);
    }
    variant->data.kerning_count = stbtt_GetKerningTableLength(&lookup->info);
    if (variant->data.kerning_count) {
        variant->data.kernings = kallocate(sizeof(font_kerning) * variant->data.kerning_count, MEMORY_TAG_ARRAY);
        // Get the kerning table for the current font.
        stbtt_kerningentry* kerning_table = kallocate(sizeof(stbtt_kerningentry) * variant->data.kerning_count, MEMORY_TAG_ARRAY);
        u32 entry_count = stbtt_GetKerningTable(&lookup->info, kerning_table, variant->data.kerning_count);
        if (entry_count != variant->data.kerning_count) {
            KERROR("Kerning entry count mismatch: %u->%u", entry_count, variant->data.kerning_count);
            return false;
        }

        for (u32 i = 0; i < variant->data.kerning_count; ++i) {
            font_kerning* k = &variant->data.kernings[i];
            k->codepoint_0 = kerning_table[i].glyph1;
            k->codepoint_1 = kerning_table[i].glyph2;
            k->amount = kerning_table[i].advance;
        }

        kfree(kerning_table, sizeof(stbtt_kerningentry) * variant->data.kerning_count, MEMORY_TAG_ARRAY);
    } else {
        variant->data.kernings = 0;
    }

    return true;
}

static b8 verify_system_font_size_variant(system_font_lookup* lookup, system_font_variant_data* variant, const char* text) {
    system_font_variant_data* internal_data = variant;

    u32 char_length = string_length(text);
    u32 added_codepoint_count = 0;
    for (u32 i = 0; i < char_length;) {
        i32 codepoint;
        u8 advance;
        if (!bytes_to_codepoint(text, i, &codepoint, &advance)) {
            KERROR("bytes_to_codepoint failed to get codepoint.");
            ++i;
            continue;
        } else {
            // Check if the codepoint is already contained. Note that ascii
            // codepoints are always included, so checking those may be skipped.
            i += advance;
            if (codepoint < 128) {
                continue;
            }
            u32 codepoint_count = darray_length(internal_data->codepoints);
            b8 found = false;
            for (u32 j = 95; j < codepoint_count; ++j) {
                if (internal_data->codepoints[j] == codepoint) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                darray_push(internal_data->codepoints, codepoint);
                added_codepoint_count++;
            }
        }
    }

    // If codepoints were added, rebuild the atlas.
    if (added_codepoint_count > 0) {
        return rebuild_system_font_variant_atlas(lookup, variant);
    }

    // Otherwise, proceed as normal.
    return true;
}

static void bitmap_font_release(font_system_state* state, bitmap_font_lookup* lookup) {
    if (state) {

        // Destroy pages.
        if (lookup->pages && lookup->page_count) {
            for (u32 i = 0; i < lookup->page_count; ++i) {
                // Release atlas
                if (lookup->pages[i].atlas) {
                    texture_system_release_resource(lookup->pages[i].atlas);
                }
            }
            KFREE_TYPE_CARRAY(lookup->pages, bitmap_font_page, lookup->page_count);
            lookup->pages = 0;
            lookup->page_count = 0;
        }

        cleanup_font_data(&lookup->data);

        lookup->uniqueid = INVALID_ID_U64;
    }
}

static void system_font_release(font_system_state* state, system_font_lookup* lookup) {
    if (state) {
        // Destroy all size variants.
        u32 variant_count = darray_length(lookup->size_variants);
        for (u32 i = 0; i < variant_count; ++i) {
            system_font_variant_data* v = &lookup->size_variants[i];
            if (v->atlas) {
                texture_system_release_resource(v->atlas);
            }

            if (v->codepoints) {
                darray_destroy(v->codepoints);
            }

            cleanup_font_data(&v->data);
        }

        if (lookup->binary_size && lookup->font_binary) {
            kfree(lookup->font_binary, lookup->binary_size, MEMORY_TAG_SYSTEM_FONT);
        }
    }
}

static font_glyph* glyph_from_codepoint(const font_data* font, i32 codepoint) {
    for (u32 i = 0; i < font->glyph_count; ++i) {
        if (font->glyphs[i].codepoint == codepoint) {
            return &font->glyphs[i];
        }
    }

    KERROR("Unable to find font glyph for codepoint: %s", codepoint);
    return 0;
}

static font_kerning* kerning_from_codepoints(const font_data* font, i32 codepoint_0, i32 codepoint_1) {
    for (u32 i = 0; i < font->kerning_count; ++i) {
        font_kerning* k = &font->kernings[i];
        if (k->codepoint_0 == codepoint_0 && k->codepoint_1 == codepoint_1) {
            return k;
        }
    }

    // No kerning found. This is okay, not necessarily an error.
    return 0;
}

static b8 generate_font_geometry(const font_data* data, font_type type, const char* text, font_geometry* out_geometry) {

    // Get the UTF-8 string length
    u32 text_length_utf8 = string_utf8_length(text);
    u32 char_length = string_length(text);

    // Iterate the string once and count how many quads are required. This allows
    // characters which don't require rendering (spaces, tabs, etc.) to be skipped.
    out_geometry->quad_count = 0;

    // If text is empty, resetting quad count is enough.
    if (text_length_utf8 < 1) {
        return true;
    }
    i32* codepoints = kallocate(sizeof(i32) * text_length_utf8, MEMORY_TAG_ARRAY);
    for (u32 c = 0, cp_idx = 0; c < char_length;) {
        i32 codepoint = text[c];
        u8 advance = 1;

        // Ensure the propert UTF-8 codepoint is being used.
        if (!bytes_to_codepoint(text, c, &codepoint, &advance)) {
            KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
            codepoint = -1;
        }

        // Whitespace codepoints do not need to be included in the quad count.
        if (!codepoint_is_whitespace(codepoint)) {
            out_geometry->quad_count++;
        }

        c += advance;

        // Add to the codepoint list.
        codepoints[cp_idx] = codepoint;
        cp_idx++;
    }

    // Calculate buffer sizes.
    static const u64 verts_per_quad = 4;
    static const u8 indices_per_quad = 6;

    // Save the data off to a pending structure.
    out_geometry->vertex_buffer_size = sizeof(vertex_2d) * verts_per_quad * out_geometry->quad_count;
    out_geometry->index_buffer_size = sizeof(u32) * indices_per_quad * out_geometry->quad_count;
    // Temp arrays to hold vertex/index data.
    out_geometry->vertex_buffer_data = kallocate(out_geometry->vertex_buffer_size, MEMORY_TAG_ARRAY);
    out_geometry->index_buffer_data = kallocate(out_geometry->index_buffer_size, MEMORY_TAG_ARRAY);

    // Generate new geometry for each character.
    f32 x = 0;
    f32 y = 0;

    // Iterate the codepoints list.
    for (u32 c = 0, q_idx = 0; c < text_length_utf8; ++c) {
        i32 codepoint = codepoints[c];

        // Whitespace doesn't get a quad created for it.
        if (codepoint == '\n') {
            // Newline needs to move to the next line and restart x position.
            x = 0;
            y += data->line_height;
            // No further processing needed.
            continue;
        } else if (codepoint == '\t') {
            // Manually move over by the configured tab advance amount.
            x += data->tab_x_advance;
            // No further processing needed.
            continue;
        }

        // Obtain the glyph.
        font_glyph* g = glyph_from_codepoint(data, codepoint);
        if (!g) {
            KERROR("Unable to find unknown codepoint. Using '?' instead.");
            g = glyph_from_codepoint(data, '?');
        }

        // If not on the last codepoint, try to find kerning between this and the next codepoint.
        i32 kerning_amount = 0;
        if (c < text_length_utf8 - 1) {
            i32 next_codepoint = codepoints[c + 1];
            // Try to find kerning
            font_kerning* kerning = kerning_from_codepoints(data, codepoint, next_codepoint);
            if (kerning) {
                kerning_amount = kerning->amount;
            }
        }

        // Only generate a quad for non-whitespace characters.
        if (!codepoint_is_whitespace(codepoint)) {
            // Generate points for the quad.
            f32 minx = x + g->x_offset;
            f32 miny = y + g->y_offset;
            f32 maxx = minx + g->width;
            f32 maxy = miny + g->height;
            f32 tminx = (f32)g->x / data->atlas_size_x;
            f32 tmaxx = (f32)(g->x + g->width) / data->atlas_size_x;
            f32 tminy = (f32)g->y / data->atlas_size_y;
            f32 tmaxy = (f32)(g->y + g->height) / data->atlas_size_y;
            // Flip the y axis for system text
            if (type == FONT_TYPE_SYSTEM) {
                tminy = 1.0f - tminy;
                tmaxy = 1.0f - tmaxy;
            }

            vertex_2d p0 = (vertex_2d){vec2_create(minx, miny), vec2_create(tminx, tminy)};
            vertex_2d p1 = (vertex_2d){vec2_create(maxx, miny), vec2_create(tmaxx, tminy)};
            vertex_2d p2 = (vertex_2d){vec2_create(maxx, maxy), vec2_create(tmaxx, tmaxy)};
            vertex_2d p3 = (vertex_2d){vec2_create(minx, maxy), vec2_create(tminx, tmaxy)};

            // Vertex data
            out_geometry->vertex_buffer_data[(q_idx * 4) + 0] = p0; // 0    3
            out_geometry->vertex_buffer_data[(q_idx * 4) + 1] = p2; //
            out_geometry->vertex_buffer_data[(q_idx * 4) + 2] = p3; //
            out_geometry->vertex_buffer_data[(q_idx * 4) + 3] = p1; // 2    1

            // Index data 210301
            out_geometry->index_buffer_data[(q_idx * 6) + 0] = (q_idx * 4) + 2;
            out_geometry->index_buffer_data[(q_idx * 6) + 1] = (q_idx * 4) + 1;
            out_geometry->index_buffer_data[(q_idx * 6) + 2] = (q_idx * 4) + 0;
            out_geometry->index_buffer_data[(q_idx * 6) + 3] = (q_idx * 4) + 3;
            out_geometry->index_buffer_data[(q_idx * 6) + 4] = (q_idx * 4) + 0;
            out_geometry->index_buffer_data[(q_idx * 6) + 5] = (q_idx * 4) + 1;

            // Increment quad index.
            q_idx++;
        }

        // Advance by the glyph's advance and kerning.
        x += g->x_advance + kerning_amount;
    }

    // Clean up.
    if (codepoints) {
        kfree(codepoints, sizeof(i32) * text_length_utf8, MEMORY_TAG_ARRAY);
    }

    return true;
}
