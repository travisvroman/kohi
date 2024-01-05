#include "texture_system.h"

#include "containers/hashtable.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "renderer/renderer_frontend.h"
#include "resources/resource_types.h"
#include "systems/job_system.h"
#include "systems/resource_system.h"

typedef struct texture_system_state {
    texture_system_config config;
    texture default_texture;
    texture default_diffuse_texture;
    texture default_specular_texture;
    texture default_normal_texture;
    texture default_combined_texture;
    texture default_cube_texture;

    // Array of registered textures.
    texture* registered_textures;

    // Hashtable for texture lookups.
    hashtable registered_texture_table;
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
    u32 current_generation;
    resource image_resource;
} texture_load_params;

static texture_system_state* state_ptr = 0;

static b8 create_default_textures(texture_system_state* state);
static void destroy_default_textures(texture_system_state* state);
static b8 load_texture(const char* texture_name, texture* t);
static b8 load_cube_textures(const char* name, const char texture_names[6][TEXTURE_NAME_MAX_LENGTH], texture* t);
static void destroy_texture(texture* t);
static b8 process_texture_reference(const char* name, texture_type type, u16 array_size, i8 reference_diff, b8 auto_release, b8 skip_load, u32* out_texture_id);

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

    // Hashtable block is after array.
    void* hashtable_block = array_block + array_requirement;

    // Create a hashtable for texture lookups.
    hashtable_create(sizeof(texture_reference), typed_config->max_texture_count, hashtable_block, false, &state_ptr->registered_texture_table);

    // Fill the hashtable with invalid references to use as a default.
    texture_reference invalid_ref;
    invalid_ref.auto_release = false;
    invalid_ref.handle = INVALID_ID;  // Primary reason for needing default values.
    invalid_ref.reference_count = 0;
    hashtable_fill(&state_ptr->registered_texture_table, &invalid_ref);

    // Invalidate all textures in the array.
    u32 count = state_ptr->config.max_texture_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->registered_textures[i].id = INVALID_ID;
        state_ptr->registered_textures[i].generation = INVALID_ID;
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
            if (t->generation != INVALID_ID) {
                renderer_texture_destroy(t);
            }
        }

        destroy_default_textures(state_ptr);

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
    // NOTE: Increments reference count, or creates new entry.
    if (!process_texture_reference(name, TEXTURE_TYPE_2D, 1, 1, auto_release, false, &id)) {
        KERROR("texture_system_acquire failed to obtain a new texture id.");
        return 0;
    }

    return &state_ptr->registered_textures[id];
}

texture* texture_system_acquire_cube(const char* name, b8 auto_release) {
    // Return default texture, but warn about it since this should be returned via get_default_texture();
    // TODO: Check against other default texture names?
    if (strings_equali(name, DEFAULT_TEXTURE_NAME)) {
        KWARN("texture_system_acquire_cube called for default texture. Use texture_system_get_default_texture for texture 'default'.");
        return &state_ptr->default_texture;
    }

    u32 id = INVALID_ID;
    // NOTE: Increments reference count, or creates new entry.
    if (!process_texture_reference(name, TEXTURE_TYPE_CUBE, 1, 1, auto_release, false, &id)) {
        KERROR("texture_system_acquire_cube failed to obtain a new texture id.");
        return 0;
    }

    return &state_ptr->registered_textures[id];
}

texture* texture_system_acquire_writeable(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency) {
    return texture_system_acquire_writeable_arrayed(name, width, height, channel_count, has_transparency, TEXTURE_TYPE_2D, 1);
}

