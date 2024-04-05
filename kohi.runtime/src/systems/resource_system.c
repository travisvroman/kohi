#include "resource_system.h"

#include "debug/kassert.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "logger.h"

// Known resource loaders.
#include "resources/loaders/binary_loader.h"
#include "resources/loaders/bitmap_font_loader.h"
#include "resources/loaders/image_loader.h"
#include "resources/loaders/material_loader.h"
#include "resources/loaders/mesh_loader.h"
#include "resources/loaders/scene_loader.h"
#include "resources/loaders/shader_loader.h"
#include "resources/loaders/system_font_loader.h"
#include "resources/loaders/terrain_loader.h"
#include "resources/loaders/text_loader.h"
#include "resources/resource_types.h"

typedef struct resource_system_state {
    resource_system_config config;
    resource_loader *registered_loaders;
} resource_system_state;

static resource_system_state *state_ptr = 0;

static b8 load(const char *name, resource_loader *loader, void *params,
               resource *out_resource);

b8 resource_system_initialize(u64 *memory_requirement, void *state,
                              void *config) {
    resource_system_config *typed_config = (resource_system_config *)config;
    if (typed_config->max_loader_count == 0) {
        KFATAL(
            "resource_system_initialize failed because "
            "config.max_loader_count==0.");
        return false;
    }

    *memory_requirement =
        sizeof(resource_system_state) +
        (sizeof(resource_loader) * typed_config->max_loader_count);

    if (!state) {
        return true;
    }

    state_ptr = state;
    state_ptr->config = *typed_config;

    void *array_block = state + sizeof(resource_system_state);
    state_ptr->registered_loaders = array_block;

    // Invalidate all loaders
    u32 count = typed_config->max_loader_count;
    for (u32 i = 0; i < count; ++i) {
        state_ptr->registered_loaders[i].id = INVALID_ID;
    }

    // NOTE: Auto-register known loader types here.
    resource_system_loader_register(text_resource_loader_create());
    resource_system_loader_register(binary_resource_loader_create());
    resource_system_loader_register(image_resource_loader_create());
    resource_system_loader_register(material_resource_loader_create());
    resource_system_loader_register(shader_resource_loader_create());
    resource_system_loader_register(mesh_resource_loader_create());
    resource_system_loader_register(bitmap_font_resource_loader_create());
    resource_system_loader_register(system_font_resource_loader_create());
    resource_system_loader_register(terrain_resource_loader_create());
    resource_system_loader_register(scene_resource_loader_create());

    KINFO("Resource system initialized with base path '%s'.",
          typed_config->asset_base_path);

    return true;
}

void resource_system_shutdown(void *state) {
    if (state_ptr) {
        state_ptr = 0;
    }
}

b8 resource_system_loader_register(resource_loader loader) {
    if (state_ptr) {
        u32 count = state_ptr->config.max_loader_count;
        // Ensure no loaders for the given type already exist
        for (u32 i = 0; i < count; ++i) {
            resource_loader *l = &state_ptr->registered_loaders[i];
            if (l->id != INVALID_ID) {
                if (l->type == loader.type) {
                    KERROR(
                        "resource_system_register_loader - Loader of type %d already "
                        "exists and will not be registered.",
                        loader.type);
                    return false;
                } else if (loader.custom_type &&
                           string_length(loader.custom_type) > 0 &&
                           strings_equali(l->custom_type, loader.custom_type)) {
                    KERROR(
                        "resource_system_register_loader - Loader of custom type %s "
                        "already exists and will not be registered.",
                        loader.custom_type);
                    return false;
                }
            }
        }
        for (u32 i = 0; i < count; ++i) {
            if (state_ptr->registered_loaders[i].id == INVALID_ID) {
                state_ptr->registered_loaders[i] = loader;
                state_ptr->registered_loaders[i].id = i;
                KTRACE("Loader registered.");
                return true;
            }
        }
    }

    return false;
}

