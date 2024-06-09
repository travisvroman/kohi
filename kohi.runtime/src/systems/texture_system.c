#include "texture_system.h"

#include "containers/hashtable.h"
#include "core/engine.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "renderer/renderer_frontend.h"
#include "resources/loaders/image_loader.h"
#include "resources/resource_types.h"
#include "strings/kstring.h"
#include "systems/job_system.h"
#include "systems/resource_system.h"
#include "threads/threadpool.h"
#include "threads/worker_thread.h"

typedef struct texture_system_state {
    texture_system_config config;
    texture default_texture;
    texture default_diffuse_texture;
    texture default_specular_texture;
    texture default_normal_texture;
    texture default_combined_texture;
    texture default_cube_texture;
    texture default_terrain_texture;

    // Array of registered textures.
    texture* registered_textures;

    // Hashtable for texture lookups.
    hashtable registered_texture_table;

    // A convenience pointer to the renderer system state.
    struct renderer_system_state* renderer;
} texture_system_state;

typedef struct texture_reference {
    u64 reference_count;
    u32 handle;
    b8 auto_release;
} texture_reference;

// Also used as result_data from job.
typedef struct texture_load_params {
    char* resource_name;
    texture* out_texture;
    texture temp_texture;
    resource image_resource;
} texture_load_params;

typedef struct texture_load_layered_params {
    char* name;
    u32 layer_count;
    char** layer_names;
    texture* out_texture;
} texture_load_layered_params;

typedef enum texture_load_job_code {
    TEXTURE_LOAD_JOB_CODE_FIRST_QUERY_FAILED,
    TEXTURE_LOAD_JOB_CODE_RESOURCE_LOAD_FAILED,
    TEXTURE_LOAD_JOB_CODE_RESOURCE_DIMENSION_MISMATCH,
} texture_load_job_code;

typedef struct texture_load_layered_result {
    char* name;
    u32 layer_count;
    texture* out_texture;
    u64 data_block_size;
    u8* data_block;
    texture temp_texture;
    texture_load_job_code result_code;
} texture_load_layered_result;

static texture_system_state* state_ptr = 0;

static b8 create_default_textures(texture_system_state* state);
static void destroy_default_textures(texture_system_state* state);
static b8 load_texture(const char* texture_name, texture* t, const char** layer_names);
static u8* load_and_combine_cube_textures(const char texture_names[6][TEXTURE_NAME_MAX_LENGTH], u32* out_width, u32* out_height, u8* out_channel_count);
static void destroy_texture(texture* t);
static b8 process_texture_reference(const char* name, i8 reference_diff, b8 auto_release, u32* out_texture_id, b8* needs_creation);
static b8 create_texture(texture* t, texture_type type, u32 width, u32 height, u8 channel_count, u16 array_size, const char** layer_texture_names, b8 is_writeable, b8 skip_load);

