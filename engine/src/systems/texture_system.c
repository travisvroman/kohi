#include "texture_system.h"

#include "core/logger.h"
#include "core/kstring.h"
#include "core/kmemory.h"
#include "containers/hashtable.h"

#include "renderer/renderer_frontend.h"

#include "systems/resource_system.h"

typedef struct texture_system_state {
    texture_system_config config;
    texture default_texture;
    texture default_diffuse_texture;
    texture default_specular_texture;
    texture default_normal_texture;

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

static texture_system_state* state_ptr = 0;

b8 create_default_textures(texture_system_state* state);
void destroy_default_textures(texture_system_state* state);
b8 load_texture(const char* texture_name, texture* t);
void destroy_texture(texture* t);
b8 process_texture_reference(const char* name, i8 reference_diff, b8 auto_release, b8 skip_load, u32* out_texture_id);

b8 texture_system_initialize(u64* memory_requirement, void* state, texture_system_config config) {
    if (config.max_texture_count == 0) {
        KFATAL("texture_system_initialize - config.max_texture_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for array, then block for hashtable.
    u64 struct_requirement = sizeof(texture_system_state);
    u64 array_requirement = sizeof(texture) * config.max_texture_count;
    u64 hashtable_requirement = sizeof(texture_reference) * config.max_texture_count;
    *memory_requirement = struct_requirement + array_requirement + hashtable_requirement;

    if (!state) {
        return true;
    }

    state_ptr = state;
    state_ptr->config = config;

    // The array block is after the state. Already allocated, so just set the pointer.
    void* array_block = state + struct_requirement;
    state_ptr->registered_textures = array_block;

    // Hashtable block is after array.
    void* hashtable_block = array_block + array_requirement;

    // Create a hashtable for texture lookups.
    hashtable_create(sizeof(texture_reference), config.max_texture_count, hashtable_block, false, &state_ptr->registered_texture_table);

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

    u32 id = INVALID_ID;
    // NOTE: Increments reference count, or creates new entry.
    if (!process_texture_reference(name, 1, auto_release, false, &id)) {
        KERROR("texture_system_acquire failed to obtain a new texture id.");
        return 0;
    }

    return &state_ptr->registered_textures[id];
}

texture* texture_system_aquire_writeable(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency) {
    u32 id = INVALID_ID;
    // NOTE: Wrapped textures are never auto-released because it means that thier
    // resources are created and managed somewhere within the renderer internals.
    if (!process_texture_reference(name, 1, false, true, &id)) {
        KERROR("texture_system_aquire_writeable failed to obtain a new texture id.");
        return 0;
    }

    texture* t = &state_ptr->registered_textures[id];
    t->id = id;
    string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->generation = INVALID_ID;
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
    if (!process_texture_reference(name, -1, false, false, &id)) {
        KERROR("texture_system_release failed to release texture '%s' properly.", name);
    }
}

texture* texture_system_wrap_internal(const char* name, u32 width, u32 height, u8 channel_count, b8 has_transparency, b8 is_writeable, b8 register_texture, void* internal_data) {
    u32 id = INVALID_ID;
    texture* t = 0;
    if (register_texture) {
        // NOTE: Wrapped textures are never auto-released because it means that thier
        // resources are created and managed somewhere within the renderer internals.
        if (!process_texture_reference(name, 1, false, true, &id)) {
            KERROR("texture_system_wrap_internal failed to obtain a new texture id.");
            return 0;
        }
        t = &state_ptr->registered_textures[id];
    } else {
        t = kallocate(sizeof(texture), MEMORY_TAG_TEXTURE);
        // KTRACE("texture_system_wrap_internal created texture '%s', but not registering, resulting in an allocation. It is up to the caller to free this memory.", name);
    }

    t->id = id;
    string_ncopy(t->name, name, TEXTURE_NAME_MAX_LENGTH);
    t->width = width;
    t->height = height;
    t->channel_count = channel_count;
    t->generation = INVALID_ID;
    t->flags |= has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;
    t->flags |= is_writeable ? TEXTURE_FLAG_IS_WRITEABLE : 0;
    t->flags |= TEXTURE_FLAG_IS_WRAPPED;
    t->internal_data = internal_data;
    return t;
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

#define RETURN_TEXT_PTR_OR_NULL(texture, func_name)                                              \
    if (state_ptr) {                                                                             \
        return &texture;                                                                         \
    }                                                                                            \
    KERROR("%s called before texture system initialization! Null pointer returned.", func_name); \
    return 0;

texture* texture_system_get_default_texture() {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_texture, "texture_system_get_default_texture");
}

texture* texture_system_get_default_diffuse_texture() {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_diffuse_texture, "texture_system_get_default_diffuse_texture");
}

texture* texture_system_get_default_specular_texture() {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_specular_texture, "texture_system_get_default_specular_texture");
}

texture* texture_system_get_default_normal_texture() {
    RETURN_TEXT_PTR_OR_NULL(state_ptr->default_normal_texture, "texture_system_get_default_normal_texture");
}

b8 create_default_textures(texture_system_state* state) {
    // NOTE: Create default texture, a 256x256 blue/white checkerboard pattern.
    // This is done in code to eliminate asset dependencies.
    // KTRACE("Creating default texture...");
    const u32 tex_dimension = 256;
    const u32 channels = 4;
    const u32 pixel_count = tex_dimension * tex_dimension;
    u8 pixels[262144];  // pixel_count * channels
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

    string_ncopy(state->default_texture.name, DEFAULT_TEXTURE_NAME, TEXTURE_NAME_MAX_LENGTH);
    state->default_texture.width = tex_dimension;
    state->default_texture.height = tex_dimension;
    state->default_texture.channel_count = 4;
    state->default_texture.generation = INVALID_ID;
    state->default_texture.flags = 0;
    renderer_texture_create(pixels, &state->default_texture);
    // Manually set the texture generation to invalid since this is a default texture.
    state->default_texture.generation = INVALID_ID;

    // Diffuse texture.
    // KTRACE("Creating default diffuse texture...");
    u8 diff_pixels[16 * 16 * 4];
    // Default diffuse map is all white.
    kset_memory(diff_pixels, 255, sizeof(u8) * 16 * 16 * 4);
    string_ncopy(state->default_diffuse_texture.name, DEFAULT_DIFFUSE_TEXTURE_NAME, TEXTURE_NAME_MAX_LENGTH);
    state->default_diffuse_texture.width = 16;
    state->default_diffuse_texture.height = 16;
    state->default_diffuse_texture.channel_count = 4;
    state->default_diffuse_texture.generation = INVALID_ID;
    state->default_diffuse_texture.flags = 0;
    renderer_texture_create(diff_pixels, &state->default_diffuse_texture);
    // Manually set the texture generation to invalid since this is a default texture.
    state->default_diffuse_texture.generation = INVALID_ID;

    // Specular texture.
    // KTRACE("Creating default specular texture...");
    u8 spec_pixels[16 * 16 * 4];
    // Default spec map is black (no specular)
    kset_memory(spec_pixels, 0, sizeof(u8) * 16 * 16 * 4);
    string_ncopy(state->default_specular_texture.name, DEFAULT_SPECULAR_TEXTURE_NAME, TEXTURE_NAME_MAX_LENGTH);
    state->default_specular_texture.width = 16;
    state->default_specular_texture.height = 16;
    state->default_specular_texture.channel_count = 4;
    state->default_specular_texture.generation = INVALID_ID;
    state->default_specular_texture.flags = 0;
    renderer_texture_create(spec_pixels, &state->default_specular_texture);
    // Manually set the texture generation to invalid since this is a default texture.
    state->default_specular_texture.generation = INVALID_ID;

    // Normal texture.
    // KTRACE("Creating default normal texture...");
    u8 normal_pixels[16 * 16 * 4];  // w * h * channels
    kset_memory(normal_pixels, 0, sizeof(u8) * 16 * 16 * 4);

    // Each pixel.
    for (u64 row = 0; row < 16; ++row) {
        for (u64 col = 0; col < 16; ++col) {
            u64 index = (row * 16) + col;
            u64 index_bpp = index * channels;
            // Set blue, z-axis by default and alpha.
            normal_pixels[index_bpp + 0] = 128;
            normal_pixels[index_bpp + 1] = 128;
            normal_pixels[index_bpp + 2] = 255;
            normal_pixels[index_bpp + 3] = 255;
        }
    }

    string_ncopy(state->default_normal_texture.name, DEFAULT_NORMAL_TEXTURE_NAME, TEXTURE_NAME_MAX_LENGTH);
    state->default_normal_texture.width = 16;
    state->default_normal_texture.height = 16;
    state->default_normal_texture.channel_count = 4;
    state->default_normal_texture.generation = INVALID_ID;
    state->default_normal_texture.flags = 0;
    renderer_texture_create(normal_pixels, &state->default_normal_texture);
    // Manually set the texture generation to invalid since this is a default texture.
    state->default_normal_texture.generation = INVALID_ID;

    return true;
}

void destroy_default_textures(texture_system_state* state) {
    if (state) {
        destroy_texture(&state->default_texture);
        destroy_texture(&state->default_diffuse_texture);
        destroy_texture(&state->default_specular_texture);
        destroy_texture(&state->default_normal_texture);
    }
}

b8 load_texture(const char* texture_name, texture* t) {
    resource img_resource;
    if (!resource_system_load(texture_name, RESOURCE_TYPE_IMAGE, &img_resource)) {
        KERROR("Failed to load image resource for texture '%s'", texture_name);
        return false;
    }

    image_resource_data* resource_data = img_resource.data;

    // Use a temporary texture to load into.
    texture temp_texture;
    temp_texture.width = resource_data->width;
    temp_texture.height = resource_data->height;
    temp_texture.channel_count = resource_data->channel_count;

    u32 current_generation = t->generation;
    t->generation = INVALID_ID;

    u64 total_size = temp_texture.width * temp_texture.height * temp_texture.channel_count;
    // Check for transparency
    b32 has_transparency = false;
    for (u64 i = 0; i < total_size; i += temp_texture.channel_count) {
        u8 a = resource_data->pixels[i + 3];
        if (a < 255) {
            has_transparency = true;
            break;
        }
    }

    // Take a copy of the name.
    string_ncopy(temp_texture.name, texture_name, TEXTURE_NAME_MAX_LENGTH);
    temp_texture.generation = INVALID_ID;
    temp_texture.flags = has_transparency ? TEXTURE_FLAG_HAS_TRANSPARENCY : 0;

    // Acquire internal texture resources and upload to GPU.
    renderer_texture_create(resource_data->pixels, &temp_texture);

    // Take a copy of the old texture.
    texture old = *t;

    // Assign the temp texture to the pointer.
    *t = temp_texture;

    // Destroy the old texture.
    renderer_texture_destroy(&old);

    if (current_generation == INVALID_ID) {
        t->generation = 0;
    } else {
        t->generation = current_generation + 1;
    }

    // Clean up data.
    resource_system_unload(&img_resource);
    return true;
}

void destroy_texture(texture* t) {
    // Clean up backend resources.
    renderer_texture_destroy(t);

    kzero_memory(t->name, sizeof(char) * TEXTURE_NAME_MAX_LENGTH);
    kzero_memory(t, sizeof(texture));
    t->id = INVALID_ID;
    t->generation = INVALID_ID;
}

b8 process_texture_reference(const char* name, i8 reference_diff, b8 auto_release, b8 skip_load, u32* out_texture_id) {
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
                        // Create new texture.
                        if (skip_load) {
                            // KTRACE("Load skipped for texture '%s'. This is expected behaviour.");
                        } else {
                            if (!load_texture(name, t)) {
                                *out_texture_id = INVALID_ID;
                                KERROR("Failed to load texture '%s'.", name);
                                return false;
                            }
                            t->id = ref.handle;
                        }
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