b8 resource_system_load(const char *name, resource_type type, void *params,
                        resource *out_resource) {
    if (state_ptr && type != RESOURCE_TYPE_CUSTOM) {
        // Select loader.
        u32 count = state_ptr->config.max_loader_count;
        for (u32 i = 0; i < count; ++i) {
            resource_loader *l = &state_ptr->registered_loaders[i];
            if (l->id != INVALID_ID && l->type == type) {
                return load(name, l, params, out_resource);
            }
        }
    }

    out_resource->loader_id = INVALID_ID;
    KERROR("resource_system_load - No loader for type %d was found.", type);
    return false;
}

const char *resource_system_base_path_for_type(resource_type type) {
    if (state_ptr && type != RESOURCE_TYPE_CUSTOM) {
        // Select loader.
        u32 count = state_ptr->config.max_loader_count;
        for (u32 i = 0; i < count; ++i) {
            resource_loader *l = &state_ptr->registered_loaders[i];
            if (l->id != INVALID_ID && l->type == type) {
                u32 type_length = string_length(l->type_path);
                u32 base_length = string_length(state_ptr->config.asset_base_path);
                u32 total_length = type_length + base_length + 3;
                char *combined_path = kallocate(sizeof(char) * total_length, MEMORY_TAG_STRING);
                string_format(combined_path, "%s/%s/", state_ptr->config.asset_base_path, l->type_path);
                return combined_path;
            }
        }
        KERROR("Attempted to query for base asset path for unrecognized type. Null will be returned.");
        return 0;
    }
    return 0;
}

b8 resource_system_load_custom(const char *name, const char *custom_type,
                               void *params, resource *out_resource) {
    if (state_ptr && custom_type && string_length(custom_type) > 0) {
        // Select loader.
        u32 count = state_ptr->config.max_loader_count;
        for (u32 i = 0; i < count; ++i) {
            resource_loader *l = &state_ptr->registered_loaders[i];
            if (l->id != INVALID_ID && l->type == RESOURCE_TYPE_CUSTOM &&
                strings_equali(l->custom_type, custom_type)) {
                return load(name, l, params, out_resource);
            }
        }
    }

    out_resource->loader_id = INVALID_ID;
    KERROR("resource_system_load_custom - No loader for type %s was found.",
           custom_type);
    return false;
}

b8 resource_system_write(resource_type type, resource *r) {
    if (!state_ptr || !r) {
        KERROR("resource_system_write requires state to be initialized and a valid pointer to a resource to be written.");
        return false;
    }

    KASSERT_MSG(false, "resource_system_write not implemented.");
    // TODO: handle this differently. Maybe a resource_writer?
    /* // Select loader.
    u32 count = state_ptr->config.max_loader_count;
    for (u32 i = 0; i < count; ++i) {
        resource_loader *l = &state_ptr->registered_loaders[i];
        if (l->id != INVALID_ID && l->type == type && l->write) {
            return l->write(l, r);
        }
    } */

    KERROR("No resource writer available for type %u", type);
    return false;
}

void resource_system_unload(resource *resource) {
    if (state_ptr && resource) {
        if (resource->loader_id != INVALID_ID) {
            resource_loader *l = &state_ptr->registered_loaders[resource->loader_id];
            if (l->id != INVALID_ID && l->unload) {
                l->unload(l, resource);
            }
        }
    }
}

const char *resource_system_base_path(void) {
    if (state_ptr) {
        return state_ptr->config.asset_base_path;
    }

    KERROR(
        "resource_system_base_path called before initialization, returning "
        "empty string.");
    return "";
}

static b8 load(const char *name, resource_loader *loader, void *params,
               resource *out_resource) {
    if (!name || !loader || !loader->load || !out_resource) {
        if (out_resource) {
            out_resource->loader_id = INVALID_ID;
        }
        return false;
    }

    out_resource->loader_id = loader->id;
    return loader->load(loader, name, params, out_resource);
}
