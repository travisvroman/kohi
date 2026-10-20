#pragma once

#include "defines.h"

/**
 * @file save_game.h
 * @brief The save file container and the hooks that let a game put its own state in it.
 *
 * A save file is a list of named sections followed by a CRC-64 of everything before it. The engine
 * writes a section for the scene and one for the physics state; a game adds its own through
 * ksave_hook_register. Multi-byte values are stored in the machine's byte order, which is
 * little-endian on every platform Kohi supports.
 */

#define KSAVE_SECTION_NAME_MAX 32

typedef struct ksave_section {
	/** @brief At most KSAVE_SECTION_NAME_MAX - 1 characters. */
	const char *name;
	const void *data;
	u64 size;
} ksave_section;

/** @brief Builds the bytes of a save file. Free the result with kfree. */
KAPI b8 ksave_pack (const ksave_section *sections, u32 count, void **out_data, u64 *out_size);

/**
 * @brief Checks a save file's header and checksum and lists its sections. The section data points
 * into `data`, which must outlive them. Fails if the file has more than max_sections sections.
 */
KAPI b8 ksave_unpack (const void *data, u64 size, ksave_section *out_sections, u32 max_sections, u32 *out_count);

/** @brief The section with this name, or null. */
KAPI const ksave_section *ksave_section_find (const ksave_section *sections, u32 count, const char *name);

/** @brief Writes the file through a temporary, so a failed write never destroys the previous save. */
KAPI b8 ksave_file_write (const char *path, const ksave_section *sections, u32 count);

/** @brief Reads a whole file into a new buffer. Free it with kfree. */
KAPI b8 ksave_file_read (const char *path, void **out_data, u64 *out_size);

/**
 * @brief Game state to include in every save. Called when saving, `save` returns a buffer from kallocate
 * (the engine frees it with kfree). Called when loading, `restore` is given the bytes `save` produced.
 */
typedef b8 (*ksave_hook_save_fn)(void *user, void **out_data, u64 *out_size);
typedef b8 (*ksave_hook_restore_fn)(void *user, const void *data, u64 size);

#define KSAVE_HOOK_MAX 16

/** @brief Registers game state under a unique name (shorter than KSAVE_SECTION_NAME_MAX - 5). */
KAPI b8 ksave_hook_register (const char *name, ksave_hook_save_fn save, ksave_hook_restore_fn restore, void *user);
KAPI void ksave_hook_unregister (const char *name);

/** @brief Runs every hook's save and returns its sections (false, with nothing to free, if one fails), named "hook.<name>". Pair with ksave_hooks_sections_free. */
KAPI b8 ksave_hooks_sections_collect (ksave_section *out_sections, u32 max_sections, u32 *out_count);
KAPI void ksave_hooks_sections_free (ksave_section *sections, u32 count);
/** @brief Hands each hook the "hook.<name>" section it saved. A hook with no section in the file fails the load. */
KAPI b8 ksave_hooks_restore (const ksave_section *sections, u32 count);