b8 texture_system_initialize(u64* memory_requirement, void* state, void* config) {
    texture_system_config* typed_config = (texture_system_config*)config;
    if (typed_config->max_texture_count == 0) {
        KFATAL("texture_system_initialize - config.max_texture_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    u64 struct_requirement = sizeof(texture_system_state);
    u64 array_requirement = sizeof(texture) * typed_config->max_texture_count;
    u64 hashtable_requirement = sizeof(texture_reference) * typed_config->max_texture_count;
    *memory_requirement = struct_requirement + array_requirement + hashtable_requirement;

    if (!state) {
        return true;
    }

    state_ptr = state;
    state_ptr->config = *typed_config;

    // The array block is after the state. Already allocated, so just set the pointer.
    void* array_block = state + struct_requirement;
    state_ptr->registered_textures = array_block;

    // Keep a pointer to the renderer system state.
    state_ptr->renderer = engine_systems_get()->renderer_system;

    // Hashtable block is after array.
    void* hashtable_block = array_block + array_requirement;

    // Create a hashtable for texture lookups.
    hashtable_create(sizeof(texture_reference), typed_config->max_texture_count, hashtable_block, false, &state_ptr->registered_texture_table);

    // Fill the hashtable with invalid references to use as a default.
    texture_reference invalid_ref;
    invalid_ref.auto_release = false;
    invalid_ref.handle = INVALID_ID; // Primary reason for needing default values.
    invalid_ref.reference_count = 0;
    hashtable_fill(&state_ptr->registered_texture_table, &invalid_ref);

    // Invalidate all textures in the array.
    u32 count = state_ptr->config.max_texture_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->registered_textures[i].id = INVALID_ID;
    }

    // Create default textures for use in the system.
    create_default_textures(state_ptr);

    return true;
}

void texture_system_shutdown(void* state) {
    if (state_ptr) {
        // Destroy all loaded textures.
        for (u32 i = 0; i < state_ptr->config.max_texture_count; ++i) {
            texture* t = &state_ptr->registered_textures[i];
            renderer_texture_resources_release(state_ptr->renderer, t->renderer_texture_handle);
        }

        destroy_default_textures(state_ptr);

        state_ptr->renderer = 0;
        state_ptr = 0;
    }
}

texture* texture_system_acquire(const char* name, b8 auto_release) {
    // Return default texture, but warn about it since this should be returned via get_default_texture();
    // TODO: Check against other default texture names?
    if (strings_equali(name, DEFAULT_TEXTURE_NAME)) {
        KWARN("texture_system_acquire called for default texture. Use texture_system_get_default_texture for texture 'default'.");
        return &state_ptr->default_texture;
    }

    if (strings_equali(name, DEFAULT_DIFFUSE_TEXTURE_NAME)) {
        KWARN("texture_system_acquire called for default diffuse texture. Use texture_system_get_default_diffuse_texture for texture 'default_DIFF'.");
        return &state_ptr->default_diffuse_texture;
    }

    if (strings_equali(name, DEFAULT_SPECULAR_TEXTURE_NAME)) {
        KWARN("texture_system_acquire called for default texture. Use texture_system_get_default_specular_texture for texture 'default_SPEC'.");
        return &state_ptr->default_specular_texture;
    }

    if (strings_equali(name, DEFAULT_NORMAL_TEXTURE_NAME)) {
        KWARN("texture_system_acquire called for default texture. Use texture_system_get_default_normal_texture for texture 'default_NORM'.");
        return &state_ptr->default_normal_texture;
    }

    u32 id = INVALID_ID;
    b8 needs_creation = false;
    // NOTE: Increments reference count, or creates new entry.
    if (!process_texture_reference(name, 1, auto_release, &id, &needs_creation)) {
        KERROR("texture_system_acquire failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];

    // Create it, if needed.
    if (needs_creation) {
        if (!create_texture(t, TEXTURE_TYPE_2D, 0, 0, 0, 1, 0, false, false)) {
            KERROR("texture_system_acquire failed to create new texture.");
            return 0;
        }
    }

    return t;
}

texture* texture_system_acquire_cube(const char* name, b8 auto_release) {
    // Return default texture, but warn about it since this should be returned via get_default_texture();
    // TODO: Check against other default texture names?
    if (strings_equali(name, DEFAULT_TEXTURE_NAME)) {
        KWARN("texture_system_acquire_cube called for default texture. Use texture_system_get_default_texture for texture 'default'.");
        return &state_ptr->default_texture;
    }

    u32 id = INVALID_ID;
    b8 needs_creation = false;
    // NOTE: Increments reference count, or creates new entry.
    if (!process_texture_reference(name, 1, auto_release, &id, &needs_creation)) {
        KERROR("texture_system_acquire_cube failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];

    // Create it, if needed.
    if (needs_creation) {
        if (!create_texture(t, TEXTURE_TYPE_CUBE, 0, 0, 0, 6, 0, false, false)) {
            KERROR("texture_system_acquire failed to create new texture.");
            return 0;
        }
    }

    return t;
}

texture* texture_system_acquire_writeable(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency) {
    return texture_system_acquire_writeable_arrayed(name, width, height, channel_count, has_transparency, TEXTURE_TYPE_2D, 1);
}

texture* texture_system_acquire_writeable_arrayed(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency, texture_type type, u16 array_size) {
    u32 id = INVALID_ID;
    b8 needs_creation = false;
    if (!process_texture_reference(name, 1, false, &id, &needs_creation)) {
        KERROR("texture_system_acquire_writeable_arrayed failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];

    // Create it, if needed.
    if (needs_creation) {
        if (!create_texture(t, type, width, height, channel_count, array_size, 0, true, true)) {
            KERROR("texture_system_acquire failed to create new texture.");
            return 0;
        }
    }

    t->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    return t;
}

texture* texture_system_acquire_textures_as_arrayed(const char* name, u32 layer_count, const char** layer_texture_names, b8 auto_release) {
    if (layer_count < 1) {
        KERROR("Must contain at least one layer.");
        return 0;
    }

    b8 needs_creation = false;
    u32 id = INVALID_ID;
    if (!process_texture_reference(name, 1, auto_release, &id, &needs_creation)) {
        KERROR("texture_system_acquire_textures_as_arrayed failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];

    // Create it, if needed.
    if (needs_creation) {
        if (!create_texture(t, TEXTURE_TYPE_2D_ARRAY, 0, 0, 0, layer_count, layer_texture_names, false, false)) {
            KERROR("texture_system_acquire_textures_as_arrayed failed to create new texture.");
            return 0;
        }
    }

    return t;
}

void texture_system_release(const char* name) {
    // Ignore release requests for the default texture.
    // TODO: Check against other default texture names as well?
    if (strings_equali(name, DEFAULT_TEXTURE_NAME)) {
        return;
    }
    u32 id = INVALID_ID;
    b8 needs_creation;
    // NOTE: Decrement the reference count.
    if (!process_texture_reference(name, -1, false, &id, &needs_creation)) {
        KERROR("texture_system_release failed to release texture '%s' properly.", name);
    }
}

void texture_system_wrap_internal(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency, b8 is_writeable, b8 register_texture, k_handle renderer_texture_handle, texture* out_texture) {
    u32 id = INVALID_ID;
    b8 needs_creation;
    texture* t = 0;
    if (register_texture) {
        // NOTE: Wrapped textures are never auto-released because it means that thier
        // resources are created and managed somewhere within the renderer internals.
        if (!process_texture_reference(name, 1, false, &id, &needs_creation)) {
            KERROR("texture_system_wrap_internal failed to obtain a new texture id.");
            return;
        }
        t = &state_ptr->registered_textures[id];
    } else {
        if (out_texture) {
            t = out_texture;
        } else {
            t = kallocate(sizeof(texture), MEMORY_TAG_TEXTURE);
            // KTRACE("texture_system_wrap_internal created texture '%s', but not registering, resulting in an allocation. It is up to the caller to free this memory.", name);
        }
    }

    t->id = id;
    t->type = TEXTURE_TYPE_2D;
    t->name = string_duplicate(name);
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    t->flags |= is_writeable ? TEXTURE_FLAG_IS_WRITEABLE : 0;
    t->flags |= TEXTURE_FLAG_IS_WRAPPED;
    t->renderer_texture_handle = renderer_texture_handle;
}

b8 texture_system_resize(texture* t, u32 width, u32 height, b8 regenerate_internal_data) {
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
            return renderer_texture_resize(state_ptr->renderer, t->renderer_texture_handle, width, height);
        }
        return true;
    }
    return false;
}

b8 texture_system_write_data(texture* t, u32 offset, u32 size, void* data) {
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

b8 texture_system_is_default_texture(texture* t) {
    if (!state_ptr) {
        return false;
    }
    return (t == &state_ptr->default_texture) ||
           (t == &state_ptr->default_diffuse_texture) ||
           (t == &state_ptr->default_normal_texture) ||
           (t == &state_ptr->default_specular_texture) ||
           (t == &state_ptr->default_combined_texture) ||
           (t == &state_ptr->default_terrain_texture) ||
           (t == &state_ptr->default_cube_texture);
}

texture* texture_system_get_default_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_texture, "texture_system_get_default_texture");
}

texture* texture_system_get_default_diffuse_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_diffuse_texture, "texture_system_get_default_diffuse_texture");
}

texture* texture_system_get_default_specular_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_specular_texture, "texture_system_get_default_specular_texture");
}

texture* texture_system_get_default_normal_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_normal_texture, "texture_system_get_default_normal_texture");
}

