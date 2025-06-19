#include <defines.h>

#include <core_render_types.h>

KAPI b8 obj_2_ksm(const char* source_path, const char* target_path, const char* mtl_target_dir, const char* package_name);

KAPI b8 mtl_2_kmt(const char* source_path, const char* target_filename, const char* mtl_target_dir, const char* package_name);

KAPI b8 mtl_2_kmt(const char* source_path, const char* target_filename, const char* mtl_target_dir, const char* package_name);

KAPI b8 source_audio_2_kaf(const char* source_path, const char* target_path);

// if output_format is set, force that format. Otherwise use source file format.
KAPI b8 source_image_2_kbi(const char* source_path, const char* target_path, b8 flip_y, kpixel_format output_format);

KAPI b8 fnt_2_kbf(const char* source_path, const char* target_path);

typedef struct import_option {
    const char* name;
    const char* value;
} import_option;

KAPI b8 import_from_path(const char* source_path, const char* target_path, u8 option_count, const import_option* options);

KAPI b8 import_all_from_manifest(const char* manifest_path);
