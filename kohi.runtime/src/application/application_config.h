#ifndef _KOHI_APPLICATION_CONFIG_H_
#define _KOHI_APPLICATION_CONFIG_H_

#include <defines.h>
#include <math/math_types.h>

/**
 * @brief Configuration for an application window.
 */
typedef struct application_window_config {
    /** @brief The name of the window */
    const char* name;
    /** @brief The title of the window. */
    const char* title;
    /** @brief The initial window resolution. */
    vec2 resolution;
    /** @brief The initial position of the window upon creation.*/
    vec2 position;
} application_window_config;

/**
 * @brief Configuration for an application runtime plugin.
 */
typedef struct application_plugin_config {
    /** @brief The name of the plugin */
    const char* name;
    /** @brief The configuration of the plugin in string format, to be parsed by the plugin itself */
    const char* configuration_str;
} application_plugin_config;

/**
 * @brief Represents the top-level configuration for a core engine system.\
 */
typedef struct application_system_config {
    /** The name of the system */
    const char* name;
    /** @brief The configuration of the system in string format, to be parsed by the system iteself. */
    const char* configuration_str;
} application_system_config;

/**
 * @brief Represents configuration for the application. The application config
 * is fed to the engine on creation, so it knows how to configure itself internally.
 */
typedef struct application_config {

    /** @brief The application name used in windowing, if applicable. */
    const char* name;

    /** @brief The name of the renderer plugin. Must match one of the plugins in the supplied list. */
    const char* renderer_plugin_name;

    /** @brief The name of the audio plugin. Must match one of the plugins in the supplied list. */
    const char* audio_plugin_name;

    // darray of window configurations for the application.
    application_window_config* windows;

    // darray of configurations for core engine systems.
    application_system_config* systems;

    /** @brief The size of the engine's frame allocator. */
    u64 frame_allocator_size;

    /** @brief The size of the application-specific frame data. Set to 0 if not used. */
    u64 app_frame_data_size;
} application_config;

/**
 * @brief Attempt to parse the application config file's content into the
 * actual application config structure.
 */
b8 application_config_parse_file_content(const char* file_content, application_config* out_config);

#endif
