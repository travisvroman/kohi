#pragma once

#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "strings/kname.h"

/**
 * An actor is an in-world representation of something which exists in or can be spawned in
 * the world. It may contain actor-components, which can be used to control how actors are rendered,
 * move about in the world, sound, etc. Each actor-component typically has reference to at least one resource, which
 * is generally what gets rendered (i.e. a static mesh resource), but not always (i.e. a sound effect).
 *
 * When used with a scene, these may be parented to one another via the scene's hierarchy view and
 * xform graph, when attached to a scene node.
 */
typedef struct kactor {
    kname name;
    k_handle xform;
} kactor;

typedef enum kactor_component_type {
    KACTOR_COMPONENT_TYPE_UNKNOWN,
    KACTOR_COMPONENT_TYPE_STATICMESH,
    KACTOR_COMPONENT_TYPE_SKYBOX,
    KACTOR_COMPONENT_TYPE_SKELETALMESH,
    KACTOR_COMPONENT_TYPE_HEIGTMAP_TERRAIN,
    KACTOR_COMPONENT_TYPE_WATER_PLANE,
    KACTOR_COMPONENT_TYPE_DIRECTIONAL_LIGHT,
    KACTOR_COMPONENT_TYPE_POINT_LIGHT,
} kactor_component_type;

typedef struct kactor_component {
    kname name;
    kactor_component_type type;
} kactor_component;

typedef struct kactor_component_staticmesh {
    kactor_component base;
    geometry g;
    kresource_material_instance material;
} kactor_component_staticmesh;

KAPI kactor_component* kactor_component_get(const kactor* actor, kactor_component_type type, kname name);

KAPI b8 kactor_component_add(kactor* actor, kactor_component* component);
