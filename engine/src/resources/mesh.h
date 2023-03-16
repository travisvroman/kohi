#pragma once

#include "resource_types.h"

// KAPI b8 mesh_load_from_resource(const char* resource_name, mesh* out_mesh);

KAPI b8 mesh_create(mesh_config config, mesh* out_mesh);

KAPI b8 mesh_initialize(mesh* m);

KAPI b8 mesh_load(mesh* m);

KAPI b8 mesh_unload(mesh* m);

KAPI b8 mesh_destroy(mesh* m);