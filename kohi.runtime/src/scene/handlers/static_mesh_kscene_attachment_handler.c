#include "static_mesh_kscene_attachment_handler.h"
#include "containers/kpool.h"
#include "core/engine.h"
#include "core/frame_data.h"
#include "defines.h"
#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "renderer/renderer_types.h"
#include "scene/kscene_attachment_registry.h"
#include "scene/kscene_attachment_types.h"
#include "strings/kname.h"
#include "systems/static_mesh_system.h"

typedef struct kscene_attachment_static_mesh {
    kscene_attachment base;
    u64 uniqueid;
    static_mesh_instance instance;
    kscene_attachment_state state;
    kname asset_name;
    kname package_name;
} kscene_attachment_static_mesh;

typedef struct kscene_attachment_static_mesh_handler_state {
    struct static_mesh_system_state* static_mesh_state;
} kscene_attachment_static_mesh_handler_state;

b8 static_mesh_create(struct kscene_attachment_handler* handler, const kscene_attachment_config* config, khandle* out_attachment) {
    if (!handler || !out_attachment) {
        return false;
    }

    u32 index = INVALID_ID;
    kscene_attachment_static_mesh* new_mesh = kpool_allocate(&handler->attachments, &index);
    if (!new_mesh) {
        return false;
    }

    *out_attachment = khandle_create(index);
    new_mesh->uniqueid = out_attachment->unique_id.uniqueid;

    new_mesh->base.name = config->name;
    new_mesh->base.type_name = config->type_name;
    return true;
}

void static_mesh_destroy(struct kscene_attachment_handler* handler, khandle* attachment) {
    if (handler && attachment && khandle_is_valid(*attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment->handle_index);
        if (mesh && khandle_is_pristine(*attachment, mesh->uniqueid)) {
            // Then do the destroy
            // TODO: Check if loaded, and unload first if needed.

            // Free the pool entry - TODO: move to registry?
            kpool_free_by_index(&handler->attachments, attachment->handle_index);

            // Invalidate the handle. - TODO: move to registry?
            khandle_invalidate(attachment);
        }
    }
}

b8 static_mesh_deserialize(struct kscene_attachment_handler* handler, khandle attachment, const char* source_string) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);

            // Parse and process configuration.
            kson_tree config_tree = {0};
            if (!kson_tree_from_string(source_string, &config_tree)) {
                KERROR("Failed to parse configuration for static mesh component. See logs for details.");
                kson_tree_cleanup(&config_tree);
                return false;
            }

            kson_object attachment_obj = config_tree.root;

            if (!kson_object_property_value_get_string_as_kname(&attachment_obj, "asset_name", &mesh->asset_name)) {
                KERROR("Failed to get 'asset_name' property for attachment '%s'.", mesh->base.name);
                return false;
            }

            // Package name. Optional.
            kson_object_property_value_get_string_as_kname(&attachment_obj, "package_name", &mesh->package_name);

            return true;
        }
    }

    return false;
}

const char* static_mesh_serialize(struct kscene_attachment_handler* handler, khandle attachment) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);

            kson_object attachment_obj = kson_object_create();

            // Asset name
            kname asset_name = mesh->asset_name ? mesh->asset_name : kname_create("default_static_mesh");
            if (!kson_object_value_add_kname_as_string(&attachment_obj, "asset_name", asset_name)) {
                KERROR("Failed to add 'asset_name' property for attachment '%s'.", kname_string_get(mesh->base.name));
                return 0;
            }

            // Package name, if it exists.
            if (mesh->package_name) {
                if (!kson_object_value_add_kname_as_string(&attachment_obj, "package_name", mesh->package_name)) {
                    KERROR("Failed to add 'package_name' property for attachment '%s'.", kname_string_get(mesh->base.name));
                    return 0;
                }
            }

            kson_tree temp_tree = {
                .root = attachment_obj};

            const char* out_str = kson_tree_to_string(&temp_tree);

            kson_tree_cleanup(&temp_tree);

            return out_str;
        }
    }

    return 0;
}

b8 static_mesh_initialize(struct kscene_attachment_handler* handler, khandle attachment) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
            if (mesh) {
                // NOTE: No specific initialization is required here. Just return success for
                // now and maybe eliminate this function later.
                return true;
            }
        }
    }

    return false;
}

