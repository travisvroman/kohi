#include "texture_system.h"

#include "assets/kasset_types.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "runtime_defines.h"
#include "strings/kname.h"
#include "strings/kstring.h"
#include "systems/kresource_system.h"

typedef struct texture_system_state {
    texture_system_config config;

    kresource_texture* default_kresource_texture;
    kresource_texture* default_kresource_base_colour_texture;
    kresource_texture* default_kresource_specular_texture;
    kresource_texture* default_kresource_normal_texture;
    kresource_texture* default_kresource_mra_texture;
    kresource_texture* default_kresource_cube_texture;
    kresource_texture* default_kresource_water_normal_texture;
    kresource_texture* default_kresource_water_dudv_texture;

    // A convenience pointer to the renderer system state.
    struct renderer_system_state* renderer;

    struct kresource_system_state* kresource_system;
} texture_system_state;

// FIXME: remove this and its dependencies.
static texture_system_state* state_ptr = 0;

static b8 create_default_textures(texture_system_state* state);
static void release_default_textures(texture_system_state* state);
static void increment_generation(kresource_texture* t);
static void invalidate_texture(kresource_texture* t);
static b8 is_default_texture(texture_system_state* state, kresource_texture* t);

static kresource_texture* default_texture_by_name(texture_system_state* state, kname name);
static kresource_texture* request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, texture_type type, u16 array_size, b8 is_depth, b8 is_stencil, b8 multiframe_buffering);

b8 texture_system_initialize(u64* memory_requirement, void* state, void* config) {
    texture_system_config* typed_config = (texture_system_config*)config;
    if (typed_config->max_texture_count == 0) {
        KFATAL("texture_system_initialize - config.max_texture_count must be > 0.");
        return false;
    }

    *memory_requirement = sizeof(texture_system_state);

    if (!state) {
        return true;
    }

    KDEBUG("Initializing texture system...");

    state_ptr = state;
    state_ptr->config = *typed_config;

    // Keep a pointer to the renderer system state.
    state_ptr->renderer = engine_systems_get()->renderer_system;
    state_ptr->kresource_system = engine_systems_get()->kresource_state;

    // Create default textures for use in the system.
    create_default_textures(state_ptr);

    KDEBUG("Texture system initialization complete.");

    return true;
}

void texture_system_shutdown(void* state) {
    if (state_ptr) {
        release_default_textures(state_ptr);

        state_ptr->renderer = 0;
        state_ptr = 0;
    }
}

kresource_texture* texture_system_request(kname name, kname package_name, void* listener, PFN_resource_loaded_user_callback callback) {
    texture_system_state* state = engine_systems_get()->texture_system;

    // Check that name is not the name of a default texture. If it is, immediately
    // make the callback with the appropriate default texture and return it
    kresource_texture* t = default_texture_by_name(state, name);
    if (t) {
        if (callback) {
            callback((kresource*)t, listener);
        }
        return t;
    }
    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(1);
    request.base.assets.data[0].type = KASSET_TYPE_IMAGE;
    request.base.assets.data[0].package_name = package_name;
    request.base.assets.data[0].asset_name = name;

    request.array_size = 1;
    request.texture_type = TEXTURE_TYPE_2D;
    request.flags = 0;
    request.flip_y = true;

    t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for texture '%s'.", kname_string_get(name));
    }

    return t;
}

kresource_texture* texture_system_request_cube(kname name, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback) {
    texture_system_state* state = engine_systems_get()->texture_system;

    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }

    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }

    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(6);

    // +X,-X,+Y,-Y,+Z,-Z in _cubemap_ space, which is LH y-down
    // Build out image side asset names. Order is important here.
    // name_r Right
    // name_l Left
    // name_u Up
    // name_d Down
    // name_f Front
    // name_b Back
    const char* sides = "rludfb";
    const char* base_name = kname_string_get(name);
    for (u8 i = 0; i < 6; ++i) {
        const char* buf = string_format("%s_%c", base_name, sides[i]);
        kname side_name = kname_create(buf);
        string_free(buf);

        request.base.assets.data[i].type = KASSET_TYPE_IMAGE;
        request.base.assets.data[i].package_name = INVALID_KNAME; // TODO: automatic package name?
        request.base.assets.data[i].asset_name = side_name;
    }

    request.array_size = 6;
    request.texture_type = TEXTURE_TYPE_CUBE;
    request.flags = multiframe_buffering ? TEXTURE_FLAG_RENDERER_BUFFERING : 0;
    request.flip_y = false;

    kresource_texture* t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for cube texture '%s'.", kname_string_get(name));
    }

    return t;
}