texture* texture_system_acquire_writeable_arrayed(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency, texture_type type, u16 array_size) {
    u32 id = INVALID_ID;
    // NOTE: Wrapped textures are never auto-released because it means that thier
    // resources are created and managed somewhere within the renderer internals.
    if (!process_texture_reference(name, type, 1, array_size, false, true, &id)) {
        KERROR("texture_system_acquire_writeable_arrayed failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];
    t->id = id;
    t->type = type;
    string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->array_size = array_size;
    t->generation = INVALID_ID;
    t->mip_levels = 1;
    t->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    t->flags |= TEXTURE_FLAG_IS_WRITEABLE;
    t->internal_data = 0;
    renderer_texture_create_writeable(t);
    return t;
}

void texture_system_release(const char* name) {
    // Ignore release requests for the default texture.
    // TODO: Check against other default texture names as well?
    if (strings_equali(name, DEFAULT_TEXTURE_NAME)) {
        return;
    }
    u32 id = INVALID_ID;
    // NOTE: Decrement the reference count.
    if (!process_texture_reference(name, TEXTURE_TYPE_2D, 1, -1, false, false, &id)) {
        KERROR("texture_system_release failed to release texture '%s' properly.", name);
    }
}

void texture_system_wrap_internal(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency, b8 is_writeable, b8 register_texture, void* internal_data, texture* out_texture) {
    u32 id = INVALID_ID;
    texture* t = 0;
    if (register_texture) {
        // NOTE: Wrapped textures are never auto-released because it means that thier
        // resources are created and managed somewhere within the renderer internals.
        if (!process_texture_reference(name, TEXTURE_TYPE_2D, 1, 1, false, true, &id)) {
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
    string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->generation = INVALID_ID;
    t->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    t->flags |= is_writeable ? TEXTURE_FLAG_IS_WRITEABLE : 0;
    t->flags |= TEXTURE_FLAG_IS_WRAPPED;
    t->internal_data = internal_data;
}

b8 texture_system_set_internal(texture* t, void* internal_data) {
    if (t) {
        t->internal_data = internal_data;
        t->generation++;
        return true;
    }
    return false;
}

b8 texture_system_resize(texture* t, u32 width, u32 height, b8 regenerate_internal_data) {
    if (t) {
        if (!(t->flags & TEXTURE_FLAG_IS_WRITEABLE)) {
            KWARN("texture_system_resize should not be called on textures that are not writeable.");
            return false;
        }
        t->width = width;
        t->height = height;
        // Only allow this for writeable textures that are not wrapped.
        // Wrapped textures can call texture_system_set_internal then call
        // this function to get the above parameter updates and a generation
        // update.
        if (!(t->flags & TEXTURE_FLAG_IS_WRAPPED) && regenerate_internal_data) {
            // Regenerate internals for the new size.
            renderer_texture_resize(t, width, height);
            return false;
        }
        t->generation++;
        return true;
    }
    return false;
}

b8 texture_system_write_data(texture* t, u32 offset, u32 size, void* data) {
    if (t) {
        renderer_texture_write_data(t, offset, size, data);
        return true;
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

static void create_default_texture(texture* t, u8* pixels, u32 tex_dimension, const char* name) {
    string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
    t->width = tex_dimension;
    t->height = tex_dimension;
    t->channel_count = 4;
    t->generation = INVALID_ID;
    t->flags = 0;
    t->type = TEXTURE_TYPE_2D;
    t->mip_levels = 1;
    renderer_texture_create(pixels, t);
    // Manually set the texture generation to invalid since this is a default texture.
    t->generation = INVALID_ID;
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

    u8* pixels = 0;
    u64 image_size = 0;
    for (u8 i = 0; i < 6; ++i) {
        if (!pixels) {
            t->width = tex_dimension;
            t->height = tex_dimension;
            t->channel_count = 4;
            t->flags = 0;
            t->generation = 0;
            t->mip_levels = 1;
            t->type = TEXTURE_TYPE_CUBE;
            // Take a copy of the name.
            string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);

            image_size = t->width * t->height * t->channel_count;
            // NOTE: no need for transparency in cube maps, so not checking for it.

            pixels = kallocate(sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
        }

        // Copy to the relevant portion of the array.
        kcopy_memory(pixels + image_size * i, cube_side_pixels, image_size);
    }

    // Acquire internal texture resources and upload to GPU.
    renderer_texture_create(pixels, t);

    kfree(pixels, sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
    pixels = 0;

    return true;
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
    u8 normal_pixels[16 * 16 * 4];  // w * h * channels
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
    u8 combined_pixels[16 * 16 * 4];  // w * h * channels
    kset_memory(combined_pixels, 255, sizeof(u8) * 16 * 16 * 4);

    // Each pixel.
    for (u64 row = 0; row < 16; ++row) {
        for (u64 col = 0; col < 16; ++col) {
            u64 index = (row * 16) + col;
            u64 index_bpp = index * channels;
            combined_pixels[index_bpp + 0] = 0;    // Default for metallic is black.
            combined_pixels[index_bpp + 1] = 128;  // Default for roughness is medium grey
            combined_pixels[index_bpp + 2] = 255;  // Default for AO is white.
        }
    }
    create_default_texture(&state->default_combined_texture, combined_pixels, 16, DEFAULT_COMBINED_TEXTURE_NAME);

    // Cube texture.
    KTRACE("Creating default cube texture...");
    create_default_cube_texture(&state->default_cube_texture, DEFAULT_CUBE_TEXTURE_NAME);

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
    }
}

static b8 load_cube_textures(const char* name, const char texture_names[6][TEXTURE_NAME_MAX_LENGTH], texture* t) {
    u8* pixels = 0;
    u64 image_size = 0;
    for (u8 i = 0; i < 6; ++i) {
        image_resource_params params;
        params.flip_y = false;

        resource img_resource;
        if (!resource_system_load(texture_names[i], RESOURCE_TYPE_IMAGE, &params, &img_resource)) {
            KERROR("load_cube_textures() - Failed to load image resource for texture '%s'", texture_names[i]);
            return false;
        }

        image_resource_data* resource_data = img_resource.data;
        if (!pixels) {
            t->width = resource_data->width;
            t->height = resource_data->height;
            t->channel_count = resource_data->channel_count;
            t->flags = 0;
            t->generation = 0;
            t->mip_levels = 1;
            // Take a copy of the name.
            string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);

            image_size = t->width * t->height * t->channel_count;
            // NOTE: no need for transparency in cube maps, so not checking for it.

            pixels = kallocate(sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
        } else {
            // Verify all textures are the same size.
            if (t->width != resource_data->width || t->height != resource_data->height || t->channel_count != resource_data->channel_count) {
                KERROR("load_cube_textures - All textures must be the same resolution and bit depth.");
                kfree(pixels, sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
                pixels = 0;
                return false;
            }
        }

        // Copy to the relevant portion of the array.
        kcopy_memory(pixels + image_size * i, resource_data->pixels, image_size);

        // Clean up data.
        resource_system_unload(&img_resource);
    }

    // Acquire internal texture resources and upload to GPU.
    renderer_texture_create(pixels, t);

    kfree(pixels, sizeof(u8) * image_size * 6, MEMORY_TAG_ARRAY);
    pixels = 0;

    return true;
}

static void texture_load_job_success(void* params) {
    texture_load_params* texture_params = (texture_load_params*)params;

    // This also handles the GPU upload. Can't be jobified until the renderer is multithreaded.
    image_resource_data* resource_data = (image_resource_data*)texture_params->image_resource.data;

    // Acquire internal texture resources and upload to GPU. Can't be jobified until the renderer is multithreaded.
    renderer_texture_create(resource_data->pixels, &texture_params->temp_texture);

    // Take a copy of the old texture.
    texture old = *texture_params->out_texture;

    // Assign the temp texture to the pointer.
    *texture_params->out_texture = texture_params->temp_texture;

    // Destroy the old texture.
    renderer_texture_destroy(&old);
    kzero_memory(&old, sizeof(texture));

    if (texture_params->current_generation == INVALID_ID) {
        texture_params->out_texture->generation = 0;
    } else {
        texture_params->out_texture->generation = texture_params->current_generation + 1;
    }

    KTRACE("Successfully loaded texture '%s'.", texture_params->resource_name);

    // Clean up data.
    resource_system_unload(&texture_params->image_resource);
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

    load_params->current_generation = load_params->out_texture->generation;
    load_params->out_texture->generation = INVALID_ID;
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
    string_ncopy(load_params->temp_texture.name, load_params->resource_name, TEXTURE_NAME_MAX_LENGTH);
    load_params->temp_texture.generation = INVALID_ID;
    load_params->temp_texture.flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;

    // NOTE: The load params are also used as the result data here, only the image_resource field is populated now.
    kcopy_memory(result_data, load_params, sizeof(texture_load_params));

    return result;
}

static b8 load_texture(const char* texture_name, texture* t) {
    // Kick off a texture loading job. Only handles loading from disk
    // to CPU. GPU upload is handled after completion of this job.
    texture_load_params params;
    params.resource_name = string_duplicate(texture_name);
    params.out_texture = t;
    params.image_resource = (resource){};
    params.current_generation = t->generation;
    params.temp_texture = (texture){};

    job_info job = job_create(texture_load_job_start, texture_load_job_success, texture_load_job_fail, &params, sizeof(texture_load_params), sizeof(texture_load_params));
    job_system_submit(job);
    return true;
}

static void destroy_texture(texture* t) {
    // Clean up backend resources.
    renderer_texture_destroy(t);

    kzero_memory(t->name, sizeof(char) * TEXTURE_NAME_MAX_LENGTH);
    kzero_memory(t, sizeof(texture));
    t->id = INVALID_ID;
    t->generation = INVALID_ID;
}

static b8 process_texture_reference(const char* name, texture_type type, u16 array_size, i8 reference_diff, b8 auto_release, b8 skip_load, u32* out_texture_id) {
    *out_texture_id = INVALID_ID;
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
                        texture* t = &state_ptr->registered_textures[ref.handle];
                        t->type = type;
                        t->array_size = array_size;
                        // Create new texture.
                        if (skip_load) {
                            // KTRACE("Load skipped for texture '%s'. This is expected behaviour.");
                        } else {
                            if (type == TEXTURE_TYPE_CUBE) {
                                char texture_names[6][TEXTURE_NAME_MAX_LENGTH];

                                // +X,-X,+Y,-Y,+Z,-Z in _cubemap_ space, which is LH y-down
                                string_format(texture_names[0], "%s_r", name);  // Right texture
                                string_format(texture_names[1], "%s_l", name);  // Left texture
                                string_format(texture_names[2], "%s_u", name);  // Up texture
                                string_format(texture_names[3], "%s_d", name);  // Down texture
                                string_format(texture_names[4], "%s_f", name);  // Front texture
                                string_format(texture_names[5], "%s_b", name);  // Back texture

                                if (!load_cube_textures(name, texture_names, t)) {
                                    *out_texture_id = INVALID_ID;
                                    KERROR("Failed to load cube texture '%s'.", name);
                                    return false;
                                }
                            } else if (type == TEXTURE_TYPE_2D) {
                                if (!load_texture(name, t)) {
                                    *out_texture_id = INVALID_ID;
                                    KERROR("Failed to load texture '%s'.", name);
                                    return false;
                                }
                            } else if (type == TEXTURE_TYPE_2D_ARRAY) {
                                // Acquire internal texture resources and upload to GPU. Can't be jobified until the renderer is multithreaded.
                                renderer_texture_create(0, t);
                            }
                            t->id = ref.handle;
                        }

                        // Make sure to hold onto the texture name.
                        string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
                        // KTRACE("Texture '%s' does not yet exist. Created, and ref_count is now %i.", name, ref.reference_count);
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
