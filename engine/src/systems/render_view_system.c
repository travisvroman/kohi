#include "render_view_system.h"

#include "containers/hashtable.h"
#include "core/logger.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "renderer/renderer_frontend.h"

// TODO: temporary - make factory and register instead.
#include "renderer/views/render_view_world.h"
#include "renderer/views/render_view_ui.h"
#include "renderer/views/render_view_skybox.h"

typedef struct render_view_system_state {
    hashtable lookup;
    void* table_block;
    u32 max_view_count;
    render_view* registered_views;
} render_view_system_state;

static render_view_system_state* state_ptr = 0;

b8 render_view_system_initialize(u64* memory_requirement, void* state, render_view_system_config config) {
    if (config.max_view_count == 0) {
        KFATAL("render_view_system_initialize - config.max_view_count must be > 0.");
        return false;
    }

    // Block of memory will contain state structure, then block for hashtable, then block for array.
    u64 struct_requirement = sizeof(render_view_system_state);
    u64 hashtable_requirement = sizeof(u16) * config.max_view_count;
    u64 array_requirement = sizeof(render_view) * config.max_view_count;
    *memory_requirement = struct_requirement + hashtable_requirement + array_requirement;

     if (!state) {
        return true;
    }

    state_ptr = state;
    state_ptr->max_view_count = config.max_view_count;

    // The hashtable block is after the state. Already allocated, so just set the pointer.
    u64 addr = (u64)state_ptr;
    state_ptr->table_block = (void*)((u64)addr + struct_requirement);

    // Array block is after hashtable.
    state_ptr->registered_views = (void*)((u64)state_ptr->table_block + hashtable_requirement);

    // Create a hashtable for view lookups.
    hashtable_create(sizeof(u16), state_ptr->max_view_count, state_ptr->table_block, false, &state_ptr->lookup);
    // Fill the hashtable with invalid ids
    u16 invalid_id = INVALID_ID_U16;
    hashtable_fill(&state_ptr->lookup, &invalid_id);

    // Fill the array with invalid entries.
    for (u32 i = 0; i < state_ptr->max_view_count; ++i) {
        state_ptr->registered_views[i].id = INVALID_ID_U16;
    }

    return true;
}

void render_view_system_shutdown(void* state) {
    state_ptr = 0;
}

b8 render_view_system_create(const render_view_config* config) {
    if (!config) {
        KERROR("render_view_system_create requires a pointer to a valid config.");
        return false;
    }

    if(!config->name || string_length(config->name) < 1) {
        KERROR("render_view_system_create: name is required");
        return false;
    }

    if (config->pass_count < 1) {
        KERROR("render_view_system_create - Config must have at least one renderpass.");
        return false;
    }

    u16 id = INVALID_ID_U16;
    // Make sure there is not already an entry with this name already registered.
    hashtable_get(&state_ptr->lookup, config->name, &id);
    if (id != INVALID_ID_U16) {
        KERROR("render_view_system_create - A view named '%s' already exists. A new one will not be created.", config->name);
        return false;
    }

    // Find a new id.
    for (u32 i = 0; i < state_ptr->max_view_count; ++i) {
        if (state_ptr->registered_views[i].id == INVALID_ID_U16) {
            id = i;
            break;
        }
    }

    // Make sure a valid entry was found.
    if (id == INVALID_ID_U16) {
        KERROR("render_view_system_create - No available space for a new view. Change system config to account for more.");
        return false;
    }

    render_view* view = &state_ptr->registered_views[id];
    view->id = id;
    view->type = config->type;
    // TODO: Leaking the name, create a destroy method and kill this.
    view->name = string_duplicate(config->name);
    view->custom_shader_name = config->custom_shader_name;
    view->renderpass_count = config->pass_count;
    view->passes = kallocate(sizeof(renderpass*) * view->renderpass_count, MEMORY_TAG_ARRAY);

    for (u32 i = 0; i < view->renderpass_count; ++i) {
        view->passes[i] = renderer_renderpass_get(config->passes[i].name);
        if (!view->passes[i]) {
            KFATAL("render_view_system_create - renderpass not found: '%s'.", config->passes[i].name);
            return false;
        }
    }

    // TODO: Assign these function pointers to known functions based on the view type.
    // TODO: Factory pattern (with register, etc. for each type)?
    if (config->type == RENDERER_VIEW_KNOWN_TYPE_WORLD) {
        view->on_build_packet = render_view_world_on_build_packet;  // For building the packet
        view->on_render = render_view_world_on_render;              // For rendering the packet
        view->on_create = render_view_world_on_create;
        view->on_destroy = render_view_world_on_destroy;
        view->on_resize = render_view_world_on_resize;
    } else if (config->type == RENDERER_VIEW_KNOWN_TYPE_UI) {
        view->on_build_packet = render_view_ui_on_build_packet;  // For building the packet
        view->on_render = render_view_ui_on_render;              // For rendering the packet
        view->on_create = render_view_ui_on_create;
        view->on_destroy = render_view_ui_on_destroy;
        view->on_resize = render_view_ui_on_resize;
    } else if (config->type == RENDERER_VIEW_KNOWN_TYPE_SKYBOX) {
        view->on_build_packet = render_view_skybox_on_build_packet;  // For building the packet
        view->on_render = render_view_skybox_on_render;              // For rendering the packet
        view->on_create = render_view_skybox_on_create;
        view->on_destroy = render_view_skybox_on_destroy;
        view->on_resize = render_view_skybox_on_resize;
    }

    // Call the on create
    if (!view->on_create(view)) {
        KERROR("Failed to create view.");
        kfree(view->passes, sizeof(renderpass*) * view->renderpass_count, MEMORY_TAG_ARRAY);
        kzero_memory(&state_ptr->registered_views[id], sizeof(render_view));
        return false;
    }

    // Update the hashtable entry.
    hashtable_set(&state_ptr->lookup, config->name, &id);

    return true;
}

void render_view_system_on_window_resize(u32 width, u32 height) {
    // Send to all views
    for (u32 i = 0; i < state_ptr->max_view_count; ++i) {
        if (state_ptr->registered_views[i].id != INVALID_ID_U16) {
            state_ptr->registered_views[i].on_resize(&state_ptr->registered_views[i], width, height);
        }
    }
}

render_view* render_view_system_get(const char* name) {
    if (state_ptr) {
        u16 id = INVALID_ID_U16;
        hashtable_get(&state_ptr->lookup, name, &id);
        if (id != INVALID_ID_U16) {
            return &state_ptr->registered_views[id];
        }
    }
    return 0;
}

b8 render_view_system_build_packet(const render_view* view, void* data, struct render_view_packet* out_packet){
    if (view && out_packet) {
        return view->on_build_packet(view, data, out_packet);
    }

    KERROR("render_view_system_build_packet requires valid pointers to a view and a packet.");
    return false;
}

b8 render_view_system_on_render(const render_view* view, const render_view_packet* packet, u64 frame_number, u64 render_target_index) {
    if (view && packet) {
        return view->on_render(view, packet, frame_number, render_target_index);
    }

    KERROR("render_view_system_on_render requires a valid pointer to a data.");
    return false;
}