texture* texture_system_get_default_combined_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_combined_texture, "texture_system_get_default_metallic_texture");
}

texture* texture_system_get_default_cube_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_cube_texture, "texture_system_get_default_cube_texture");
}

texture* texture_system_get_default_terrain_texture(void) {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_terrain_texture, "texture_system_get_default_terrain_texture");
}

struct texture_internal_data* texture_system_get_internal_or_default(texture* t, u8* out_generation) {
    if (!t || !out_generation) {
        return 0;
    }

    // Texture isn't loaded yet, use a default.
    if (t->generation == INVALID_ID_U8) {
        // Texture generations are always invalid for default textures, so
        // check first if already using one.
        if (!texture_system_is_default_texture(t)) {
            // If not using one, grab the default by type. This is only here as a failsafe
            // and to be used while assets are loading.
            switch (t->type) {
            case TEXTURE_TYPE_2D:
                t = texture_system_get_default_texture();
                break;
            case TEXTURE_TYPE_2D_ARRAY:
                // TODO: Making the assumption this is a terrain.
                // Should probably acquire a new default texture with the
                // corresponding number of layers instead.
                t = texture_system_get_default_terrain_texture();
                break;
            case TEXTURE_TYPE_CUBE:
                t = texture_system_get_default_cube_texture();
                break;
            default:
                KWARN("Texture system failed to determine texture type while getting internal data. Falling back to 2D.");
                t = texture_system_get_default_texture();
                break;
            }
        }

        // Since using a default texture, set the outward generation to invalid id
        *out_generation = INVALID_ID_U8;

    } else {
        // Set the actual texture generation.
        *out_generation = t->generation;
    }

    return renderer_texture_internal_get(state_ptr->renderer, t->renderer_texture_handle);
}

static b8 create_and_upload_texture(texture* t, const char* name, texture_type type, u32 width, u32 height, u8 channel_count, u8 mip_levels, u16 array_size, texture_flag_bits flags, u32 offset, u8* pixels) {
    // Setup basic texture properties.
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->flags = flags;
    t->type = type;
    t->mip_levels = mip_levels;
    t->array_size = array_size;

    // Take a copy of the name.
    t->name = string_duplicate(name);

    // Acquire backing renderer resources.
    if (!renderer_texture_resources_acquire(state_ptr->renderer, t->name, t->type, t->width, t->height, t->channel_count, t->mip_levels, t->array_size, t->flags, &t->renderer_texture_handle)) {
        KERROR("Failed to acquire renderer resources for default texture '%s'. See logs for details.", name);
        string_free(t->name);
        t->name = 0;
        return false;
    }

    // Upload the texture data.
    u32 size = t->width * t->height * t->channel_count * t->array_size;
    if (!renderer_texture_write_data(state_ptr->renderer, t->renderer_texture_handle, 0, size, pixels)) {
        KERROR("Failed to write texture data for default texture '%s'", name);
        string_free(t->name);
        t->name = 0;
        // Since this failed, make sure to release the texture's backing renderer resources.
        renderer_texture_resources_release(state_ptr->renderer, t->renderer_texture_handle);
        // Also zero out memory so there's no mistaking this for an active texture.
        kzero_memory(t, sizeof(texture));

        return false;
    }

    return true;
}

static b8 create_default_texture(texture* t, u8* pixels, u32 tex_dimension, const char* name) {
    // Acquire internal texture resources and upload to GPU.
    return create_and_upload_texture(t, name, TEXTURE_TYPE_2D, tex_dimension, tex_dimension, 4, 1, 1, 0, 0, pixels);
}