b8 static_mesh_load(struct kscene_attachment_handler* handler, khandle attachment) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);

            // Acquire a static mesh instance.
            if (!static_mesh_system_instance_acquire(engine_systems_get()->static_mesh_system, mesh->asset_name, mesh->package_name, &mesh->instance)) {
                KERROR("%s Failed to create new static mesh.", __FUNCTION__);
                return false;
            }

            return true;
        }
    }

    return false;
}

void static_mesh_unload(struct kscene_attachment_handler* handler, khandle attachment) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);

            // Release the static mesh instance.
            static_mesh_system_instance_release(engine_systems_get()->static_mesh_system, &mesh->instance);
        }
    }
}

b8 static_mesh_update(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
            if (mesh) {
                // NOTE: No specific update is required here. Just return success for
                // now and maybe eliminate this function later.
                return true;
            }
        }
    }

    return false;
}

b8 static_mesh_render_frame_prepare(struct kscene_attachment_handler* handler, khandle attachment, const struct frame_data* p_frame_data) {
    if (handler && khandle_is_valid(attachment)) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
            if (mesh) {
                // NOTE: No specific frame prepare is required here. Just return success for
                // now and maybe eliminate this function later.
                return true;
            }
        }
    }

    return false;
}

b8 static_mesh_generate_render_data(struct kscene_attachment_handler* handler, khandle attachment, mat4 node_model, const struct frame_data* p_frame_data, u32* render_data_count, struct geometry_render_data** out_render_datas) {
    if (handler && khandle_is_valid(attachment) && p_frame_data && render_data_count && out_render_datas) {
        kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
        if (mesh && khandle_is_pristine(attachment, mesh->uniqueid)) {

            kscene_attachment_static_mesh* mesh = kpool_get_by_index(&handler->attachments, attachment.handle_index);
            if (mesh) {

                // One render data per submesh.
                const kresource_static_mesh* resource = mesh->instance.mesh_resource;
                if (!resource || resource->base.state < KRESOURCE_STATE_LOADED) {
                    *render_data_count = 0;
                    *out_render_datas = 0;
                    // Not really an error, just nothing is ready to render yet.
                    return true;
                }

                u32 submesh_count = resource->submesh_count;
                if (submesh_count) {

                    // Use the frame allocator for this so freeing isn't required after.
                    *out_render_datas = p_frame_data->allocator.allocate(sizeof(geometry_render_data) * submesh_count);
                    *render_data_count = submesh_count;

                    // Determine if the winding needs to be inverted (i.e. negative scale).
                    f32 determinant = mat4_determinant(node_model);
                    b8 winding_inverted = determinant < 0;

                    // Fill out geometry render datas
                    for (u32 i = 0; i < submesh_count; ++i) {
                        static_mesh_submesh* submesh = &resource->submeshes[i];
                        kgeometry* g = &submesh->geometry;

                        geometry_render_data* data = out_render_datas[i];
                        data->model = node_model;
                        data->material = mesh->instance.material_instances[i];
                        data->vertex_count = g->vertex_count;
                        data->vertex_buffer_offset = g->vertex_buffer_offset;
                        data->vertex_element_size = g->vertex_element_size;
                        data->index_count = g->index_count;
                        data->index_buffer_offset = g->index_buffer_offset;
                        data->index_element_size = g->index_element_size;
                        data->unique_id = 0; // m->id.uniqueid; FIXME: needed for per-pixel selection
                        data->winding_inverted = winding_inverted;
                    }
                }

                return true;
            }
        }
    }

    return false;
}

b8 static_mesh_kscene_attachment_handler_create() {

    kscene_attachment_handler handler = {
        .type_name = kname_create(KSCENE_ATTACHMENT_TYPE_NAME_STATIC_MESH),
        .pool_element_max = 1024,
        .pool_element_size = sizeof(kscene_attachment_static_mesh),
        .create = static_mesh_create,
        .deserialize = static_mesh_deserialize,
        .serialize = static_mesh_serialize,
        .initialize = static_mesh_initialize,
        .load = static_mesh_load,
        .unload = static_mesh_unload,
        .update = static_mesh_update,
        .render_frame_prepare = static_mesh_render_frame_prepare,
        .generate_render_data = static_mesh_generate_render_data};

    // Setup internal state.
    handler.internal_state = kallocate(sizeof(kscene_attachment_static_mesh_handler_state), MEMORY_TAG_SCENE);
    kscene_attachment_static_mesh_handler_state* state = handler.internal_state;
    state->static_mesh_state = engine_systems_get()->static_mesh_system;

    // Register it.
    return kscene_attachment_type_register_type_handler(engine_systems_get()->scene_attachment_type_registry, handler);
}
