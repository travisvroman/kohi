#ifndef _KOHI_PLUGIN_TYPES_H_
#define _KOHI_PLUGIN_TYPES_H_

#include "defines.h"
#include "platform/platform.h"

struct frame_data;
struct kwindow;
struct kruntime_plugin;

typedef b8 (*PFN_kruntime_plugin_create)(struct kruntime_plugin* out_plugin);
typedef void (*PFN_kruntime_plugin_initialize)(struct kruntime_plugin* plugin);
typedef void (*PFN_kruntime_plugin_destroy)(struct kruntime_plugin* plugin);

typedef b8 (*PFN_kruntime_plugin_update)(struct kruntime_plugin* plugin, struct frame_data* p_frame_data);
typedef b8 (*PFN_kruntime_plugin_frame_prepare)(struct kruntime_plugin* plugin, struct frame_data* p_frame_data);
typedef b8 (*PFN_kruntime_plugin_render)(struct kruntime_plugin* plugin, struct frame_data* p_frame_data);

typedef void (*PFN_kruntime_plugin_on_window_resized)(void* plugin_state, struct kwindow* window, u16 width, u16 height);

/** @brief An opaque handle to the plugin's internal state. */
struct kruntime_plugin_state;

/**
 * A generic structure to hold function pointers for a given plugin. These serve as
 * the plugin's hook into the system at various points of its lifecycle. Only the
 * 'create' and 'destroy' are required, all others are optional. Also note that the "create"
 * isn't saved because it is only called the first time the plugin is loaded.
 *
 * NOTE: There must be an exported function named the same as _each_ parameter for it
 * to get picked up automatically. For example, the "vulkan renderer" plugin must have
 * an exported function called "kplugin_create". This is automatically found via dynamic
 * linking by name, and thus the names must match to facilitate automatic linking.
 */
typedef struct kruntime_plugin {
    /** @brief The plugin's name. Just for display, really. Serves no purpose. */
    const char* name;

    /** @brief The dynamically loaded library for the plugin. */
    dynamic_library library;

    /**
     * @brief A pointer to the plugin's `kplugin_initialize` function. Optional.
     */
    PFN_kruntime_plugin_initialize kplugin_initialize;

    /**
     * @brief A pointer to the plugin's `kplugin_destroy` function. Required.
     */
    PFN_kruntime_plugin_destroy kplugin_destroy;

    /** @brief A function pointer for the plugin's hook into the update loop. Optional. */
    PFN_kruntime_plugin_update kplugin_update;

    /** @brief A function pointer for the plugin's hook into the frame_prepare stage. Optional. */
    PFN_kruntime_plugin_frame_prepare kplugin_frame_prepare;

    /** @brief A function pointer for the plugin's hook into the render loop. Optional. */
    PFN_kruntime_plugin_render kplugin_render;

    /** @brief A function pointer for the plugin's hook into the window resize event. Optional. */
    PFN_kruntime_plugin_on_window_resized kplugin_on_window_resized;

    /** @brief The size of the plugin's internal state. */
    u64 plugin_state_size;

    /** @brief The block of memory holding the plugin's internal state. */
    struct kruntime_plugin_state* plugin_state;
} kruntime_plugin;

#endif
