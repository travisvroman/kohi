/**
 * @file entry.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the main entry point to the application.
 * It also contains a reference to an externally defined create_application
 * method, which should create and set a custom application object to the
 * location pointed to by out_app. This would be provided by the
 * consuming application, which is then hooked into the engine itself
 * during the bootstrapping phase.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2025
 *
 */

/*! \mainpage Home Page
 *
 * \section intro Introduction
 *
 * This site contains the auto-generated API documentation for the Kohi Game Engine.
 *
 * \section information Information
 *
 * \subsection mainsite Main Website
 * See [kohiengine.com](https://kohiengine.com) for the newest project updates.
 *
 * \subsection twitch Twitch
 * The Twitch channel is where development happens LIVE on stream.
 *
 * Link: [Twitch Channel](https://twitch.tv/travisvroman)
 *
 * \subsection yt YouTube
 * The YouTube channel contains all of the archives of the Twitch streams, as well as the original video series for the Kohi Game Engine. It also contains lots of other content outside of this project.
 *
 * Link: [YouTube Channel](https://youtube.com/travisvroman)
 */
#pragma once

#include "application/application_types.h"
#include "core/engine.h"
#include "logger.h"

/**
 * @brief Gets the application config path from the application.
 *
 * @return const char* The application path.
 */
extern const char *application_config_path_get (void);
extern const char *application_klib_name_get (void);

/**
 * @brief The main entry point of the application.
 * @returns 0 on successful execution; nonzero on error.
 */
#include "strings/kstring.h"

/**
 * @brief Returns the value following the given flag on the command line, or 0 if the flag
 * is not given (or is given without a value).
 */
static const char *command_line_value (int argc, char **argv, const char *flag) {
	for (int i = 1; i < argc; ++i) {
		if (strings_equal(argv[i], flag)) {
			if (i + 1 < argc) {
				return argv[i + 1];
			}
			KWARN("'%s' was given without a value; ignoring it.", flag);
		}
	}
	return 0;
}

/**
 * Command-line flags:
 *   --config <path>  Application config to load instead of the default one, for example a
 *                    headless config.
 *   --log <path>     File to write the log to instead of the config's log_file (default
 *                    "console.log"). Give each instance its own file when running several.
 */
int main (int argc, char **argv) {

	application app_inst = {0};

	const char *app_config_path = command_line_value(argc, argv, "--config");
	if (!app_config_path) {
		app_config_path = application_config_path_get();
	}
	app_inst.log_file_path_override = command_line_value(argc, argv, "--log");

	// Initialization.
	if (!engine_create(&app_inst, app_config_path, application_klib_name_get())) {
		// KERROR, not KFATAL: KFATAL raises a debug trap, so the process would die with SIGTRAP
		// instead of exiting with this code.
		KERROR("Engine failed to create. See the errors above.");
		return 1;
	}

	// Begin the engine loop.
	if (!engine_run(&app_inst)) {
		KINFO("Application did not shutdown gracefully.");
		return 2;
	}

	return 0;
}
