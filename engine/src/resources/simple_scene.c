#include "simple_scene.h"

#include "core/logger.h"
#include "core/kmemory.h"
#include "containers/darray.h"
#include "math/transform.h"
#include "resources/resource_types.h"

#include "systems/light_system.h"

static u32 global_scene_id = 0;

b8 simple_scene_create(void* config, simple_scene* out_scene){
    if(!out_scene) {
        KERROR(("simple_scene_create(): A valid pointer to out_scene is required."));
        return false;
    }

    kzero_memory(out_scene, sizeof(simple_scene));

    out_scene->enabled = false;
    out_scene->state = SIMPLE_SCENE_STATE_UNINITIALIZED;
    out_scene->scene_transform = transform_create();
    global_scene_id++;
    out_scene->id = global_scene_id;

    // Internal "lists" of renderable objects.
    out_scene->dir_light = 0;
    out_scene->point_lights = darray_create(point_light*);
    out_scene->meshes = darray_create(mesh*);
    out_scene->sb = 0;

    // TODO: process scene config.

    return true;
}

b8 simple_scene_destroy(simple_scene* scene){
    if(!scene) {
        KERROR("simple_scene_destroy requires a valid pointer to a scene.");
        return false;
    }

    if(scene->state == SIMPLE_SCENE_STATE_LOADED) {
        b8 result = simple_scene_unload(scene);
        if(!result) {
            KERROR("simple_scene_destroy, failed to unload scene before destruction.");
            return false;
        }
    }

    if(scene->point_lights) {
        darray_destroy(scene->point_lights);
    }

    if(scene->meshes) {
        darray_destroy(scene->meshes);
    }

    kzero_memory(scene, sizeof(simple_scene));

    return true;
}

b8 simple_scene_initialize(simple_scene* scene){
    if(!scene) {
        KERROR("simple_scene_initialize requires a valid pointer to a scene.");
        return false;
    }

    // TODO: Process configuration and setup hierarchy.

    return true;
}

b8 simple_scene_load(simple_scene* scene){
    
}

b8 simple_scene_unload(simple_scene* scene){
    
}

b8 simple_scene_update(simple_scene* scene, const struct frame_data* p_frame_data){
    
}

b8 simple_scene_populate_render_packet(simple_scene* scene, const struct frame_data* p_frame_data, struct render_packet* packet){
    
}



b8 simple_scene_add_directional_light(simple_scene* scene, struct directional_light* light){
    
}

b8 simple_scene_add_point_light(simple_scene* scene, struct point_light* light){
    
}

b8 simple_scene_add_mesh(simple_scene* scene, struct mesh* m){
    
}

b8 simple_scene_add_skybox(simple_scene* scene, struct skybox* sb){
    
}


b8 simple_scene_remove_directional_light(simple_scene* scene, struct directional_light* light){
    
}

b8 simple_scene_remove_point_light(simple_scene* scene, struct point_light* light) {
    
}

b8 simple_scene_remove_mesh(simple_scene* scene, struct mesh* m) {

}

b8 simple_scene_remove_skybox(simple_scene* scene, struct skybox* sb) {

}