#include "save_game.h"

#include "logger.h"
#include "memory/kmemory.h"
#include "platform/filesystem.h"
#include "strings/kstring.h"
#include "utils/crc64.h"

#include <stdio.h>
#include <string.h>

#define KSAVE_MAGIC 0x5641534Bu // "KSAV" on disk.
#define KSAVE_VERSION 1u

typedef struct ksave_header {
	u32 magic;
	u32 version;
	u32 section_count;
	u32 reserved;
} ksave_header;

typedef struct ksave_section_header {
	char name[KSAVE_SECTION_NAME_MAX];
	u64 size;
} ksave_section_header;

b8 ksave_pack (const ksave_section *sections, u32 count, void **out_data, u64 *out_size) {
	if (!out_data || !out_size || (count && !sections)) {
		return false;
	}
	u64 total = sizeof(ksave_header) + sizeof(u64);
	for (u32 i = 0; i < count; ++i) {
		if (!sections[i].name || strlen(sections[i].name) >= KSAVE_SECTION_NAME_MAX) {
			KERROR("ksave_pack: section %u has no name or a name that is too long.", i);
			return false;
		}
		total += sizeof(ksave_section_header) + sections[i].size;
	}

	u8 *bytes = kallocate(total, MEMORY_TAG_SCENE);
	ksave_header header = {.magic = KSAVE_MAGIC, .version = KSAVE_VERSION, .section_count = count};
	u8 *cursor = bytes;
	kcopy_memory(cursor, &header, sizeof(header));
	cursor += sizeof(header);
	for (u32 i = 0; i < count; ++i) {
		ksave_section_header section_header = {0};
		strcpy(section_header.name, sections[i].name);
		section_header.size = sections[i].size;
		kcopy_memory(cursor, &section_header, sizeof(section_header));
		cursor += sizeof(section_header);
		if (sections[i].size) {
			kcopy_memory(cursor, sections[i].data, sections[i].size);
			cursor += sections[i].size;
		}
	}
	u64 checksum = crc64(0, bytes, (u64)(cursor - bytes));
	kcopy_memory(cursor, &checksum, sizeof(checksum));

	*out_data = bytes;
	*out_size = total;
	return true;
}

b8 ksave_unpack (const void *data, u64 size, ksave_section *out_sections, u32 max_sections, u32 *out_count) {
	if (!data || !out_count || size < sizeof(ksave_header) + sizeof(u64)) {
		KERROR("ksave_unpack: the file is too small to be a save.");
		return false;
	}
	const u8 *bytes = data;
	u64 stored_checksum;
	kcopy_memory(&stored_checksum, bytes + size - sizeof(u64), sizeof(u64));
	if (crc64(0, bytes, size - sizeof(u64)) != stored_checksum) {
		KERROR("ksave_unpack: the checksum does not match; the file is damaged.");
		return false;
	}

	ksave_header header;
	kcopy_memory(&header, bytes, sizeof(header));
	if (header.magic != KSAVE_MAGIC) {
		KERROR("ksave_unpack: not a Kohi save file.");
		return false;
	}
	if (header.version != KSAVE_VERSION) {
		KERROR("ksave_unpack: save version %u is not supported (expected %u).", header.version, KSAVE_VERSION);
		return false;
	}
	if (header.section_count > max_sections) {
		KERROR("ksave_unpack: the file has %u sections; the limit is %u.", header.section_count, max_sections);
		return false;
	}

	const u8 *cursor = bytes + sizeof(header);
	const u8 *end = bytes + size - sizeof(u64);
	for (u32 i = 0; i < header.section_count; ++i) {
		ksave_section_header section_header;
		if ((u64)(end - cursor) < sizeof(section_header)) {
			KERROR("ksave_unpack: the file ends inside section %u.", i);
			return false;
		}
		kcopy_memory(&section_header, cursor, sizeof(section_header));
		cursor += sizeof(section_header);
		if (section_header.name[KSAVE_SECTION_NAME_MAX - 1] != 0 || section_header.size > (u64)(end - cursor)) {
			KERROR("ksave_unpack: section %u is malformed.", i);
			return false;
		}
		// The name is the first member of the section header, so it can point into the buffer.
		out_sections[i].name = (const char *)(cursor - sizeof(section_header));
		out_sections[i].data = cursor;
		out_sections[i].size = section_header.size;
		cursor += section_header.size;
	}
	if (cursor != end) {
		KERROR("ksave_unpack: %llu unexpected bytes after the last section.", (u64)(end - cursor));
		return false;
	}
	*out_count = header.section_count;
	return true;
}

const ksave_section *ksave_section_find (const ksave_section *sections, u32 count, const char *name) {
	for (u32 i = 0; i < count; ++i) {
		if (strcmp(sections[i].name, name) == 0) {
			return &sections[i];
		}
	}
	return 0;
}

