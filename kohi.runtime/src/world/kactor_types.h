#pragma once

#include "kresources/kresource_types.h"
#include "math/geometry.h"
#include "strings/kname.h"

typedef enum kactor_type {
    KACTOR_TYPE_GROUP,
    KACTOR_TYPE_STATICMESH,
    KACTOR_TYPE_SKYBOX,
    KACTOR_TYPE_SKELETALMESH,
    KACTOR_TYPE_HEIGTMAP_TERRAIN
} kactor_type;

typedef struct kactor {
    kname name;
    kactor_type type;
    k_handle xform;
} kactor;

typedef struct kactor_staticmesh {
    kactor base;
    geometry g;
    kresource_material_instance material;
} kactor_staticmesh;