static b8 create_default_cube_texture(texture* t, const char* name) {
    const u32 tex_dimension = 16;
    const u32 channels = 4;
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
    u64 image_size = tex_dimension * tex_dimension * channels * 6;
    u8* pixels = kallocate(sizeof(u8) * image_size, MEMORY_TAG_ARRAY);
    for (u8 i = 0; i < 6; ++i) {
        kcopy_memory(pixels + image_size * i, cube_side_pixels, image_size);
    }

    // Acquire internal texture resources and upload to GPU.
    b8 result = create_and_upload_texture(t, name, TEXTURE_TYPE_CUBE, tex_dimension, tex_dimension, 4, 1, 6, 0, 0, pixels);

    // Cleanup pixels array.
    kfree(pixels, sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
    pixels = 0;

    return result;
}

static b8 create_default_layered_texture(texture* t, u32 layer_count, u8* all_layer_pixels, u32 tex_dimension, const char* name) {
    // Acquire internal texture resources and upload to GPU.
    return create_and_upload_texture(t, name, TEXTURE_TYPE_2D_ARRAY, tex_dimension, tex_dimension, 4, 1, layer_count, 0, 0, all_layer_pixels);
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
    create_default_texture(&state->default_texture, pixels, tex_dimension, DEFAULT_TEXTURE_NAME);

    // Diffuse texture.
    KTRACE("Creating default diffuse texture...");
    u8 diff_pixels[16 * 16 * 4];
    // Default diffuse map is all white.
    kset_memory(diff_pixels, 255, sizeof(u8) * 16 * 16 * 4);
    create_default_texture(&state->default_diffuse_texture, diff_pixels, 16, DEFAULT_DIFFUSE_TEXTURE_NAME);

    // Specular texture.
    KTRACE("Creating default specular texture...");
    u8 spec_pixels[16 * 16 * 4];
    // Default spec map is black (no specular)
    kset_memory(spec_pixels, 0, sizeof(u8) * 16 * 16 * 4);
    create_default_texture(&state->default_specular_texture, spec_pixels, 16, DEFAULT_SPECULAR_TEXTURE_NAME);

    // Normal texture.
    KTRACE("Creating default normal texture...");
    u8 normal_pixels[16 * 16 * 4]; // w * h * channels
    kset_memory(normal_pixels, 255, sizeof(u8) * 16 * 16 * 4);

    // Each pixel.
    for (u64 row = 0; row < 16; ++row) {
        for (u64 col = 0; col < 16; ++col) {
            u64 index = (row * 16) + col;
            u64 index_bpp = index * channels;
            // Set blue, z-axis by default and alpha.
            normal_pixels[index_bpp + 0] = 128;
            normal_pixels[index_bpp + 1] = 128;
        }
    }
    create_default_texture(&state->default_normal_texture, normal_pixels, 16, DEFAULT_NORMAL_TEXTURE_NAME);

    // Combined texture
    KTRACE("Creating default combined (metallic, roughness, AO) texture...");
    u8 combined_pixels[16 * 16 * 4]; // w * h * channels
    kset_memory(combined_pixels, 255, sizeof(u8) * 16 * 16 * 4);

    // Each pixel.
    for (u64 row = 0; row < 16; ++row) {
        for (u64 col = 0; col < 16; ++col) {
            u64 index = (row * 16) + col;
            u64 index_bpp = index * channels;
            combined_pixels[index_bpp + 0] = 0;   // Default for metallic is black.
            combined_pixels[index_bpp + 1] = 128; // Default for roughness is medium grey
            combined_pixels[index_bpp + 2] = 255; // Default for AO is white.
        }
    }
    create_default_texture(&state->default_combined_texture, combined_pixels, 16, DEFAULT_COMBINED_TEXTURE_NAME);

    // Cube texture.
    KTRACE("Creating default cube texture...");
    create_default_cube_texture(&state->default_cube_texture, DEFAULT_CUBE_TEXTURE_NAME);

    // Default terrain textures. 4 materials, 3 maps per, for 12 layers.
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
        kcopy_memory(terrain_pixels + (material_size * i) + (layer_size * 2), combined_pixels, layer_size);
    }
    create_default_layered_texture(&state->default_terrain_texture, layer_count, terrain_pixels, 16, DEFAULT_TERRAIN_TEXTURE_NAME);
    kfree(terrain_pixels, layer_size * layer_count, MEMORY_TAG_ARRAY);

    return true;
}

static void destroy_default_textures(texture_system_state* state) {
    if (state) {
        destroy_texture(&state->default_texture);
        destroy_texture(&state->default_diffuse_texture);
        destroy_texture(&state->default_specular_texture);
        destroy_texture(&state->default_normal_texture);
        destroy_texture(&state->default_combined_texture);
        destroy_texture(&state->default_cube_texture);
        destroy_texture(&state->default_terrain_texture);
    }
}

