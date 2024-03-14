#include "scene_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "math/kmath.h"
#include "math/transform.h"
#include "platform/filesystem.h"
#include "resources/loaders/loader_utils.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"
#include "systems/xform_system.h"

#define SHADOW_DISTANCE_DEFAULT 200.0f
#define SHADOW_FADE_DISTANCE_DEFAULT 25.0f
#define SHADOW_SPLIT_MULT_DEFAULT 0.95f;

static b8 scene_loader_load(struct resource_loader* self, const char* name, void* params, resource* out_resource) {
    if (!self || !name || !out_resource) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, name, ".kss");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
        KERROR("scene_loader_load - unable to open simple scene file for reading: '%s'.", full_file_path);
        return false;
    }

    out_resource->full_path = string_duplicate(full_file_path);

    scene_config* resource_data = kallocate(sizeof(scene_config), MEMORY_TAG_RESOURCE);
    kzero_memory(resource_data, sizeof(scene_config));

    // HACK: temporarily construct a scene hierarchy, will read from file later.

    resource_data->name = "test_scene2";
    resource_data->description = "A hardcoded test scene.";

    resource_data->nodes = darray_create(scene_node_config);

    // sponza
    scene_node_config sponza = {0};
    sponza.name = "sponza";

    sponza.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    sponza.xform->scale = vec3_create(0.01f, 0.01f, 0.01f);
    sponza.xform->position = vec3_create(0, -1, 0);
    sponza.xform->rotation = quat_identity();

    sponza.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config sponza_mesh_attachment = {0};
    sponza_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    sponza_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* typed_mesh_attachment = sponza_mesh_attachment.attachment_data;
    typed_mesh_attachment->resource_name = "sponza";
    darray_push(sponza.attachments, sponza_mesh_attachment);

    // Create children.
    sponza.children = darray_create(scene_node_config);

    // Tree, a child of sponza
    scene_node_config tree = {0};
    tree.name = "tree";

    tree.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    // Large scale/position to compensate for small scale of parent.
    tree.xform->scale = vec3_create(200.0f, 200.0f, 200.0f);
    tree.xform->position = vec3_create(700.4f, 80.0f, 1400.0f);
    tree.xform->rotation = quat_identity();

    tree.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config tree_mesh_attachment = {0};
    tree_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    tree_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* typed_tree_mesh_attachment = tree_mesh_attachment.attachment_data;
    typed_tree_mesh_attachment->resource_name = "Tree";
    darray_push(tree.attachments, tree_mesh_attachment);

    darray_push(sponza.children, tree);

    // Add to global nodes array.
    darray_push(resource_data->nodes, sponza);

    // falcon
    scene_node_config falcon = {0};
    falcon.name = "falcon";

    falcon.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    falcon.xform->scale = vec3_create(0.35f, 0.35f, 0.35f);
    falcon.xform->position = vec3_create(9.4f, 0.8f, 14.0f);
    falcon.xform->rotation = quat_identity();

    falcon.attachments = darray_create(scene_node_attachment_config);

    scene_node_attachment_config falcon_mesh_attachment = {0};
    falcon_mesh_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_STATIC_MESH;
    falcon_mesh_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_static_mesh), MEMORY_TAG_SCENE);
    scene_node_attachment_static_mesh* falcon_typed_mesh_attachment = falcon_mesh_attachment.attachment_data;
    falcon_typed_mesh_attachment->resource_name = "falcon";
    darray_push(falcon.attachments, falcon_mesh_attachment);

    scene_node_attachment_config falcon_red_light_attachment = {0};
    falcon_red_light_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_POINT_LIGHT;
    falcon_red_light_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_point_light), MEMORY_TAG_SCENE);
    scene_node_attachment_point_light* falcon_red_light_typed_attachment = falcon_red_light_attachment.attachment_data;
    falcon_red_light_typed_attachment->colour = vec4_create(100.0f, 0.0f, 0.0f, 1.0f);
    falcon_red_light_typed_attachment->constant_f = 1.0f;
    falcon_red_light_typed_attachment->linear = 0.35f;
    falcon_red_light_typed_attachment->quadratic = 0.44f;
    falcon_red_light_typed_attachment->position = vec4_create(7.0f, 1.25f, 20.0f, 0.0f);
    darray_push(falcon.attachments, falcon_red_light_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, falcon);

    // terrain
    scene_node_config terrain = {0};
    terrain.name = "falcon";

    terrain.xform = kallocate(sizeof(scene_xform_config), MEMORY_TAG_SCENE);
    terrain.xform->scale = vec3_one();
    terrain.xform->position = vec3_create(-50.0f, -3.9f, -50.0f);
    terrain.xform->rotation = quat_identity();

    terrain.attachments = darray_create(scene_node_attachment_config);
    scene_node_attachment_config terrain_attachment = {0};
    terrain_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_TERRAIN;
    terrain_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_terrain), MEMORY_TAG_SCENE);
    scene_node_attachment_terrain* terrain_typed_mesh_attachment = terrain_attachment.attachment_data;
    terrain_typed_mesh_attachment->resource_name = "test_terrain";
    terrain_typed_mesh_attachment->name = "test_terrain";
    darray_push(terrain.attachments, terrain_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, terrain);

    // Environment
    scene_node_config environment = {0};
    environment.name = "environment";

    environment.attachments = darray_create(scene_node_attachment_config);

    scene_node_attachment_config skybox_attachment = {0};
    skybox_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_SKYBOX;
    skybox_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_skybox), MEMORY_TAG_SCENE);
    scene_node_attachment_skybox* skybox_typed_mesh_attachment = skybox_attachment.attachment_data;
    skybox_typed_mesh_attachment->cubemap_name = "skybox";
    darray_push(environment.attachments, skybox_attachment);

    scene_node_attachment_config dir_light_attachment = {0};
    dir_light_attachment.type = SCENE_NODE_ATTACHMENT_TYPE_DIRECTIONAL_LIGHT;
    dir_light_attachment.attachment_data = kallocate(sizeof(scene_node_attachment_directional_light), MEMORY_TAG_SCENE);
    scene_node_attachment_directional_light* dir_light_typed_mesh_attachment = dir_light_attachment.attachment_data;
    dir_light_typed_mesh_attachment->colour = vec4_create(80.0f, 80.0f, 70.0f, 1.0);
    dir_light_typed_mesh_attachment->direction = vec4_create(0.1f, -1.0f, 0.1f, 1.0f);
    dir_light_typed_mesh_attachment->shadow_distance = 100.0f;
    dir_light_typed_mesh_attachment->shadow_fade_distance = 5.0f;
    dir_light_typed_mesh_attachment->shadow_split_mult = 0.75f;
    darray_push(environment.attachments, dir_light_attachment);

    // Add to global nodes array.
    darray_push(resource_data->nodes, environment);
    /*
    // Set some defaults, create arrays.
    resource_data->directional_light_config.shadow_fade_distance = SHADOW_FADE_DISTANCE_DEFAULT;
    resource_data->directional_light_config.shadow_distance = SHADOW_DISTANCE_DEFAULT;
    resource_data->directional_light_config.shadow_split_mult = SHADOW_SPLIT_MULT_DEFAULT;
    resource_data->description = 0;
    resource_data->name = string_duplicate(name);
    resource_data->point_lights = darray_create(point_light_scene_config);
    resource_data->meshes = darray_create(mesh_scene_config);
    resource_data->terrains = darray_create(terrain_scene_config);

    u32 version = 0;
    scene_parse_mode mode = scene_PARSE_MODE_ROOT;

    // Buffer objects that get populated when in corresponding mode, and pushed to list when
    // leaving said mode.
    point_light_scene_config current_point_light_config = {0};
    mesh_scene_config current_mesh_config = {0};
    terrain_scene_config current_terrain_config = {0};

    // Read each line of the file.
    char line_buf[512] = "";
    char* p = &line_buf[0];
    u64 line_length = 0;
    u32 line_number = 1;
    */

    // TODO: Pass to kson parser.

    filesystem_close(&f);

    out_resource->data = resource_data;
    out_resource->data_size = sizeof(scene_config);

    return true;
}

