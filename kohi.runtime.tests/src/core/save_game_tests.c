#include "save_game_tests.h"

#include <core/save_game.h>
#include <defines.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <stdio.h>
#include <string.h>

#include "../expect.h"
#include "../test_manager.h"

static const u8 gold_bytes[] = {1, 2, 3, 4};
static const u8 level_bytes[] = {9, 8, 7};

static u8 save_game_pack_and_unpack_round_trip (void) {
	ksave_section sections[] = {
		{.name = "gold", .data = gold_bytes, .size = sizeof(gold_bytes)},
		{.name = "empty", .data = 0, .size = 0},
		{.name = "level", .data = level_bytes, .size = sizeof(level_bytes)},
	};
	void *file = 0;
	u64 file_size = 0;
	expect_to_be_true(ksave_pack(sections, 3, &file, &file_size));

	ksave_section loaded[4];
	u32 count = 0;
	expect_to_be_true(ksave_unpack(file, file_size, loaded, 4, &count));
	expect_should_be(3, count);
	const ksave_section *level = ksave_section_find(loaded, count, "level");
	expect_should_not_be(0, (u64)level);
	expect_should_be(sizeof(level_bytes), level->size);
	expect_should_be(0, memcmp(level->data, level_bytes, sizeof(level_bytes)));
	expect_should_be(0, ksave_section_find(loaded, count, "missing") == 0 ? 0 : 1);
	expect_should_be(0, ksave_section_find(loaded, count, "empty")->size);
	kfree(file);
	return true;
}

static u8 save_game_rejects_a_flipped_byte (void) {
	ksave_section sections[] = {{.name = "gold", .data = gold_bytes, .size = sizeof(gold_bytes)}};
	void *file = 0;
	u64 file_size = 0;
	expect_to_be_true(ksave_pack(sections, 1, &file, &file_size));

	// Any byte, header or payload, must be caught by the checksum.
	ksave_section loaded[1];
	u32 count = 0;
	for (u64 i = 0; i < file_size; ++i) {
		((u8 *)file)[i] ^= 0x40;
		expect_to_be_false(ksave_unpack(file, file_size, loaded, 1, &count));
		((u8 *)file)[i] ^= 0x40;
	}
	expect_to_be_true(ksave_unpack(file, file_size, loaded, 1, &count));
	// A truncated file is rejected too.
	expect_to_be_false(ksave_unpack(file, file_size - 1, loaded, 1, &count));
	kfree(file);
	return true;
}

static u8 save_game_rejects_long_names (void) {
	ksave_section sections[] = {{.name = "0123456789012345678901234567890123456789", .data = gold_bytes, .size = 1}};
	void *file = 0;
	u64 file_size = 0;
	expect_to_be_false(ksave_pack(sections, 1, &file, &file_size));
	return true;
}

static u8 save_game_file_write_replaces_and_reads_back (void) {
	const char *path = "save_game_test.ksav";
	ksave_section first[] = {{.name = "gold", .data = gold_bytes, .size = sizeof(gold_bytes)}};
	ksave_section second[] = {{.name = "level", .data = level_bytes, .size = sizeof(level_bytes)}};
	expect_to_be_true(ksave_file_write(path, first, 1));
	expect_to_be_true(ksave_file_write(path, second, 1));

	void *file = 0;
	u64 file_size = 0;
	expect_to_be_true(ksave_file_read(path, &file, &file_size));
	ksave_section loaded[2];
	u32 count = 0;
	expect_to_be_true(ksave_unpack(file, file_size, loaded, 2, &count));
	expect_should_be(1, count);
	expect_string_to_be("level", loaded[0].name);
	kfree(file);
	remove(path);

	expect_to_be_false(filesystem_exists("save_game_test.ksav.tmp"));
	expect_to_be_false(ksave_file_read(path, &file, &file_size));
	return true;
}

typedef struct game_state {
	u32 gold;
} game_state;

static b8 game_save (void *user, void **out_data, u64 *out_size) {
	game_state *state = user;
	u32 *data = kallocate(sizeof(u32), MEMORY_TAG_SCENE);
	*data = state->gold;
	*out_data = data;
	*out_size = sizeof(u32);
	return true;
}

static b8 game_restore (void *user, const void *data, u64 size) {
	if (size != sizeof(u32)) {
		return false;
	}
	((game_state *)user)->gold = *(const u32 *)data;
	return true;
}

static u8 save_game_hooks_round_trip_game_state (void) {
	game_state state = {.gold = 120};
	expect_to_be_true(ksave_hook_register("game", game_save, game_restore, &state));
	expect_to_be_false(ksave_hook_register("game", game_save, game_restore, &state));

	ksave_section sections[KSAVE_HOOK_MAX];
	u32 count = 0;
	expect_to_be_true(ksave_hooks_sections_collect(sections, KSAVE_HOOK_MAX, &count));
	expect_should_be(1, count);
	expect_string_to_be("hook.game", sections[0].name);

	state.gold = 5;
	expect_to_be_true(ksave_hooks_restore(sections, count));
	expect_should_be(120, state.gold);

	// A save made without the hook cannot be loaded, and nothing is touched.
	state.gold = 7;
	expect_to_be_false(ksave_hooks_restore(sections, 0));
	expect_should_be(7, state.gold);

	ksave_hooks_sections_free(sections, count);
	ksave_hook_unregister("game");
	expect_to_be_true(ksave_hooks_sections_collect(sections, KSAVE_HOOK_MAX, &count));
	expect_should_be(0, count);
	return true;
}

void save_game_register_tests (void) {
	test_manager_register_test(save_game_pack_and_unpack_round_trip, "save game: sections survive pack and unpack.");
	test_manager_register_test(save_game_rejects_a_flipped_byte, "save game: a damaged or cut-off file is rejected.");
	test_manager_register_test(save_game_rejects_long_names, "save game: a section name that is too long is rejected.");
	test_manager_register_test(save_game_file_write_replaces_and_reads_back, "save game: a file write replaces the old save and leaves no temporary.");
	test_manager_register_test(save_game_hooks_round_trip_game_state, "save game: hooks save and restore game state.");
}