static u8* load_and_combine_cube_textures(const char texture_names[6][TEXTURE_NAME_MAX_LENGTH], u32* out_width, u32* out_height, u8* out_channel_count) {
    u8* pixels = 0;

    // Size of each side's image.
    u64 side_image_size = 0;

    for (u8 i = 0; i < 6; ++i) {
        image_resource_params params;
        params.flip_y = false;

        resource img_resource;
        if (!resource_system_load(texture_names[i], RESOURCE_TYPE_IMAGE, &params, &img_resource)) {
            KERROR("load_and_combine_cube_textures() - Failed to load image resource for texture '%s'", texture_names[i]);
            return false;
        }

        image_resource_data* resource_data = img_resource.data;
        if (!pixels) {
            *out_width = resource_data->width;
            *out_height = resource_data->height;
            *out_channel_count = resource_data->channel_count;

            side_image_size = resource_data->width * resource_data->height * resource_data->channel_count;
            // NOTE: no need for transparency in cube maps, so not checking for it.

            pixels = kallocate(sizeof(u8) * side_image_size * 6, MEMORY_TAG_ARRAY);
        } else {
            // Verify all textures are the same size.
            if (*out_width != resource_data->width || *out_height != resource_data->height || *out_channel_count != resource_data->channel_count) {
                KERROR("load_and_combine_cube_textures - All textures must be the same resolution and bit depth.");
                if (pixels) {
                    kfree(pixels, sizeof(u8) * side_image_size * 6, MEMORY_TAG_ARRAY);
                }
                return 0;
            }
        }

        // Copy to the relevant portion of the array.
        kcopy_memory(pixels + side_image_size * i, resource_data->pixels, side_image_size);

        // Clean up data.
        resource_system_unload(&img_resource);
    }

    return pixels;
}

static void texture_load_job_success(void* params) {
    texture_load_params* texture_params = (texture_load_params*)params;

    // This also handles the GPU upload. Can't be jobified until the renderer is multithreaded.
    image_resource_data* resource_data = (image_resource_data*)texture_params->image_resource.data;

    // Acquire internal texture resources. Can't be jobified until the renderer is multithreaded.
    texture* t = &texture_params->temp_texture;
    if (!renderer_texture_resources_acquire(
            state_ptr->renderer, t->name, t->type, t->width, t->height, t->channel_count, t->mip_levels, t->array_size, t->flags, &t->renderer_texture_handle)) {
        KERROR("Error acquiring renderer backing resources for texture '%s'.", t->name);
        return;
    }

    // Upload pixel data to GPU.
    u64 total_size = t->width * t->height * t->channel_count;
    if (!renderer_texture_write_data(state_ptr->renderer, t->renderer_texture_handle, 0, total_size, resource_data->pixels)) {
        KERROR("Error uploading pixel data to GPU for texture '%s'", t->name);
    }

    // Unload the resource.
    resource_system_unload(&texture_params->image_resource);

    // Take a copy of the old texture.
    texture old = *texture_params->out_texture;

    // Assign the temp texture to the pointer.
    *texture_params->out_texture = texture_params->temp_texture;

    // Destroy the old texture.
    renderer_texture_resources_release(state_ptr->renderer, old.renderer_texture_handle);

    KTRACE("Successfully loaded texture '%s'.", texture_params->resource_name);

    // Clean up data.
    if (texture_params->resource_name) {
        u32 length = string_length(texture_params->resource_name);
        kfree(texture_params->resource_name, sizeof(char) * length + 1, MEMORY_TAG_STRING);
        texture_params->resource_name = 0;
    }
}

static void texture_load_job_fail(void* params) {
    texture_load_params* texture_params = (texture_load_params*)params;

    KERROR("Failed to load texture '%s'.", texture_params->resource_name);

    resource_system_unload(&texture_params->image_resource);
}

static b8 texture_load_job_start(void* params, void* result_data) {
    texture_load_params* load_params = (texture_load_params*)params;

    image_resource_params resource_params;
    resource_params.flip_y = true;

    b8 result = resource_system_load(load_params->resource_name, RESOURCE_TYPE_IMAGE, &resource_params, &load_params->image_resource);

    image_resource_data* resource_data = load_params->image_resource.data;

    // Use a temporary texture to load into.
    load_params->temp_texture.width = resource_data->width;
    load_params->temp_texture.height = resource_data->height;
    load_params->temp_texture.channel_count = resource_data->channel_count;
    load_params->temp_texture.mip_levels = resource_data->mip_levels;
    load_params->temp_texture.type = TEXTURE_TYPE_2D;
    load_params->temp_texture.array_size = 1;

    load_params->out_texture->mip_levels = resource_data->mip_levels;

    u64 total_size = load_params->temp_texture.width * load_params->temp_texture.height * load_params->temp_texture.channel_count;
    // Check for transparency
    b32 has_transparency = false;
    for (u64 i = 0; i < total_size; i += load_params->temp_texture.channel_count) {
        u8 a = resource_data->pixels[i + 3];
        if (a < 255) {
            has_transparency = true;
            break;
        }
    }

    // Take a copy of the name.
    load_params->temp_texture.name = string_duplicate(load_params->resource_name);
    load_params->temp_texture.flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;

    // NOTE: The load params are also used as the result data here, only the image_resource field is populated now.
    kcopy_memory(result_data, load_params, sizeof(texture_load_params));

    return result;
}

// Layered texture job callbacks.
static void texture_load_layered_job_success(void* result) {
    texture_load_layered_result* typed_result = (texture_load_layered_result*)result;

    // Acquire internal texture resources. Can't be jobified until the renderer is multithreaded.
    texture* t = &typed_result->temp_texture;
    if (!renderer_texture_resources_acquire(
            state_ptr->renderer, t->name, t->type, t->width, t->height, t->channel_count, t->mip_levels, t->array_size, t->flags, &t->renderer_texture_handle)) {
        KERROR("Error acquiring renderer backing resources for texture '%s'.", t->name);
        return;
    }

    // Upload pixel data to GPU.
    u64 total_size = t->width * t->height * t->channel_count;
    if (!renderer_texture_write_data(state_ptr->renderer, t->renderer_texture_handle, 0, total_size, typed_result->data_block)) {
        KERROR("Error uploading pixel data to GPU for texture '%s'", t->name);
    }

    // Take a copy of the old texture.
    texture old = *typed_result->out_texture;

    // Assign the temp texture to the pointer.
    *typed_result->out_texture = typed_result->temp_texture;

    // Destroy the old texture.
    renderer_texture_resources_release(state_ptr->renderer, old.renderer_texture_handle);

    if (typed_result->name) {
        KTRACE("Successfully loaded layered texture '%s'.", typed_result->name);
        string_free(typed_result->name);
        typed_result->name = 0;
    } else {
        KTRACE("Successfully loaded layered texture.");
    }
    kfree(typed_result->data_block, typed_result->data_block_size, MEMORY_TAG_ARRAY);
}