static void scene_loader_unload(struct resource_loader* self, resource* resource) {
    scene_config* data = (scene_config*)resource->data;

    // TODO: properly destroy nodes, attachments, etc.

    if (data->name) {
        kfree(data->name, string_length(data->name) + 1, MEMORY_TAG_STRING);
    }

    if (data->description) {
        kfree(data->description, string_length(data->description) + 1, MEMORY_TAG_STRING);
    }

    if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
        KWARN("scene_loader_unload called with nullptr for self or resource.");
    }
}

static b8 scene_loader_write(struct resource_loader* self, resource* r) {
    if (!self || !r) {
        return false;
    }

    char* format_str = "%s/%s/%s%s";
    char full_file_path[512];
    string_format(full_file_path, format_str, resource_system_base_path(), self->type_path, r->name, ".kss");

    file_handle f;
    if (!filesystem_open(full_file_path, FILE_MODE_WRITE, false, &f)) {
        KERROR("scene_loader_write - unable to open simple scene file for writing: '%s'.", full_file_path);
        return false;
    }

    scene_config* resource_data = r->data;
    if (resource_data) {
        // TODO: Send to kson parser to be written to string.
    }
    b8 result = true;

    if (!result) {
        KERROR("Failed to write scene file.");
    }
    return result;
}

resource_loader scene_resource_loader_create(void) {
    resource_loader loader;
    loader.type = RESOURCE_TYPE_scene;
    loader.custom_type = 0;
    loader.load = scene_loader_load;
    loader.unload = scene_loader_unload;
    loader.write = scene_loader_write;
    loader.type_path = "scenes";

    return loader;
}
