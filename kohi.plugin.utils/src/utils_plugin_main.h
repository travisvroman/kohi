#include <defines.h>

struct kruntime_plugin;

KAPI b8 kohi_plugin_utils_create (struct kruntime_plugin *out_plugin);
KAPI b8 kohi_plugin_utils_initialize (struct kruntime_plugin *plugin);
KAPI void kohi_plugin_utils_destroy (struct kruntime_plugin *plugin);
