#include "ibl_probe.h"
#include "kresources/kresource_types.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kname.h"
#include "systems/texture_system.h"

b8 ibl_probe_create(kname cubemap_name, vec3 position, ibl_probe* out_probe) {
    if (!out_probe) {
        KERROR("ibl_probe_create requires a valid pointer to out_probe.");
        return false;
    }

    out_probe->cubemap_name = cubemap_name;
    out_probe->position = position;
    return true;
}

void ibl_probe_destroy(ibl_probe* probe) {
    if (probe) {
        ibl_probe_unload(probe);
    }

    kzero_memory(probe, sizeof(ibl_probe));
}

b8 ibl_probe_load(ibl_probe* probe) {
    if (!probe) {
        return false;
    }

    if (probe->cubemap_name == INVALID_KNAME) {
        // Nothing to do, return success.
        KWARN("No cubemap name assigned to ibl probe.");
        return true;
    }

    probe->ibl_cube_texture = texture_system_request_cube(probe->cubemap_name, true, false, 0, 0);

    return true;
}

void ibl_probe_unload(ibl_probe* probe) {
    if (probe->ibl_cube_texture) {
        texture_system_release_resource((kresource_texture*)probe->ibl_cube_texture);
        probe->ibl_cube_texture = 0;
    }
}
