#include "font_system.h"

#include "containers/darray.h"
#include "containers/hashtable.h"
#include "core/engine.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/resource_system.h"
#include "systems/texture_system.h"

// For system fonts.
#define STB_TRUETYPE_IMPLEMENTATION
#include "vendor/stb_truetype.h"

typedef struct bitmap_font_internal_data {
    resource loaded_resource;
    // Casted pointer to resource data for convenience.
    bitmap_font_resource_data* resource_data;
} bitmap_font_internal_data;

typedef struct system_font_variant_data {
    // darray
    i32* codepoints;
    f32 scale;
} system_font_variant_data;

typedef struct bitmap_font_lookup {
    u16 id;
    u16 reference_count;
    bitmap_font_internal_data font;
} bitmap_font_lookup;

typedef struct system_font_lookup {
    u16 id;
    u16 reference_count;
    // darray
    font_data* size_variants;
    // A copy of all this is kept for each for convenience.
    u64 binary_size;
    char* face;
    void* font_binary;
    i32 offset;
    i32 index;
    stbtt_fontinfo info;
} system_font_lookup;

typedef struct font_system_state {
    font_system_config config;
    hashtable bitmap_font_lookup;
    hashtable system_font_lookup;
    bitmap_font_lookup* bitmap_fonts;
    system_font_lookup* system_fonts;
    void* bitmap_hashtable_block;
    void* system_hashtable_block;
} font_system_state;

static const u32 max_font_count = 101; // TODO: May need to find a better way to handle this.

static b8 setup_font_data(font_data* font);
static void cleanup_font_data(font_data* font);
static b8 create_system_font_variant(system_font_lookup* lookup, u16 size, const char* font_name, font_data* out_variant);
static b8 rebuild_system_font_variant_atlas(system_font_lookup* lookup, font_data* variant);
static b8 verify_system_font_size_variant(system_font_lookup* lookup, font_data* variant, const char* text);

static font_system_state* state_ptr;

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

    // Auto-release property. Optional, defaults to true if not provided.
    if (!kson_object_property_value_get_bool(&tree.root, "auto_release", &out_config->auto_release)) {
        out_config->auto_release = true;
    }

    // default_bitmap_font object is required.
    kson_object default_bitmap_font_obj;
    if (!kson_object_property_value_get_object(&tree.root, "default_bitmap_font", &default_bitmap_font_obj)) {
        KERROR("font_system_deserialize_config: config does not contain default_bitmap_font object, which is required.");
        return false;
    } else {
        // Font name.
        if (!kson_object_property_value_get_string(&default_bitmap_font_obj, "name", &out_config->default_bitmap_font.name)) {
            KERROR("Default bitmap font requires a 'name'.");
            return false;
        }

        // Font size is required for bitmap fonts.
        i64 font_size = 0;
        if (!kson_object_property_value_get_int(&default_bitmap_font_obj, "size", &font_size)) {
            KERROR("'size' is a required field for bitmap fonts.");
            return false;
        }
        out_config->default_bitmap_font.size = (u16)font_size;

        // Resource name.
        if (!kson_object_property_value_get_string(&default_bitmap_font_obj, "resource_name", &out_config->default_bitmap_font.resource_name)) {
            KERROR("Default bitmap font requires a 'resource_name'.");
            return false;
        }
    }

    // default_system_font object is required.
    kson_object default_system_font_obj;
    if (!kson_object_property_value_get_object(&tree.root, "default_system_font", &default_system_font_obj)) {
        KERROR("font_system_deserialize_config: config does not contain default_system_font object, which is required.");
        return false;
    } else {
        // Font name.
        if (!kson_object_property_value_get_string(&default_system_font_obj, "name", &out_config->default_system_font.name)) {
            KERROR("Default system font requires a 'name'.");
            return false;
        }

        // Font size is optional for system fonts. Use a default of 20 if not provided.
        i64 font_size = 0;
        if (!kson_object_property_value_get_int(&default_system_font_obj, "size", &font_size)) {
            font_size = 20;
        }
        out_config->default_system_font.default_size = (u16)font_size;

        // Resource name.
        if (!kson_object_property_value_get_string(&default_system_font_obj, "resource_name", &out_config->default_system_font.resource_name)) {
            KERROR("Default system font requires a 'resource_name'.");
            return false;
        }
    }

    kson_tree_cleanup(&tree);

    return true;
}