b8 ksave_file_write (const char *path, const ksave_section *sections, u32 count) {
	void *data = 0;
	u64 size = 0;
	if (!ksave_pack(sections, count, &data, &size)) {
		return false;
	}
	char temp_path[1024];
	snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
	b8 ok = filesystem_write_entire_binary_file(temp_path, size, data);
	kfree(data);
	if (!ok) {
		return false;
	}
#ifdef KPLATFORM_WINDOWS
	// rename fails on Windows when the target exists.
	remove(path);
#endif
	if (rename(temp_path, path) != 0) {
		KERROR("ksave_file_write: could not move '%s' to '%s'.", temp_path, path);
		remove(temp_path);
		return false;
	}
	return true;
}

b8 ksave_file_read (const char *path, void **out_data, u64 *out_size) {
	if (!filesystem_exists(path)) {
		KERROR("ksave_file_read: no file at '%s'.", path);
		return false;
	}
	file_handle file;
	if (!filesystem_open(path, FILE_MODE_READ, true, &file)) {
		return false;
	}
	u64 size = 0;
	b8 ok = filesystem_size(&file, &size) && size > 0;
	u8 *bytes = 0;
	if (ok) {
		bytes = kallocate(size, MEMORY_TAG_SCENE);
		u64 read = 0;
		ok = filesystem_read_all_bytes(&file, bytes, &read) && read == size;
	}
	filesystem_close(&file);
	if (!ok) {
		if (bytes) {
			kfree(bytes);
		}
		return false;
	}
	*out_data = bytes;
	*out_size = size;
	return true;
}

typedef struct ksave_hook {
	char name[KSAVE_SECTION_NAME_MAX];
	char section_name[KSAVE_SECTION_NAME_MAX];
	ksave_hook_save_fn save;
	ksave_hook_restore_fn restore;
	void *user;
} ksave_hook;

static ksave_hook hooks[KSAVE_HOOK_MAX];
static u32 hook_count;

#define KSAVE_HOOK_PREFIX "hook."

b8 ksave_hook_register (const char *name, ksave_hook_save_fn save, ksave_hook_restore_fn restore, void *user) {
	if (!name || !save || !restore || strlen(name) + sizeof(KSAVE_HOOK_PREFIX) > KSAVE_SECTION_NAME_MAX) {
		KERROR("ksave_hook_register: needs a name of at most %u characters and both callbacks.", (u32)(KSAVE_SECTION_NAME_MAX - sizeof(KSAVE_HOOK_PREFIX)));
		return false;
	}
	for (u32 i = 0; i < hook_count; ++i) {
		if (strcmp(hooks[i].name, name) == 0) {
			KERROR("ksave_hook_register: '%s' is already registered.", name);
			return false;
		}
	}
	if (hook_count == KSAVE_HOOK_MAX) {
		KERROR("ksave_hook_register: at most %u hooks.", KSAVE_HOOK_MAX);
		return false;
	}
	strcpy(hooks[hook_count].name, name);
	snprintf(hooks[hook_count].section_name, KSAVE_SECTION_NAME_MAX, KSAVE_HOOK_PREFIX "%s", name);
	hooks[hook_count].save = save;
	hooks[hook_count].restore = restore;
	hooks[hook_count].user = user;
	++hook_count;
	return true;
}

void ksave_hook_unregister (const char *name) {
	for (u32 i = 0; i < hook_count; ++i) {
		if (strcmp(hooks[i].name, name) == 0) {
			hooks[i] = hooks[--hook_count];
			return;
		}
	}
}

b8 ksave_hooks_sections_collect (ksave_section *out_sections, u32 max_sections, u32 *out_count) {
	if (hook_count > max_sections) {
		KERROR("ksave_hooks_sections_collect: %u hooks do not fit in %u sections.", hook_count, max_sections);
		return false;
	}
	for (u32 i = 0; i < hook_count; ++i) {
		void *data = 0;
		u64 size = 0;
		if (!hooks[i].save(hooks[i].user, &data, &size)) {
			KERROR("Save hook '%s' failed.", hooks[i].name);
			ksave_hooks_sections_free(out_sections, i);
			return false;
		}
		out_sections[i] = (ksave_section){.name = hooks[i].section_name, .data = data, .size = size};
	}
	*out_count = hook_count;
	return true;
}

void ksave_hooks_sections_free (ksave_section *sections, u32 count) {
	for (u32 i = 0; i < count; ++i) {
		if (sections[i].data) {
			kfree((void *)sections[i].data);
		}
	}
}

b8 ksave_hooks_restore (const ksave_section *sections, u32 count) {
	// Check every hook has data before any runs, so a missing section changes nothing.
	for (u32 i = 0; i < hook_count; ++i) {
		if (!ksave_section_find(sections, count, hooks[i].section_name)) {
			KERROR("The save has no data for '%s'.", hooks[i].name);
			return false;
		}
	}
	for (u32 i = 0; i < hook_count; ++i) {
		const ksave_section *section = ksave_section_find(sections, count, hooks[i].section_name);
		if (!hooks[i].restore(hooks[i].user, section->data, section->size)) {
			KERROR("Restore hook '%s' failed.", hooks[i].name);
			return false;
		}
	}
	return true;
}