static void texture_load_layered_job_fail(void* result_data) {
    texture_load_layered_result* typed_result = result_data;

    switch (typed_result->result_code) {
    case TEXTURE_LOAD_JOB_CODE_RESOURCE_LOAD_FAILED:
        KERROR("Layered texture load failed to load one or more resources.");
        break;
    case TEXTURE_LOAD_JOB_CODE_FIRST_QUERY_FAILED:
        KERROR("Failed to query properties for first layer image. Unable to create arrayed texture.");
        break;
    case TEXTURE_LOAD_JOB_CODE_RESOURCE_DIMENSION_MISMATCH:
        KERROR("Failed to load the layered image because at least one layer's texture is the wrong size.");
        break;
    default:
        KERROR("Layered texture load failed for an unknown reason.");
        break;
    }
}

typedef struct layer_work_info {
    u32 layer;
    u64 layer_size;
    u32 first_width;
    u32 first_height;
    u8** data_block;
    b8 result;
    texture_load_job_code result_code;
    char* layer_name;
    b8 has_transparency;
} layer_work_info;

static u32 layered_texture_do_work(void* param) {
    layer_work_info* info = param;
    info->result = true;

    image_resource_params resource_params;
    resource_params.flip_y = true;

    resource image_resource;
    if (!resource_system_load(info->layer_name, RESOURCE_TYPE_IMAGE, &resource_params, &image_resource)) {
        info->result_code = TEXTURE_LOAD_JOB_CODE_RESOURCE_LOAD_FAILED;
        info->result = false;
        return false;
    }

    image_resource_data* resource_data = image_resource.data;

    // Verify the dimensions match that of the first layer's texture.
    if (resource_data->width != info->first_width || resource_data->height != info->first_height) {
        info->result_code = TEXTURE_LOAD_JOB_CODE_RESOURCE_DIMENSION_MISMATCH;
        resource_system_unload(&image_resource);
        info->result = false;
        return false;
    }

    // Check for transparency
    info->has_transparency = false;
    for (u64 i = 0; i < info->layer_size; i += resource_data->channel_count) {
        u8 a = resource_data->pixels[i + 3];
        if (a < 255) {
            info->has_transparency = true;
            break;
        }
    }

    // Insert the pixels into the corresponding "layer".
    u8* data_location = *info->data_block + (info->layer * info->layer_size);
    kcopy_memory(data_location, resource_data->pixels, info->layer_size);

    resource_system_unload(&image_resource);

    return info->result;
}