b8 font_system_initialize(u64* memory_requirement, void* memory, font_system_config* config) {
    font_system_config* typed_config = (font_system_config*)config;

    // Block of memory will contain state structure, then blocks for arrays, then blocks for hashtables.
    u64 struct_requirement = sizeof(font_system_state);
    u64 bmp_array_requirement = sizeof(bitmap_font_lookup) * max_font_count;
    u64 sys_array_requirement = sizeof(system_font_lookup) * max_font_count;
    u64 bmp_hashtable_requirement = sizeof(u16) * max_font_count;
    u64 sys_hashtable_requirement = sizeof(u16) * max_font_count;
    *memory_requirement = struct_requirement + bmp_array_requirement + sys_array_requirement + bmp_hashtable_requirement + sys_hashtable_requirement;

    if (!memory) {
        return true;
    }

    state_ptr = (font_system_state*)memory;
    state_ptr->config = *typed_config;

    // The array blocks are after the state. Already allocated, so just set the pointer.
    void* bmp_array_block = (void*)(((u8*)memory) + struct_requirement);
    void* sys_array_block = (void*)(((u8*)bmp_array_block) + bmp_array_requirement);

    state_ptr->bitmap_fonts = bmp_array_block;
    state_ptr->system_fonts = sys_array_block;

    // Hashtable blocks are after arrays.
    void* bmp_hashtable_block = (void*)(((u8*)sys_array_block) + sys_array_requirement);
    void* sys_hashtable_block = (void*)(((u8*)bmp_hashtable_block) + bmp_hashtable_requirement);

    // Create hashtables for font lookups.
    hashtable_create(sizeof(u16), max_font_count, bmp_hashtable_block, false, &state_ptr->bitmap_font_lookup);
    hashtable_create(sizeof(u16), max_font_count, sys_hashtable_block, false, &state_ptr->system_font_lookup);

    // Fill both hashtables with invalid references to use as a default.
    u16 invalid_id = INVALID_ID_U16;
    hashtable_fill(&state_ptr->bitmap_font_lookup, &invalid_id);
    hashtable_fill(&state_ptr->system_font_lookup, &invalid_id);

    // Invalidate all entries in both arrays.
    for (u32 i = 0; i < max_font_count; ++i) {
        state_ptr->bitmap_fonts[i].id = INVALID_ID_U16;
        state_ptr->bitmap_fonts[i].reference_count = 0;
    }
    for (u32 i = 0; i < max_font_count; ++i) {
        state_ptr->system_fonts[i].id = INVALID_ID_U16;
        state_ptr->system_fonts[i].reference_count = 0;
    }

    // Load up default bitmap font.
    if (!font_system_bitmap_font_load(&state_ptr->config.default_bitmap_font)) {
        KERROR("Failed to load bitmap font: %s", state_ptr->config.default_bitmap_font.name);
    }
    // System font.
    if (!font_system_system_font_load(&state_ptr->config.default_system_font)) {
        KERROR("Failed to load system font: %s", state_ptr->config.default_system_font.name);
    }

    return true;
}

void font_system_shutdown(void* memory) {
    if (memory) {
        // Cleanup bitmap fonts.
        for (u16 i = 0; i < max_font_count; ++i) {
            if (state_ptr->bitmap_fonts[i].id != INVALID_ID_U16) {
                font_data* data = &state_ptr->bitmap_fonts[i].font.resource_data->data;
                cleanup_font_data(data);
                state_ptr->bitmap_fonts[i].id = INVALID_ID_U16;
            }
        }

        // Cleanup system fonts.
        for (u16 i = 0; i < max_font_count; ++i) {
            if (state_ptr->system_fonts[i].id != INVALID_ID_U16) {
                // Cleanup each variant.
                u32 variant_count = darray_length(state_ptr->system_fonts[i].size_variants);
                for (u32 j = 0; j < variant_count; ++j) {
                    font_data* data = &state_ptr->system_fonts[i].size_variants[j];
                    cleanup_font_data(data);
                }
                state_ptr->bitmap_fonts[i].id = INVALID_ID_U16;

                darray_destroy(state_ptr->system_fonts[i].size_variants);
                state_ptr->system_fonts[i].size_variants = 0;
            }
        }
    }
}

