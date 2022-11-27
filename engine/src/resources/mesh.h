#pragma once

#include "resource_types.h"

KAPI b8 mesh_load_from_resource(const char* resource_name, mesh* out_mesh);

KAPI void mesh_unload(mesh* m);
