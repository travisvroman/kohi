#include "utils_plugin_main.h"

#include "kohi.plugin.utils_version.h"

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

    // TODO: register plugin... stuff.

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