b8 font_system_system_font_load(system_font_config* config) {
    // For system fonts, they can actually contain multiple fonts. For this reason,
    // a copy of the resource's data will be held in each resulting variant, and the
    // resource itself will be released.
    resource loaded_resource;
    if (!resource_system_load(config->resource_name, RESOURCE_TYPE_SYSTEM_FONT, 0, &loaded_resource)) {
        KERROR("Failed to load system font.");
        return false;
    }

    // Keep a casted pointer to the resource data for convenience.
    system_font_resource_data* resource_data = (system_font_resource_data*)loaded_resource.data;

    // Loop through the faces and create one lookup for each, as well as a default size
    // variant for each lookup.
    u32 font_face_count = darray_length(resource_data->fonts);
    for (u32 i = 0; i < font_face_count; ++i) {
        system_font_face* face = &resource_data->fonts[i];

        // Make sure a font with this name doesn't already exist.
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->system_font_lookup, face->name, &id)) {
            KERROR("Hashtable lookup failed. Font will not be loaded.");
            return false;
        }
        if (id != INVALID_ID_U16) {
            KWARN("A font named '%s' already exists and will not be loaded again.", config->name);
            // Not a hard error, return success since it already exists and can be used.
            return true;
        }

        // Get a new id
        for (u16 j = 0; j < max_font_count; ++j) {
            if (state_ptr->system_fonts[j].id == INVALID_ID_U16) {
                id = j;
                break;
            }
        }
        if (id == INVALID_ID_U16) {
            KERROR("No space left to allocate a new font. Increase maximum number allowed in font system config.");
            return false;
        }

        // Obtain the lookup.
        system_font_lookup* lookup = &state_ptr->system_fonts[id];
        lookup->binary_size = resource_data->binary_size;
        lookup->font_binary = resource_data->font_binary;
        lookup->face = string_duplicate(face->name);
        lookup->index = i;
        // To hold the size variants.
        lookup->size_variants = darray_create(font_data);

        // The offset
        lookup->offset = stbtt_GetFontOffsetForIndex(lookup->font_binary, i);
        i32 result = stbtt_InitFont(&lookup->info, lookup->font_binary, lookup->offset);
        if (result == 0) {
            // Zero indicates failure.
            KERROR("Failed to init system font %s at index %i.", loaded_resource.full_path, i);
            return false;
        }

        // Create a default size variant.
        font_data variant = {0};
        if (!create_system_font_variant(lookup, config->default_size, face->name, &variant)) {
            KERROR("Failed to create variant: %s, index %i", face->name, i);
            continue;
        }

        // Also perform setup for the variant
        if (!setup_font_data(&variant)) {
            KERROR("Failed to setup font data");
            continue;
        }

        // Add to the lookup's size variants.
        darray_push(lookup->size_variants, variant);

        // Set the entry id here last before updating the hashtable.
        lookup->id = id;
        if (!hashtable_set(&state_ptr->system_font_lookup, face->name, &id)) {
            KERROR("Hashtable set failed on font load.");
            return false;
        }
    }

    return true;
}

static void bitmap_font_texture_resource_loaded(kresource* resource, void* listener) {
    font_data* font = (font_data*)listener;

    // Setup the texture map.
    kresource_texture_map* map = &font->atlas;
    map->repeat_u = map->repeat_v = map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    map->filter_minify = map->filter_magnify = TEXTURE_FILTER_MODE_NEAREST;
    map->texture = (kresource_texture*)resource;
    if (!renderer_kresource_texture_map_resources_acquire(engine_systems_get()->renderer_system, map)) {
        KERROR("Unable to acquire texture map resources. Bitmap font cannot be initialized.");
    }
}

