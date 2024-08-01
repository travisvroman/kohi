#include <defines.h>

struct kruntime_plugin;

KAPI b8 kplugin_create(struct kruntime_plugin* out_plugin);
KAPI b8 kplugin_initialize(struct kruntime_plugin* plugin);
KAPI void kplugin_destroy(struct kruntime_plugin* plugin);