static b8 texture_load_layered_job_start(void* params, void* result_data) {
    texture_load_layered_params* load_params = (texture_load_layered_params*)params;
    texture_load_layered_result* typed_result = result_data;

    // LEFTOFF: Split this into 3 jobs - The first job performs the query, then sets up all of the layered texture jobs, then sets
    // up a final job to move all that memory into the contiguous block in a final job, which has the layer jobs as a dependency.
    // THEN submit all jobs.

    // Query the dimensions of the first image. All subsequent images must match dimensions.
    // Channel count from the image is ignored.
    i32 first_width, first_height, channel_count;
    u32 mip_levels;
    if (!image_loader_query_properties(load_params->layer_names[0], &first_width, &first_height, &channel_count, &mip_levels)) {
        typed_result->result_code = TEXTURE_LOAD_JOB_CODE_FIRST_QUERY_FAILED;
        return false;
    }

    // Once the first image size is acquired, allocate enough memory for
    // it's dimensions * channels * layer count. Note that 4 channels are always required here.
    const u32 layer_channel_count = 4;
    u32 layer_size = sizeof(u8) * (u32)first_width * (u32)first_height * layer_channel_count;
    typed_result->data_block_size = layer_size * load_params->layer_count;
    typed_result->data_block = kallocate(typed_result->data_block_size, MEMORY_TAG_ARRAY);
    typed_result->out_texture = load_params->out_texture;

    b8 has_transparency = false;

    // Create a temporary texture to load into, so that if an existing texture is being used, we don't trash memory
    // that's currently in use for a draw, etc.
    typed_result->temp_texture = (texture){};
    // Use a temporary texture to load into.
    typed_result->temp_texture.width = first_width;
    typed_result->temp_texture.height = first_height;
    typed_result->temp_texture.channel_count = layer_channel_count;
    typed_result->temp_texture.mip_levels = mip_levels;
    typed_result->temp_texture.array_size = load_params->layer_count;
    // Copy relevant properties from the original texture.
    typed_result->temp_texture.type = load_params->out_texture->type;
    typed_result->temp_texture.id = load_params->out_texture->id;
    typed_result->temp_texture.flags = load_params->out_texture->flags;

    image_resource_params resource_params;
    resource_params.flip_y = true;

    // Create a threadpool to load layers simultaneously.
    threadpool pool = {0};
    if (!threadpool_create(load_params->layer_count, &pool)) {
        KERROR("Failed to create threadpool. See logs for details.");
        return false;
    }

    // Create a job for each layer, but don't submit it yet.
    layer_work_info* infos = kallocate(sizeof(layer_work_info) * load_params->layer_count, MEMORY_TAG_ARRAY);
    u32 layer = 0;
    for (; layer < load_params->layer_count; ++layer) {
        layer_work_info* info = &infos[layer];
        info->layer = layer;
        info->layer_size = layer_size;
        info->first_width = first_width;
        info->first_height = first_height;
        info->data_block = &typed_result->data_block;
        info->result = false;
        info->layer_name = load_params->layer_names[layer];

        if (!worker_thread_add(&pool.threads[layer], layered_texture_do_work, info)) {
            KERROR("Failed to add work to worker thread.");
            return false;
        }
    }

    // Start the work.
    for (u32 i = 0; i < load_params->layer_count; ++i) {
        worker_thread_start(&pool.threads[i]);
    }

    // Wait on the threads.
    if (!threadpool_wait(&pool)) {
        KERROR("Threadpool wait failed.");
        return false;
    }

    threadpool_destroy(&pool);

    // Verify results.
    b8 load_result = true;
    for (u32 i = 0; i < load_params->layer_count; ++i) {
        if (!infos[i].result) {
            load_result = false;
        }
    }

    kfree(infos, sizeof(layer_work_info), MEMORY_TAG_ARRAY);

    for (u32 i = 0; i < typed_result->layer_count; ++i) {
        string_free(load_params->layer_names[i]);
    }
    kfree(load_params->layer_names, sizeof(char*) * load_params->layer_count, MEMORY_TAG_ARRAY);
    load_params->layer_names = 0;

    if (load_result) {
        typed_result->temp_texture.flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
        typed_result->name = string_duplicate(load_params->name);
        typed_result->layer_count = load_params->layer_count;

    } else {
        KERROR("At least one texture layer failed to load. See logs for details.");

        if (typed_result->data_block) {
            kfree(typed_result->data_block, layer_size * load_params->layer_count, MEMORY_TAG_ARRAY);
            typed_result->data_block = 0;
        }
    }
    return load_result;
}

static b8 load_texture(const char* texture_name, texture* t, const char** layer_names) {
    if (t->type == TEXTURE_TYPE_2D) {
        // Kick off a texture loading job. Only handles loading from disk
        // to CPU. GPU upload is handled after completion of this job.
        texture_load_params params = {0};
        params.resource_name = string_duplicate(texture_name);
        params.out_texture = t;
        params.image_resource = (resource){};
        params.temp_texture = (texture){};
        params.temp_texture.array_size = t->array_size;

        job_info job = job_create(texture_load_job_start, texture_load_job_success, texture_load_job_fail, &params, sizeof(texture_load_params), sizeof(texture_load_params));
        job_system_submit(job);
    } else if (t->type == TEXTURE_TYPE_2D_ARRAY) {
        texture_load_layered_params params = {0};
        params.layer_count = t->array_size;
        params.name = string_duplicate(texture_name);
        params.layer_names = kallocate(sizeof(char*) * t->array_size, MEMORY_TAG_ARRAY);
        for (u32 i = 0; i < t->array_size; ++i) {
            params.layer_names[i] = string_duplicate(layer_names[i]);
        }
        params.out_texture = t;

        job_info job = job_create(
            texture_load_layered_job_start,
            texture_load_layered_job_success,
            texture_load_layered_job_fail,
            &params,
            sizeof(texture_load_layered_params),
            sizeof(texture_load_layered_result));

        job_system_submit(job);
    } else {
        KERROR("Texture system attempted to load unknown texture type: %u", t->type);
        return false;
    }
    return true;
}

static void destroy_texture(texture* t) {
    // Clean up backend resources.
    renderer_texture_resources_release(state_ptr->renderer, t->renderer_texture_handle);

    string_free(t->name);
    kzero_memory(t, sizeof(texture));
    t->id = INVALID_ID;
}