b8 font_system_bitmap_font_load(bitmap_font_config* config) {
    // Make sure a font with this name doesn't already exist.
    u16 id = INVALID_ID_U16;
    if (!hashtable_get(&state_ptr->bitmap_font_lookup, config->name, &id)) {
        KERROR("Hashtable lookup failed. Font will not be loaded.");
        return false;
    }
    if (id != INVALID_ID_U16) {
        KWARN("A font named '%s' already exists and will not be loaded again.", config->name);
        // Not a hard error, return success since it already exists and can be used.
        return true;
    }

    // Get a new id
    for (u16 i = 0; i < max_font_count; ++i) {
        if (state_ptr->bitmap_fonts[i].id == INVALID_ID_U16) {
            id = i;
            break;
        }
    }
    if (id == INVALID_ID_U16) {
        KERROR("No space left to allocate a new bitmap font. Increase maximum number allowed in font system config.");
        return false;
    }

    // Obtain the lookup.
    bitmap_font_lookup* lookup = &state_ptr->bitmap_fonts[id];

    if (!resource_system_load(config->resource_name, RESOURCE_TYPE_BITMAP_FONT, 0, &lookup->font.loaded_resource)) {
        KERROR("Failed to load bitmap font.");
        return false;
    }

    // Keep a casted pointer to the resource data for convenience.
    lookup->font.resource_data = (bitmap_font_resource_data*)lookup->font.loaded_resource.data;

    // Acquire the texture.
    /* lookup->font.resource_data->data.atlas.texture = texture_system_acquire(lookup->font.resource_data->pages[0].file, true); */

    // Font atlas texture.
    // TODO: only accounts for one page at the moment.
    b8 request_result = texture_system_request(
        // NOTE: Might have to address this by using the new font resource type.
        kname_create(lookup->font.resource_data->pages[0].file),
        kname_create("PluginUiStandard"), // TODO: configurable
        &lookup->font.resource_data->data,
        bitmap_font_texture_resource_loaded,
        &lookup->font.resource_data->data.atlas_texture);
    if (!request_result) {
        KERROR("Failed to request bitmap font texture.");
        // TODO: use default texture instead.
        return false;
    }
    font_data* data = &lookup->font.resource_data->data;

    // Create map resources
    data->atlas.filter_magnify = data->atlas.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    data->atlas.repeat_u = data->atlas.repeat_v = data->atlas.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    if (!renderer_kresource_texture_map_resources_acquire(engine_systems_get()->renderer_system, &data->atlas)) {
        KERROR("Unable to acquire resources for font atlas texture map.");
        return false;
    }
    data->atlas.texture = &data->atlas_texture;

    b8 result = setup_font_data(&lookup->font.resource_data->data);

    // Set the entry id here last before updating the hashtable.
    if (!hashtable_set(&state_ptr->bitmap_font_lookup, config->name, &id)) {
        KERROR("Hashtable set failed on font load.");
        return false;
    }

    lookup->id = id;

    return result;
}

font_data* font_system_acquire(const char* font_name, u16 font_size, font_type type) {
    if (type == FONT_TYPE_BITMAP) {
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->bitmap_font_lookup, font_name, &id)) {
            KERROR("Bitmap font lookup failed on acquire.");
            return false;
        }

        if (id == INVALID_ID_U16) {
            KERROR("A bitmap font named '%s' was not found. Font acquisition failed.", font_name);
            return false;
        }

        // Get the lookup.
        bitmap_font_lookup* lookup = &state_ptr->bitmap_fonts[id];

        // Increment the reference.
        lookup->reference_count++;

        return &lookup->font.resource_data->data;
    } else if (type == FONT_TYPE_SYSTEM) {
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->system_font_lookup, font_name, &id)) {
            KERROR("System font lookup failed on acquire.");
            return false;
        }

        if (id == INVALID_ID_U16) {
            KERROR("A system font named '%s' was not found. Font acquisition failed.", font_name);
            return false;
        }

        // Get the lookup.
        system_font_lookup* lookup = &state_ptr->system_fonts[id];

        // Search the size variants for the correct size.
        u32 count = darray_length(lookup->size_variants);
        for (u32 i = 0; i < count; ++i) {
            if (lookup->size_variants[i].size == font_size) {
                // Increment the reference.
                lookup->reference_count++;
                return &lookup->size_variants[i];
            }
        }

        // If we reach this point, the size variant doesn't exist. Create it.
        font_data variant;
        if (!create_system_font_variant(lookup, font_size, font_name, &variant)) {
            KERROR("Failed to create variant: %s, index %i, size %i", lookup->face, lookup->index, font_size);
            return false;
        }

        // Also perform setup for the variant
        if (!setup_font_data(&variant)) {
            KERROR("Failed to setup font data");
        }

        // Add to the lookup's size variants.
        darray_push(lookup->size_variants, variant);
        u32 length = darray_length(lookup->size_variants);

        // Increment the reference.
        lookup->reference_count++;
        return &lookup->size_variants[length - 1];
    }

    KERROR("Unrecognized font type: %d", type);
    return 0;
}

