#include "font_system.h"

#include "core/kmemory.h"
#include "core/logger.h"
#include "core/kstring.h"
#include "containers/darray.h"
#include "containers/hashtable.h"
#include "resources/resource_types.h"
#include "resources/ui_text.h"
#include "renderer/renderer_frontend.h"
#include "systems/texture_system.h"
#include "systems/resource_system.h"

typedef struct bitmap_font_internal_data {
    resource loaded_resource;
    // Casted pointer to resource data for convenience.
    bitmap_font_resource_data* resource_data;
} bitmap_font_internal_data;

typedef struct bitmap_font_lookup {
    u16 id;
    u16 reference_count;
    bitmap_font_internal_data font;
} bitmap_font_lookup;

typedef struct font_system_state {
    font_system_config config;
    hashtable bitmap_font_lookup;
    bitmap_font_lookup* bitmap_fonts;
    void* bitmap_hashtable_block;
} font_system_state;

b8 setup_font_data(font_data* font);
void cleanup_font_data(font_data* font);

static font_system_state* state_ptr;

b8 font_system_initialize(u64* memory_requirement, void* memory, font_system_config* config) {
    if (config->max_bitmap_font_count == 0 || config->max_system_font_count == 0) {
        KFATAL("font_system_initialize - config.max_bitmap_font_count and config.max_system_font_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then blocks for arrays, then blocks for hashtables.
    u64 struct_requirement = sizeof(font_system_state);
    u64 bmp_array_requirement = sizeof(bitmap_font_lookup) * config->max_bitmap_font_count;
    u64 bmp_hashtable_requirement = sizeof(u16) * config->max_bitmap_font_count;
    *memory_requirement = struct_requirement + bmp_array_requirement + bmp_hashtable_requirement;

    if (!memory) {
        return true;
    }

    state_ptr = (font_system_state*)memory;
    state_ptr->config = *config;

    // The array blocks are after the state. Already allocated, so just set the pointer.
    void* bmp_array_block = (void*)(((u8*)memory) + struct_requirement);

    state_ptr->bitmap_fonts = bmp_array_block;

    // Hashtable blocks are after arrays.
    void* bmp_hashtable_block = (void*)(((u8*)bmp_array_block));

    // Create hashtables for font lookups.
    hashtable_create(sizeof(u16), state_ptr->config.max_bitmap_font_count, bmp_hashtable_block, false, &state_ptr->bitmap_font_lookup);

    // Fill both hashtables with invalid references to use as a default.
    u16 invalid_id = INVALID_ID_U16;
    hashtable_fill(&state_ptr->bitmap_font_lookup, &invalid_id);

    // Invalidate all entries in both arrays.
    u32 count = state_ptr->config.max_bitmap_font_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->bitmap_fonts[i].id = INVALID_ID_U16;
        state_ptr->bitmap_fonts[i].reference_count = 0;
    }

    // Load up any default fonts.
    // Bitmap fonts.
    for (u32 i = 0; i < state_ptr->config.default_bitmap_font_count; ++i) {
        if (!font_system_load_bitmap_font(&state_ptr->config.bitmap_font_configs[i])) {
            KERROR("Failed to load bitmap font: %s", state_ptr->config.bitmap_font_configs[i].name);
        }
    }

    return true;
}

void font_system_shutdown(void* memory) {
    if (memory) {
        // Cleanup bitmap fonts.
        for (u16 i = 0; i < state_ptr->config.max_bitmap_font_count; ++i) {
            if (state_ptr->bitmap_fonts[i].id != INVALID_ID_U16) {
                font_data* data = &state_ptr->bitmap_fonts[i].font.resource_data->data;
                cleanup_font_data(data);
                state_ptr->bitmap_fonts[i].id = INVALID_ID_U16;
            }
        }
    }
}

b8 font_system_load_system_font(system_font_config* config) {
    KERROR("System fonts not yet supported.");
    return false;
}

b8 font_system_load_bitmap_font(bitmap_font_config* config) {
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
    for (u16 i = 0; i < state_ptr->config.max_bitmap_font_count; ++i) {
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
    // TODO: only accounts for one page at the moment.
    lookup->font.resource_data->data.atlas.texture = texture_system_acquire(lookup->font.resource_data->pages[0].file, true);

    b8 result = setup_font_data(&lookup->font.resource_data->data);

    // Set the entry id here last before updating the hashtable.
    if (!hashtable_set(&state_ptr->bitmap_font_lookup, config->name, &id)) {
        KERROR("Hashtable set failed on font load.");
        return false;
    }

    lookup->id = id;

    return result;
}

b8 font_system_acquire(const char* font_name, u16 font_size, struct ui_text* text) {
    if (text->type == UI_TEXT_TYPE_BITMAP) {
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

        // Assign the data, increment the reference.
        text->data = &lookup->font.resource_data->data;
        lookup->reference_count++;

        return true;
    } else if (text->type == UI_TEXT_TYPE_SYSTEM) {
        KERROR("System fonts not yet supported");
        return false;
    }

    KERROR("Unrecognized font type: %d", text->type);
    return false;
}

b8 font_system_release(struct ui_text* text) {
    // TODO: Lookup font by name in appropriate hashtable.
    return true;
}

b8 font_system_verify_atlas(font_data* font, const char* text) {
    if (font->type == FONT_TYPE_BITMAP) {
        // Bitmaps don't need verification since they are already generated.
        return true;
    }

    KERROR("font_system_verify_atlas failed: Unknown font type.");
    return false;
}

b8 setup_font_data(font_data* font) {
    // Create map resources
    font->atlas.filter_magnify = font->atlas.filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    font->atlas.repeat_u = font->atlas.repeat_v = font->atlas.repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    font->atlas.use = TEXTURE_USE_MAP_DIFFUSE;
    if (!renderer_texture_map_acquire_resources(&font->atlas)) {
        KERROR("Unable to acquire resources for font atlas texture map.");
        return false;
    }

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

void cleanup_font_data(font_data* font) {
    // Release the texture map resources.
    renderer_texture_map_release_resources(&font->atlas);

    // If a bitmap font, release the reference to the texture.
    if (font->type == FONT_TYPE_BITMAP && font->atlas.texture) {
        texture_system_release(font->atlas.texture->name);
    }
    font->atlas.texture = 0;
}
