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
#include "core/agent_channel.h"
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
 * @returns 0 on a clean shutdown, 1 if the engine failed to start, 2 if it stopped because of an
 * error (for example a failed simulation step or a fatal renderer error).
 */
#include "strings/kstring.h"

#include <errno.h>
#include <stdlib.h>

/**
 * @brief Returns the value following '--config' on the command line, or the application's
 * default config path if the flag is not given. Lets one binary run different configurations,
 * for example a headless one.
 */
static const char *app_config_path_from_args (int argc, char **argv) {
	for (int i = 1; i < argc; ++i) {
		if (strings_equal(argv[i], "--config")) {
			if (i + 1 < argc) {
				return argv[i + 1];
			}
			KWARN("'--config' was given without a path; using the default application config.");
		}
	}
	return application_config_path_get();
}

/** @brief Parses a whole decimal string as a u64, rejecting signs, junk and overflow. */
static b8 seed_parse (const char *text, u64 *out_seed) {
	if (!text || *text < '0' || *text > '9') {
		return false;
	}
	errno = 0;
	char *end = 0;
	unsigned long long value = strtoull(text, &end, 10);
	if (errno || *end) {
		return false;
	}
	*out_seed = (u64)value;
	return true;
}

int main (int argc, char **argv) {

	application app_inst = {0};

	// --agent: control the engine with JSON lines over stdin/stdout (see core/agent_channel.h).
	// Enabled before anything logs, so stdout only ever carries agent replies.
	for (int i = 1; i < argc; ++i) {
		if (strings_equal(argv[i], "--agent")) {
			agent_channel_enable();
		}
	}

	// --seed <n>: seed every engine RNG, overriding the config's random_seed.
	for (int i = 1; i < argc; ++i) {
		if (strings_equal(argv[i], "--seed")) {
			u64 seed = 0;
			if (i + 1 < argc && seed_parse(argv[i + 1], &seed)) {
				engine_random_seed_override(seed);
			} else {
				KERROR("'--seed' needs a non-negative integer.");
				return 1;
			}
		}
	}

	// Initialization.
	if (!engine_create(&app_inst, app_config_path_from_args(argc, argv), application_klib_name_get())) {
		KFATAL("Engine failed to create!.");
		return 1;
	}

	// Begin the engine loop.
	if (!engine_run(&app_inst)) {
		KINFO("Application did not shutdown gracefully.");
		return 2;
	}

	return 0;
}