b8 font_system_release(const char* font_name) {
    // TODO: Lookup font by name in appropriate hashtable.
    return true;
}

b8 font_system_verify_atlas(font_data* font, const char* text) {
    if (font->type == FONT_TYPE_BITMAP) {
        // Bitmaps don't need verification since they are already generated.
        return true;
    } else if (font->type == FONT_TYPE_SYSTEM) {
        u16 id = INVALID_ID_U16;
        if (!hashtable_get(&state_ptr->system_font_lookup, font->face, &id)) {
            KERROR("System font lookup failed on acquire.");
            return false;
        }

        if (id == INVALID_ID_U16) {
            KERROR("A system font named '%s' was not found. Font atlas verification failed.", font->face);
            return false;
        }

        // Get the lookup.
        system_font_lookup* lookup = &state_ptr->system_fonts[id];

        return verify_system_font_size_variant(lookup, font, text);
    }

    KERROR("font_system_verify_atlas failed: Unknown font type.");
    return false;
}

vec2 font_system_measure_string(font_data* font, const char* text) {
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

static b8 setup_font_data(font_data* font) {
    /* // Create map resources
    font->atlas.filter_magnify = font->atlas.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    font->atlas.repeat_u = font->atlas.repeat_v = font->atlas.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    if (!renderer_texture_map_resources_acquire(&font->atlas)) {
        KERROR("Unable to acquire resources for font atlas texture map.");
        return false;
    } */

    // Check for a tab glyph, as there may not always be one exported. If there is, store its
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

    return true;
}

static void cleanup_font_data(font_data* font) {
    // Release the texture map resources.
    renderer_kresource_texture_map_resources_release(engine_systems_get()->renderer_system, &font->atlas);

    // If a bitmap font, release the reference to the texture.
    if (font->type == FONT_TYPE_BITMAP && font->atlas.texture) {
        texture_system_release_resource(font->atlas.texture);
    }
    font->atlas.texture = 0;
}

static b8 create_system_font_variant(system_font_lookup* lookup, u16 size, const char* font_name, font_data* out_variant) {
    kzero_memory(out_variant, sizeof(font_data));
    out_variant->atlas_size_x = 1024; // TODO: configurable size
    out_variant->atlas_size_y = 1024;
    out_variant->size = size;
    out_variant->type = FONT_TYPE_SYSTEM;
    string_ncopy(out_variant->face, font_name, 255);
    out_variant->internal_data_size = sizeof(system_font_variant_data);
    out_variant->internal_data = kallocate(out_variant->internal_data_size, MEMORY_TAG_SYSTEM_FONT);

    system_font_variant_data* internal_data = (system_font_variant_data*)out_variant->internal_data;

    // Push default codepoints (ascii 32-127) always, plus a -1 for unknown.
    internal_data->codepoints = darray_reserve(i32, 96);
    darray_push(internal_data->codepoints, -1); // push invalid char
    for (i32 i = 0; i < 95; ++i) {
        internal_data->codepoints[i + 1] = i + 32;
    }
    darray_length_set(internal_data->codepoints, 96);

    // Create texture.
    const char* font_tex_name = string_format("__system_text_atlas_%s_i%i_sz%i__", font_name, lookup->index, size);

    b8 request_result = texture_system_request_writeable(
        kname_create(font_tex_name),
        out_variant->atlas_size_x,
        out_variant->atlas_size_y, 
        KRESOURCE_TEXTURE_FORMAT_RGB8, 
        true, 
        &out_variant->atlas_texture);
    string_free(font_tex_name);
    font_tex_name = 0;

    // Create map resources
    out_variant->atlas.filter_magnify = out_variant->atlas.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    out_variant->atlas.repeat_u = out_variant->atlas.repeat_v = out_variant->atlas.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    out_variant->atlas.texture = &out_variant->atlas_texture;
    if (!renderer_kresource_texture_map_resources_acquire(engine_systems_get()->renderer_system, &out_variant->atlas)) {
        KERROR("Unable to acquire resources for font atlas texture map.");
        return false;
    }

    if (request_result) {
        // Obtain some metrics
        internal_data->scale = stbtt_ScaleForPixelHeight(&lookup->info, (f32)size);
        i32 ascent, descent, line_gap;
        stbtt_GetFontVMetrics(&lookup->info, &ascent, &descent, &line_gap);
        out_variant->line_height = (ascent - descent + line_gap) * internal_data->scale;

        return rebuild_system_font_variant_atlas(lookup, out_variant);
    }

    KERROR("Request for writeable font texture atlas resource failed. See logs for details.");
    return request_result;
}

static b8 rebuild_system_font_variant_atlas(system_font_lookup* lookup, font_data* variant) {
    system_font_variant_data* internal_data = (system_font_variant_data*)variant->internal_data;

    u32 pack_image_size = variant->atlas_size_x * variant->atlas_size_y * sizeof(u8);
    u8* pixels = kallocate(pack_image_size, MEMORY_TAG_ARRAY);
    u32 codepoint_count = darray_length(internal_data->codepoints);
    stbtt_packedchar* packed_chars = kallocate(sizeof(stbtt_packedchar) * codepoint_count, MEMORY_TAG_ARRAY);

    // Begin packing all known characters into the atlas. This
    // creates a single-channel image with rendered glyphs at the
    // given size.
    stbtt_pack_context context;
    if (!stbtt_PackBegin(&context, pixels, variant->atlas_size_x, variant->atlas_size_y, 0, 1, 0)) {
        KERROR("stbtt_PackBegin failed");
        return false;
    }

    // Fit all codepoints into a single range for packing.
    stbtt_pack_range range;
    range.first_unicode_codepoint_in_range = 0;
    range.font_size = variant->size;
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
            variant->atlas_texture.renderer_texture_handle,
            0, pack_image_size * 4, rgba_pixels)) {
        KERROR("Failed to write data to system font variant texture");
        return false;
    }

    // Free pixel/rgba_pixel data.
    kfree(pixels, pack_image_size, MEMORY_TAG_ARRAY);
    kfree(rgba_pixels, pack_image_size * 4, MEMORY_TAG_ARRAY);

    // Regenerate glyphs
    if (variant->glyphs && variant->glyph_count) {
        kfree(variant->glyphs, sizeof(font_glyph) * variant->glyph_count, MEMORY_TAG_ARRAY);
    }
    variant->glyph_count = codepoint_count;
    variant->glyphs = kallocate(sizeof(font_glyph) * codepoint_count, MEMORY_TAG_ARRAY);
    for (u16 i = 0; i < variant->glyph_count; ++i) {
        stbtt_packedchar* pc = &packed_chars[i];
        font_glyph* g = &variant->glyphs[i];
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
    if (variant->kernings && variant->kerning_count) {
        kfree(variant->kernings, sizeof(font_kerning) * variant->kerning_count, MEMORY_TAG_ARRAY);
    }
    variant->kerning_count = stbtt_GetKerningTableLength(&lookup->info);
    if (variant->kerning_count) {
        variant->kernings = kallocate(sizeof(font_kerning) * variant->kerning_count, MEMORY_TAG_ARRAY);
        // Get the kerning table for the current font.
        stbtt_kerningentry* kerning_table = kallocate(sizeof(stbtt_kerningentry) * variant->kerning_count, MEMORY_TAG_ARRAY);
        u32 entry_count = stbtt_GetKerningTable(&lookup->info, kerning_table, variant->kerning_count);
        if (entry_count != variant->kerning_count) {
            KERROR("Kerning entry count mismatch: %u->%u", entry_count, variant->kerning_count);
            return false;
        }

        for (u32 i = 0; i < variant->kerning_count; ++i) {
            font_kerning* k = &variant->kernings[i];
            k->codepoint_0 = kerning_table[i].glyph1;
            k->codepoint_1 = kerning_table[i].glyph2;
            k->amount = kerning_table[i].advance;
        }

        kfree(kerning_table, sizeof(stbtt_kerningentry) * variant->kerning_count, MEMORY_TAG_ARRAY);
    } else {
        variant->kernings = 0;
    }

    return true;
}

static b8 verify_system_font_size_variant(system_font_lookup* lookup, font_data* variant, const char* text) {
    system_font_variant_data* internal_data = (system_font_variant_data*)variant->internal_data;

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
