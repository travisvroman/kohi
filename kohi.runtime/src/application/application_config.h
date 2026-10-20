#ifndef _KOHI_APPLICATION_CONFIG_H_
#define _KOHI_APPLICATION_CONFIG_H_

#include "strings/kname.h"
#include <defines.h>
#include <math/math_types.h>

struct kwindow_config;

/**
 * @brief Configuration for an application runtime plugin.
 */
typedef struct application_plugin_config {
	/** @brief The name of the plugin */
	const char *name;
	/**
	 * @brief The configuration of the plugin in string format, to be parsed by the plugin itself.
	 * Can be 0/null if not required by the plugin.
	 * */
	const char *configuration_str;
} application_plugin_config;

/**
 * @brief Represents the top-level configuration for a core engine system.
 * Each system is responsible for parsing its own portion of the configuration structure, which
 * is provided here in string format. Systems not requiring config can simply not set this.
 */
typedef struct application_system_config {
	/** The name of the system */
	const char *name;
	/** @brief The configuration of the system in string format, to be parsed by the system iteself. */
	const char *configuration_str;
} application_system_config;

/**
 * @brief Represents configuration for the application. The application config
 * is fed to the engine on creation, so it knows how to configure itself internally.
 */
typedef struct application_config {

	/** @brief The application name used in windowing, if applicable. */
	const char *name;

	/** @brief The name of the audio plugin. Must match one of the plugins in the supplied list. */
	const char *audio_plugin_name;

	// darray of window configurations for the application.
	struct kwindow_config *windows;

	// darray of configurations for core engine systems.
	application_system_config *systems;

	/** @brief The size of the engine's frame allocator. */
	u64 frame_allocator_size;

	/** @brief The size of the application-specific frame data. Set to 0 if not used. */
	u64 app_frame_data_size;

	/** @brief The asset manifest file path. */
	const char *manifest_file_path;

	/** @brief The name of the default package to be used when loading assets, if one is not provided. */
	kname default_package_name;

	/**
	 * @brief Run without a display server. Windows become virtual (sized, but never shown), and
	 * no OS windowing or input events are processed. Pair with a renderer that needs no display,
	 * such as kohi.plugin.renderer.null. Optional, defaults to false.
	 */
	b8 headless;

	/**
	 * @brief When nonzero, every frame advances time by exactly 1/fixed_step_hz seconds instead
	 * of by the measured wall-clock time, so a run is repeatable. Optional, defaults to 0 (off).
	 */
	u32 fixed_step_hz;

	/** @brief When nonzero, the engine shuts down after this many frames. Optional, defaults to 0 (unlimited). */
	u64 max_frames;
} application_config;

/**
 * @brief Attempt to parse the application config file's content into the
 * actual application config structure.
 *
 * @param The file content of the application config (KSON format).
 * @param out_config A pointer to hold the application config structure.
 * @returns True on success; otherwise false.
 */
KAPI b8 application_config_parse_file_content (const char *file_content, application_config *out_config);

KAPI void application_config_destroy (application_config *config);

/**
 * @brief Attempts to get the generic-level configuration for the system with the provided name.
 *
 * @param config A constant pointer to the top-level application config.
 * @param system_name The name of the system to retrieve the top-level config for.
 * @param out_sys_config A pointer to hold the selected system config, if found.
 * @returns True on success; otherwise false.
 */
KAPI b8 application_config_system_config_get (const application_config *config, const char *system_name, application_system_config *out_sys_config);

#endif
