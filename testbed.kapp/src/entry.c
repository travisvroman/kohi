/**
 * @file entry.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include <entry.h>

const char *application_config_path_get (void) {
	return "../testbed.kapp/app_config.kson";
}

const char *application_klib_name_get (void) {
	return "testbed.klib";
}