kresource_texture* texture_system_request_cube_writeable(kname name, u32 dimension, b8 auto_release, b8 multiframe_buffering) {
    texture_system_state* state = engine_systems_get()->texture_system;
    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }
    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }
    return request_writeable_arrayed(name, dimension, dimension, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_CUBE, 6, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_cube_depth(kname name, u32 dimension, b8 auto_release, b8 include_stencil, b8 multiframe_buffering) {
    texture_system_state* state = engine_systems_get()->texture_system;
    // If requesting the default cube texture name, just return it.
    if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    }
    if (name == INVALID_KNAME) {
        KWARN("texture_system_request_cube - name supplied is invalid. Returning default cubemap instead.");
        return state->default_kresource_cube_texture;
    }
    return request_writeable_arrayed(name, dimension, dimension, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_CUBE, 6, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_request_writeable(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, format, has_transparency, TEXTURE_TYPE_2D, 1, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, b8 multiframe_buffering, texture_type type, u16 array_size) {
    return request_writeable_arrayed(name, width, height, format, has_transparency, type, array_size, false, false, multiframe_buffering);
}

kresource_texture* texture_system_request_depth(kname name, u32 width, u32 height, b8 include_stencil, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_2D, 1, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_request_depth_arrayed(kname name, u32 width, u32 height, u16 array_size, b8 include_stencil, b8 multiframe_buffering) {
    return request_writeable_arrayed(name, width, height, TEXTURE_FORMAT_RGBA8, false, TEXTURE_TYPE_2D_ARRAY, array_size, true, include_stencil, multiframe_buffering);
}

kresource_texture* texture_system_acquire_textures_as_arrayed(kname name, kname package_name, u32 layer_count, kname* layer_asset_names, b8 auto_release, b8 multiframe_buffering, void* listener, PFN_resource_loaded_user_callback callback) {
    if (layer_count < 1) {
        KERROR("Must contain at least one layer.");
        return 0;
    }

    texture_system_state* state = engine_systems_get()->texture_system;

    // Check that name is not the name of a default texture. If it is, immediately
    // make the callback with the appropriate default texture and return it
    kresource_texture* t = default_texture_by_name(state, name);
    if (t) {
        if (callback) {
            callback((kresource*)t, listener);
        }
        return t;
    }
    // If not default, request the resource from the resource system.
    kresource_texture_request_info request = {0};
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.base.listener_inst = listener;
    request.base.user_callback = callback;

    request.base.assets = array_kresource_asset_info_create(layer_count);
    for (u32 i = 0; i < layer_count; ++i) {
        kresource_asset_info* asset = &request.base.assets.data[i];
        asset->type = KASSET_TYPE_IMAGE;
        asset->package_name = package_name;
        asset->asset_name = layer_asset_names[i];
    }

    request.array_size = layer_count;
    request.texture_type = TEXTURE_TYPE_2D_ARRAY;
    request.flags = 0;
    request.flip_y = true;

    t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to properly request resource for arrayed texture '%s'.", kname_string_get(name));
    }

    return t;
}

void texture_system_release_resource(kresource_texture* t) {
    struct kresource_system_state* resource_system = engine_systems_get()->kresource_state;
    texture_system_state* state = engine_systems_get()->texture_system;

    // Do nothing if this is a default texture.
    if (is_default_texture(state, t)) {
        return;
    }

    kresource_system_release(resource_system, t->base.name);
}

b8 texture_system_resize(kresource_texture* t, u32 width, u32 height, b8 regenerate_internal_data) {
    if (t) {
        if (!(t->flags & TEXTURE_FLAG_IS_WRITEABLE)) {
            KWARN("texture_system_resize should not be called on textures that are not writeable.");
            return false;
        }
        t->width = width;
        t->height = height;
        // FIXME: remove this requirement, and potentially the regenerate_internal_data flag as well.
        // Only allow this for writeable textures that are not wrapped.
        // Wrapped textures can call texture_system_set_internal then call
        // this function to get the above parameter updates and a generation
        // update.
        if (!(t->flags & TEXTURE_FLAG_IS_WRAPPED) && regenerate_internal_data) {
            // Regenerate internals for the new size.
            b8 result = renderer_texture_resize(state_ptr->renderer, t->renderer_texture_handle, width, height);
            increment_generation(t);
            return result;
        }
        return true;
    }
    return false;
}

b8 texture_system_write_data(kresource_texture* t, u32 offset, u32 size, void* data) {
    if (t) {
        return renderer_texture_write_data(state_ptr->renderer, t->renderer_texture_handle, offset, size, data);
    }
    return false;
}

#define RETURN_TEXT_PTR_OR_NULL(texture, func_name)                                              \
    if (state_ptr) {                                                                             \
        return &texture;                                                                         \
    }                                                                                            \
    KERROR("%s called before texture system initialization! Null pointer returned.", func_name); \
    return 0;

static b8 is_default_texture(texture_system_state* state, kresource_texture* t) {
    if (!state_ptr) {
        return false;
    }
    return (t == state->default_kresource_texture) ||
           (t == state->default_kresource_base_colour_texture) ||
           (t == state->default_kresource_specular_texture) ||
           (t == state->default_kresource_normal_texture) ||
           (t == state->default_kresource_mra_texture) ||
           (t == state->default_kresource_cube_texture) ||
           (t == state->default_kresource_water_normal_texture) ||
           (t == state->default_kresource_water_dudv_texture);
}

kresource_texture* create_default_kresource_texture(texture_system_state* state, kname name, texture_type type, u32 tex_dimension, u8 layer_count, u8 channel_count, u32 pixel_array_size, u8* pixels) {
    kresource_texture_request_info request = {0};
    kzero_memory(&request, sizeof(kresource_texture_request_info));
    request.texture_type = type;
    request.array_size = layer_count;
    request.flags = TEXTURE_FLAG_IS_WRITEABLE;
    request.pixel_data = array_kresource_texture_pixel_data_create(1);
    kresource_texture_pixel_data* px = &request.pixel_data.data[0];
    px->pixel_array_size = pixel_array_size;
    px->pixels = pixels;
    px->width = tex_dimension;
    px->height = tex_dimension;
    px->channel_count = channel_count;
    px->format = TEXTURE_FORMAT_RGBA8;
    px->mip_levels = 1;
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.flip_y = false; // Doesn't really matter since there's no asset being loaded.
    kresource_texture* t = (kresource_texture*)kresource_system_request(state->kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to request resources for default texture");
    }
    return t;
}

static b8 create_default_textures(texture_system_state* state) {
    // NOTE: Create default texture, a 256x256 blue/white checkerboard pattern.
    // This is done in code to eliminate asset dependencies.
    KTRACE("Creating default texture...");
    const u32 tex_dimension = 16;
    const u32 channels = 4;
    const u32 pixel_count = tex_dimension * tex_dimension;
    u8 pixels[16 * 16 * 4];
    kset_memory(pixels, 255, sizeof(u8) * pixel_count * channels);

    // Each pixel.
    for (u64 row = 0; row < tex_dimension; ++row) {
        for (u64 col = 0; col < tex_dimension; ++col) {
            u64 index = (row * tex_dimension) + col;
            u64 index_bpp = index * channels;
            if (row % 2) {
                if (col % 2) {
                    pixels[index_bpp + 0] = 0;
                    pixels[index_bpp + 1] = 0;
                }
            } else {
                if (!(col % 2)) {
                    pixels[index_bpp + 0] = 0;
                    pixels[index_bpp + 1] = 0;
                }
            }
        }
    }

    // Default texture
    {
        KTRACE("Creating default resource texture...");

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_texture = create_default_kresource_texture(state, kname_create(DEFAULT_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, pixels);
        if (!state->default_kresource_texture) {
            KERROR("Failed to request resources for default texture");
            return false;
        }
    }

    // Base colour texture.
    {
        KTRACE("Creating default base colour texture...");

        u8 diff_pixels[16 * 16 * 4];
        // Default diffuse map is all white.
        kset_memory(diff_pixels, 255, sizeof(u8) * 16 * 16 * 4);
        // Request new resource texture.

        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_base_colour_texture = create_default_kresource_texture(state, kname_create(DEFAULT_BASE_COLOUR_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, diff_pixels);
        if (!state->default_kresource_base_colour_texture) {
            KERROR("Failed to request resources for default base colour texture");
            return false;
        }
    }

    // Specular texture.
    {
        KTRACE("Creating default specular texture...");
        u8 spec_pixels[16 * 16 * 4];
        // Default spec map is black (no specular)
        kset_memory(spec_pixels, 0, sizeof(u8) * 16 * 16 * 4);

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_specular_texture = create_default_kresource_texture(state, kname_create(DEFAULT_SPECULAR_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, spec_pixels);
        if (!state->default_kresource_specular_texture) {
            KERROR("Failed to request resources for default specular texture");
            return false;
        }
    }

    // Normal texture.
    u8 normal_pixels[16 * 16 * 4]; // w * h * channels
    {
        KTRACE("Creating default normal texture...");
        kset_memory(normal_pixels, 255, sizeof(u8) * 16 * 16 * 4);

        // Each pixel.
        for (u64 row = 0; row < 16; ++row) {
            for (u64 col = 0; col < 16; ++col) {
                u64 index = (row * 16) + col;
                u64 index_bpp = index * channels;
                // Set blue, z-axis by default and alpha.
                normal_pixels[index_bpp + 2] = 128;
                normal_pixels[index_bpp + 3] = 128;
            }
        }

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_normal_texture = create_default_kresource_texture(state, kname_create(DEFAULT_NORMAL_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, normal_pixels);
        if (!state->default_kresource_normal_texture) {
            KERROR("Failed to request resources for default normal texture");
            return false;
        }
    }

    // MRA texture
    u8 mra_pixels[16 * 16 * 4]; // w * h * channels
    {
        KTRACE("Creating default MRA (metallic, roughness, AO) texture...");
        kset_memory(mra_pixels, 255, sizeof(u8) * 16 * 16 * 4);

        // Each pixel.
        for (u64 row = 0; row < 16; ++row) {
            for (u64 col = 0; col < 16; ++col) {
                u64 index = (row * 16) + col;
                u64 index_bpp = index * channels;
                mra_pixels[index_bpp + 0] = 0;   // Default for metallic is black.
                mra_pixels[index_bpp + 1] = 128; // Default for roughness is medium grey
                mra_pixels[index_bpp + 2] = 255; // Default for AO is white.
            }
        }

        // Request new resource texture.
        u32 pixel_array_size = sizeof(u8) * pixel_count * channels;
        state->default_kresource_mra_texture = create_default_kresource_texture(state, kname_create(DEFAULT_MRA_TEXTURE_NAME), TEXTURE_TYPE_2D, tex_dimension, 1, channels, pixel_array_size, mra_pixels);
        if (!state->default_kresource_mra_texture) {
            KERROR("Failed to request resources for default MRA texture");
            return false;
        }
    }

    // Cube texture.
    {
        KTRACE("Creating default cube texture...");

        // New resource type
        const u32 tex_dimension = 16;
        const u32 channels = 4;
        const u32 layers = 6; // one per side.
        const u32 pixel_count = tex_dimension * tex_dimension;
        u8 cube_side_pixels[16 * 16 * 4];
        kset_memory(cube_side_pixels, 255, sizeof(u8) * pixel_count * channels);

        // Each pixel.
        for (u64 row = 0; row < tex_dimension; ++row) {
            for (u64 col = 0; col < tex_dimension; ++col) {
                u64 index = (row * tex_dimension) + col;
                u64 index_bpp = index * channels;
                if (row % 2) {
                    if (col % 2) {
                        cube_side_pixels[index_bpp + 1] = 0;
                        cube_side_pixels[index_bpp + 2] = 0;
                    }
                } else {
                    if (!(col % 2)) {
                        cube_side_pixels[index_bpp + 1] = 0;
                        cube_side_pixels[index_bpp + 2] = 0;
                    }
                }
            }
        }

        // Copy the image side data (same on all sides) to the relevant portion of the pixel array.
        u64 layer_size = sizeof(u8) * tex_dimension * tex_dimension * channels;
        u64 image_size = layer_size * layers;
        u8* pixels = kallocate(image_size, MEMORY_TAG_ARRAY);
        for (u8 i = 0; i < layers; ++i) {
            kcopy_memory(pixels + layer_size * i, cube_side_pixels, layer_size);
        }

        // Request new resource texture.?
        u32 pixel_array_size = image_size;
        state->default_kresource_cube_texture = create_default_kresource_texture(state, kname_create(DEFAULT_CUBE_TEXTURE_NAME), TEXTURE_TYPE_CUBE, tex_dimension, 6, channels, pixel_array_size, pixels);
        if (!state->default_kresource_cube_texture) {
            KERROR("Failed to request resources for default cube texture");
            return false;
        }
        kfree(pixels, image_size, MEMORY_TAG_ARRAY);
    }

    // FIXME: Should there even _be_ a default layered texture, or should they just
    // be created on the fly as needed?
    //
    // Default layered textures. 4 materials, 3 maps per, for 12 layers.
    // TODO: This should be a default layered texture of n layers, if anything.
    //
    /* {
        u32 layer_size = sizeof(u8) * 16 * 16 * 4;
        u32 terrain_material_count = 4;
        u32 terrain_per_material_map_count = 3;
        u32 layer_count = terrain_per_material_map_count * terrain_material_count;
        u8* terrain_pixels = kallocate(layer_size * layer_count, MEMORY_TAG_ARRAY);
        u32 material_size = layer_size * terrain_per_material_map_count;
        for (u32 i = 0; i < terrain_material_count; ++i) {
            // Albedo NOTE: purposefully using checkerboard here instead of default diffuse white;
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 0), pixels, layer_size);
            // Normal
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 1), normal_pixels, layer_size);
            // Combined
            kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 2), mra_pixels, layer_size);
        }

        // Request new resource texture.
        state->default_kresource_terrain_texture = create_default_kresource_texture(state, kname_create(DEFAULT_TERRAIN_TEXTURE_NAME), texture_TYPE_2D_ARRAY, tex_dimension, layer_count, channels, layer_size * layer_count, terrain_pixels);
        if (!state->default_kresource_terrain_texture) {
            KERROR("Failed to request resources for default terrain texture");
            return false;
        }
        kfree(terrain_pixels, layer_size * layer_count, MEMORY_TAG_ARRAY);
    } */

    // Default water normal texture is part of the runtime package - request it.
    state->default_kresource_water_normal_texture = texture_system_request(kname_create(DEFAULT_WATER_NORMAL_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    // Default water dudv texture is part of the runtime package - request it.
    state->default_kresource_water_dudv_texture = texture_system_request(kname_create(DEFAULT_WATER_DUDV_TEXTURE_NAME), kname_create(PACKAGE_NAME_RUNTIME), 0, 0);

    return true;
}

static void release_default_textures(texture_system_state* state) {
    if (state) {
        kresource_system_release(state->kresource_system, state->default_kresource_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_base_colour_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_specular_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_normal_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_mra_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_cube_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_water_normal_texture->base.name);
        kresource_system_release(state->kresource_system, state->default_kresource_water_dudv_texture->base.name);
    }
}

static void increment_generation(kresource_texture* t) {
    if (t) {
        t->base.generation++;
        // Ensure we don't land on invalid before rolling over.
        if (t->base.generation == INVALID_ID_U8) {
            t->base.generation = 0;
        }
    }
}

static void invalidate_texture(kresource_texture* t) {
    if (t) {
        kzero_memory(t, sizeof(kresource_texture));
        t->base.generation = INVALID_ID_U8;
        t->renderer_texture_handle = khandle_invalid();
    }
}

static kresource_texture* default_texture_by_name(texture_system_state* state, kname name) {
    if (name == state->default_kresource_texture->base.name) {
        return state->default_kresource_texture;
    } else if (name == state->default_kresource_base_colour_texture->base.name) {
        return state->default_kresource_base_colour_texture;
    } else if (name == state->default_kresource_normal_texture->base.name) {
        return state->default_kresource_normal_texture;
    } else if (name == state->default_kresource_specular_texture->base.name) {
        return state->default_kresource_specular_texture;
    } else if (name == state->default_kresource_mra_texture->base.name) {
        return state->default_kresource_mra_texture;
    } else if (name == state->default_kresource_cube_texture->base.name) {
        return state->default_kresource_cube_texture;
    } else if (state->default_kresource_water_normal_texture && name == state->default_kresource_water_normal_texture->base.name) {
        return state->default_kresource_water_normal_texture;
    } else if (state->default_kresource_water_dudv_texture && name == state->default_kresource_water_dudv_texture->base.name) {
        return state->default_kresource_water_dudv_texture;
    }

    return 0;
}

static kresource_texture* request_writeable_arrayed(kname name, u32 width, u32 height, texture_format format, b8 has_transparency, texture_type type, u16 array_size, b8 is_depth, b8 is_stencil, b8 multiframe_buffering) {

    struct kresource_system_state* kresource_system = engine_systems_get()->kresource_state;
    kresource_texture_request_info request = {0};
    kzero_memory(&request, sizeof(kresource_texture_request_info));
    request.texture_type = type;
    request.array_size = array_size;
    request.flags = TEXTURE_FLAG_IS_WRITEABLE;
    request.flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    request.flags |= is_depth ? TEXTURE_FLAG_DEPTH : 0;
    request.flags |= is_stencil ? TEXTURE_FLAG_STENCIL : 0;
    request.flags |= multiframe_buffering ? TEXTURE_FLAG_RENDERER_BUFFERING : 0;
    request.width = width;
    request.height = height;
    request.format = format;
    request.mip_levels = 1; // TODO: configurable?
    request.base.type = KRESOURCE_TYPE_TEXTURE;
    request.flip_y = false; // Doesn't really matter anyway for this type.
    kresource_texture* t = (kresource_texture*)kresource_system_request(kresource_system, name, (kresource_request_info*)&request);
    if (!t) {
        KERROR("Failed to request resources for arrayed writeable texture");
        return 0;
    }

    return t;
}