static b8 create_texture(texture* t, texture_type type, u32 width, u32 height, u8 channel_count, u16 array_size, const char** layer_texture_names, b8 is_writeable, b8 skip_load) {
    b8 result = false;
    // Set some values regardless of texture type.
    t->type = type;
    t->array_size = array_size;
    if (is_writeable) {
        t->flags |= TEXTURE_FLAG_IS_WRITEABLE;
    }

    // Create new texture.
    if (skip_load) {
        // For non-loaded textures, dimensions, and channel count.
        t->width = width;
        t->height = height;
        t->channel_count = channel_count;
        if (is_writeable) {
            t->mip_levels = 1;
        }

        // Acquire backing renderer resources.
        if (!renderer_texture_resources_acquire(state_ptr->renderer, t->name, t->type, t->width, t->height, t->channel_count, t->mip_levels, t->array_size, t->flags, &t->renderer_texture_handle)) {
            KERROR("Failed to acquire renderer resources for default texture '%s'. See logs for details.", t->name);
            return false;
        }

        result = true;
        // KTRACE("Load skipped for texture '%s'. This is expected behaviour.");
    } else {
        switch (t->type) {
        case TEXTURE_TYPE_CUBE: {
            char texture_names[6][TEXTURE_NAME_MAX_LENGTH];

            // +X,-X,+Y,-Y,+Z,-Z in _cubemap_ space, which is LH y-down
            string_format_unsafe(texture_names[0], "%s_r", t->name); // Right texture
            string_format_unsafe(texture_names[1], "%s_l", t->name); // Left texture
            string_format_unsafe(texture_names[2], "%s_u", t->name); // Up texture
            string_format_unsafe(texture_names[3], "%s_d", t->name); // Down texture
            string_format_unsafe(texture_names[4], "%s_f", t->name); // Front texture
            string_format_unsafe(texture_names[5], "%s_b", t->name); // Back texture

            u8* pixels = load_and_combine_cube_textures(texture_names, &t->width, &t->height, &t->channel_count);
            if (!pixels) {
                KERROR("Failed to load cube texture '%s'. See logs for details.", t->name);
                return false;
            }

            result = create_and_upload_texture(t, t->name, TEXTURE_TYPE_CUBE, t->width, t->height, t->channel_count, 1, 6, 0, 0, pixels);

            if (pixels) {
                kfree(pixels, sizeof(u8) * t->width * t->height * t->channel_count * 6, MEMORY_TAG_ARRAY);
            }
        } break;
        case TEXTURE_TYPE_2D:
        case TEXTURE_TYPE_2D_ARRAY: {
            if (!load_texture(t->name, t, layer_texture_names)) {
                KERROR("Failed to load texture '%s'.", t->name);
                return false;
            }
            result = true;
        } break;
        default: {
            KERROR("Unrecognized texture type %u. Cannot process texture reference.", t->type);
            return false;
        }
        }
    }

    return result;
}

static b8 process_texture_reference(const char* name, i8 reference_diff, b8 auto_release, u32* out_texture_id, b8* needs_creation) {
    *out_texture_id = INVALID_ID;
    *needs_creation = false;
    if (state_ptr) {
        texture_reference ref;
        if (hashtable_get(&state_ptr->registered_texture_table, name, &ref)) {
            // If the reference count starts off at zero, one of two things can be
            // true. If incrementing references, this means the entry is new. If
            // decrementing, then the texture doesn't exist _if_ not auto-releasing.
            if (ref.reference_count == 0 && reference_diff > 0) {
                if (reference_diff > 0) {
                    // This can only be changed the first time a texture is loaded.
                    ref.auto_release = auto_release;
                } else {
                    if (ref.auto_release) {
                        KWARN("Tried to release non-existent texture: '%s'", name);
                        return false;
                    } else {
                        KWARN("Tried to release a texture where autorelease=false, but references was already 0.");
                        // Still count this as a success, but warn about it.
                        return true;
                    }
                }
            }

            ref.reference_count += reference_diff;

            // Take a copy of the name since it would be wiped out if destroyed,
            // (as passed in name is generally a pointer to the actual texture's name).
            char name_copy[TEXTURE_NAME_MAX_LENGTH];
            string_ncopy(name_copy, name, TEXTURE_NAME_MAX_LENGTH);

            // If decrementing, this means a release.
            if (reference_diff < 0) {
                // Check if the reference count has reached 0. If it has, and the reference
                // is set to auto-release, destroy the texture.
                if (ref.reference_count == 0 && ref.auto_release) {
                    texture* t = &state_ptr->registered_textures[ref.handle];

                    // Destroy/reset texture.
                    destroy_texture(t);

                    // Reset the reference.
                    ref.handle = INVALID_ID;
                    ref.auto_release = false;
                    // KTRACE("Released texture '%s'., Texture unloaded because reference count=0 and auto_release=true.", name_copy);
                } else {
                    // KTRACE("Released texture '%s', now has a reference count of '%i' (auto_release=%s).", name_copy, ref.reference_count, ref.auto_release ? "true" : "false");
                }

            } else {
                // Incrementing. Check if the handle is new or not.
                if (ref.handle == INVALID_ID) {
                    // This means no texture exists here. Find a free index first.
                    u32 count = state_ptr->config.max_texture_count;

                    for (u32 i = 0; i < count; ++i) {
                        if (state_ptr->registered_textures[i].id == INVALID_ID) {
                            // A free slot has been found. Use its index as the handle.
                            ref.handle = i;
                            *out_texture_id = i;
                            break;
                        }
                    }

                    // An empty slot was not found, bleat about it and boot out.
                    if (*out_texture_id == INVALID_ID) {
                        KFATAL("process_texture_reference - Texture system cannot hold anymore textures. Adjust configuration to allow more.");
                        return false;
                    } else {
                        // Setup some basic properties on the texture.
                        texture* t = &state_ptr->registered_textures[ref.handle];
                        t->id = ref.handle;
                        // Make sure to hold onto the texture name.
                        t->name = string_duplicate(name);
                        // KTRACE("Texture '%s' does not yet exist. Created, and ref_count is now %i.", name, ref.reference_count);
                        *needs_creation = true;
                    }
                } else {
                    *out_texture_id = ref.handle;
                    // KTRACE("Texture '%s' already exists, ref_count increased to %i.", name, ref.reference_count);
                }
            }

            // Either way, update the entry.
            hashtable_set(&state_ptr->registered_texture_table, name_copy, &ref);
            return true;
        }

        // NOTE: This would only happen in the event something went wrong with the state.
        KERROR("process_texture_reference failed to acquire id for name '%s'. INVALID_ID returned.", name);
        return false;
    }

    KERROR("process_texture_reference called before texture system is initialized.");
    return false;
}
