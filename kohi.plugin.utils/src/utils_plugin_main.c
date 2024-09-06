#include "utils_plugin_main.h"

#include "importers/kasset_importer_image.h"
#include "importers/kasset_importer_static_mesh_obj.h"
#include "kohi.plugin.utils_version.h"

#include <assets/kasset_importer_registry.h>
#include <assets/kasset_types.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <plugins/plugin_types.h>

b8 kplugin_create(struct kruntime_plugin* out_plugin) {
    if (!out_plugin) {
        KERROR("Cannot create a plugin without a pointer to hold it, ya dingus!");
        return false;
    }

    // NOTE: This plugin has no state.
    out_plugin->plugin_state_size = 0;
    out_plugin->plugin_state = 0;

    // TODO: Register known importer types.

    // Images - one per file extension.
    {
        const char* image_types[] = {"tga", "png", "jpg", "bmp"};
        for (u8 i = 0; i < 4; ++i) {
            kasset_importer image_importer = {0};
            image_importer.import = kasset_importer_image_import;
            if (!kasset_importer_registry_register(KASSET_TYPE_IMAGE, image_types[i], image_importer)) {
                KERROR("Failed to register image asset importer!");
                return false;
            }
        }
    }

    // Static mesh - Wavefront OBJ.
    {
        kasset_importer obj_importer = {0};
        obj_importer.import = kasset_importer_static_mesh_obj_import;
        if (!kasset_importer_registry_register(KASSET_TYPE_IMAGE, "obj", obj_importer)) {
            KERROR("Failed to register static mesh Wavefront OBJ asset importer!");
            return false;
        }
    }

    KINFO("Kohi Utils Plugin Creation successful (%s).", KVERSION);

    return true;
}

b8 kplugin_initialize(struct kruntime_plugin* plugin) {
    if (!plugin) {
        KERROR("Cannot initialize a plugin without a pointer to it, ya dingus!");
        return false;
    }

    KINFO("Kohi Utils plugin initialized successfully.");

    return true;
}

void kplugin_destroy(struct kruntime_plugin* plugin) {
    if (plugin) {
        // A no-op for this plugin since there is no state.
    }
}
