#pragma once

#include <defines.h>

#include <core_render_types.h>

typedef enum kimport_flags {
	KIMPORT_FLAG_NONE = 0,
	KIMPORT_FLAG_UPDATED_ONLY_BIT = 1 << 0
} kimport_flags;

typedef u32 kimport_flag_bits;

b8 source_audio_2_kaf (const char *source_path, const char *target_path);

// if output_format is set, force that format. Otherwise use source file format.
b8 source_image_2_kbi (const char *source_path, const char *target_path, b8 flip_y, kpixel_format output_format);

b8 fnt_2_kbf (const char *source_path, const char *target_path);

b8 dae_fbx_2_kam (const char *source_path, const char *target_path, const char *material_target_dir, const char *package_name);

typedef struct import_option {
	const char *name;
	const char *value;
} import_option;

b8 import_from_path (const char *source_path, const char *target_path, u8 option_count, const import_option *options);

b8 import_all_from_manifest (const char *manifest_path, kimport_flag_bits flags);
