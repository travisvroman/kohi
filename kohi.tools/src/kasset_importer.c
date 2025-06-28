#include "kasset_importer.h"

#include "containers/darray.h"
#include "core_render_types.h"
#include "importers/kasset_importer_audio.h"
#include "importers/kasset_importer_bitmap_font_fnt.h"
#include "importers/kasset_importer_image.h"
#include "importers/kasset_importer_material_obj_mtl.h"
#include "importers/kasset_importer_static_mesh_obj.h"
#include "logger.h"
#include "platform/filesystem.h"
#include "platform/kpackage.h"
#include "strings/kstring.h"
#include "utils/render_type_utils.h"

/*
 *
 *
NOTE: Need to add required/optional options (lul) to import processes. Can vary by type/importer
kohi.tools -t "./assets/models/Tree.ksm" -s "./assets/models/source/Tree.obj" -mtl_target_path="./assets/materials/" -package_name="Testbed"
kohi.tools -t "./assets/models/Tree.ksm" -s "./assets/models/source/Tree.gltf" -mtl_target_path="./assets/materials/" -package_name="Testbed"
kohi.tools -t "./assets/images/orange_lines_512.kbi" -s "./assets/images/source/orange_lines_512.png" -flip_y=no
*/

// Returns the index of the option. -1 if not found.
static i16 get_option_index(const char* name, u8 option_count, const import_option* options);
static const char* get_option_value(const char* name, u8 option_count, const import_option* options);
static b8 extension_is_audio(const char* extension);
static b8 extension_is_image(const char* extension);

b8 obj_2_ksm(const char* source_path, const char* target_path, const char* mtl_target_dir, const char* package_name) {
    KDEBUG("Executing %s...", __FUNCTION__);
    // OBJ import
    const char* content = filesystem_read_entire_text_file(source_path);
    if (!content) {
        KERROR("Failed to read file content for path '%s'. Import failed.", source_path);
        return false;
    }

    u32 material_file_count = 0;
    const char** material_file_names = 0;
    // Parses source file, imports and writes asset to disk.
    if (!kasset_static_mesh_obj_import(target_path, content, &material_file_count, &material_file_names)) {
        KERROR("Failed to import obj file '%s'. See logs for details.", source_path);
        return false;
    }

    const char* source_folder = string_directory_from_path(source_path);

    // Secondary import of materials. If these fail, should not count as a static mesh import failure.
    for (u32 i = 0; i < material_file_count; ++i) {
        const char* mtl_file_name_no_extension = string_filename_no_extension_from_path(material_file_names[i]);
        const char* src_mtl_file_path = string_format("%s/%s", source_folder, material_file_names[i]);
        const char* data = filesystem_read_entire_text_file(src_mtl_file_path);
        b8 mtl_result = kasset_material_obj_mtl_import(mtl_target_dir, mtl_file_name_no_extension, package_name, data);
        string_free(mtl_file_name_no_extension);
        string_free(src_mtl_file_path);
        string_free(data);
        if (!mtl_result) {
            KWARN("Material file import failed (%s). See logs for details.", source_path);
        }
    }

    string_free(source_folder);

    return true;
}

b8 mtl_2_kmt(const char* source_path, const char* target_filename, const char* mtl_target_dir, const char* package_name) {
    KDEBUG("Executing %s...", __FUNCTION__);
    // MTL import
    /* const char* mtl_file_name = string_filename_from_path(source_path); */
    const char* data = filesystem_read_entire_text_file(source_path);
    b8 success = kasset_material_obj_mtl_import(mtl_target_dir, target_filename, package_name, data);
    /* string_free(mtl_file_name); */
    string_free(data);
    if (!success) {
        KERROR("Material file import failed (%s). See logs for details.", source_path);
        return false;
    }

    return true;
}

b8 source_audio_2_kaf(const char* source_path, const char* target_path) {
    KDEBUG("Executing %s...", __FUNCTION__);
    return kasset_audio_import(source_path, target_path);
}

// if output_format is set, force that format. Otherwise use source file format.
b8 source_image_2_kbi(const char* source_path, const char* target_path, b8 flip_y, kpixel_format output_format) {
    KDEBUG("Executing %s...", __FUNCTION__);
    return kasset_image_import(source_path, target_path, flip_y, output_format);
}

b8 fnt_2_kbf(const char* source_path, const char* target_path) {
    KDEBUG("Executing %s...", __FUNCTION__);
    return kasset_bitmap_font_fnt_import(source_path, target_path);
}

b8 import_from_path(const char* source_path, const char* target_path, u8 option_count, const import_option* options) {
    if (!source_path || !string_length(source_path)) {
        KERROR("Path is required. Import failed.");
        return false;
    }

    // The source file extension dictates what importer is used.
    const char* source_extension = string_extension_from_path(source_path, true);
    if (!source_extension) {
        return false;
    }

    /* const char* target_folder = string_directory_from_path(target_path);
    if (!target_folder) {
        return false;
    } */

    const char* target_filename = string_filename_no_extension_from_path(target_path);
    if (!target_filename) {
        return false;
    }

    b8 success = false;

    // NOTE: No VFS state available here. Use raw filesystem instead here.

    if (strings_equali(source_extension, ".obj")) {
        // optional
        const char* mtl_target_dir = get_option_value("mtl_target_path", option_count, options);
        // optional
        const char* package_name = get_option_value("package_name", option_count, options);

        if (!obj_2_ksm(source_path, target_path, mtl_target_dir, package_name)) {
            goto import_from_path_cleanup;
        }

    } else if (strings_equali(source_extension, ".mtl")) {

        // required
        const char* mtl_target_dir = get_option_value("mtl_target_path", option_count, options);
        if (!mtl_target_dir) {
            KERROR("mtl_2_kmt requires property 'mtl_target_path' to be set.");
            goto import_from_path_cleanup;
        }

        // required
        const char* package_name = get_option_value("package_name", option_count, options);
        if (!package_name) {
            KERROR("mtl_2_kmt requires property 'package_name' to be set.");
            goto import_from_path_cleanup;
        }

        if (!mtl_2_kmt(source_path, target_filename, mtl_target_dir, package_name)) {
            goto import_from_path_cleanup;
        }
    } else if (extension_is_audio(source_extension)) {
        if (!source_audio_2_kaf(source_path, target_path)) {
            goto import_from_path_cleanup;
        }
    } else if (extension_is_image(source_extension)) {
        b8 flip_y = true;
        kpixel_format output_format = KPIXEL_FORMAT_UNKNOWN;

        // Extract optional properties.
        const char* flip_y_str = get_option_value("flip_y", option_count, options);
        if (flip_y_str) {
            string_to_bool(flip_y_str, &flip_y);
        }

        const char* output_format_str = get_option_value("output_format", option_count, options);
        if (output_format_str) {
            output_format = string_to_kpixel_format(output_format_str);
        }

        if (!source_image_2_kbi(source_path, target_path, flip_y, output_format)) {
            goto import_from_path_cleanup;
        }
    } else if (strings_equali(source_extension, ".fnt")) {
        if (!fnt_2_kbf(source_path, target_path)) {
            goto import_from_path_cleanup;
        }
    } else {
        KERROR("Unknown file extension (%s) provided in import path '%s'", source_extension, source_path);
        goto import_from_path_cleanup;
    }

    success = true;
import_from_path_cleanup:

    if (source_extension) {
        string_free(source_extension);
    }

    return success;
}

b8 import_all_from_manifest(const char* manifest_path) {
    if (!manifest_path) {
        return false;
    }

    const char* asset_base_directory = string_directory_from_path(manifest_path);
    if (!asset_base_directory) {
        KERROR("Failed to obtain base directory of manifest file. See logs for details.");
        return false;
    }

    // Read and deserialize the manifest first.
    const char* manifest_content = filesystem_read_entire_text_file(manifest_path);
    if (!manifest_content) {
        KERROR("Failed to read manifest file. See logs for details.");
        return false;
    }

    asset_manifest manifest = {0};
    if (!kpackage_parse_manifest_file_content(manifest_path, &manifest)) {
        KERROR("Failed to parse asset manifest. See logs for details.");
        return false;
    }

    u32 asset_count = darray_length(manifest.assets);

    KINFO("Asset manifest '%s' has a total listing of %u assets.", manifest_path, asset_count);

    for (u32 i = 0; i < asset_count; ++i) {
        asset_manifest_asset* asset = &manifest.assets[i];
        if (!asset->source_path) {
            KTRACE("Asset '%s' (%s) does NOT have a source_path. Nothing to import.", kname_string_get(asset->name), asset->path);
        } else {
            KINFO("Asset '%s' (%s) DOES have a source_path of '%s'. Importing...", kname_string_get(asset->name), asset->path, asset->source_path);

            // The source file extension dictates what importer is used.
            const char* source_extension = string_extension_from_path(asset->source_path, true);
            if (!source_extension) {
                KWARN("Unable to determine source extension for path '%s'. Skipping import.", asset->source_path);
                continue;
            }

            if (strings_equali(source_extension, ".obj")) {
                // NOTE: Using defaults for this.
                const char* mtl_target_dir = string_format("%s/%s", manifest.path, "assets/materials/");
                const char* package_name = kname_string_get(manifest.name);

                if (!obj_2_ksm(asset->source_path, asset->path, mtl_target_dir, package_name)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (strings_equali(source_extension, ".mtl")) {
                const char* mtl_target_dir = string_directory_from_path(asset->path);
                if (!mtl_target_dir) {
                    KERROR("mtl_2_kmt requires property 'mtl_target_path' to be set.");
                    goto import_all_from_manifest_cleanup;
                }

                const char* package_name = kname_string_get(manifest.name);
                if (!mtl_2_kmt(asset->source_path, asset->path, mtl_target_dir, package_name)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (extension_is_audio(source_extension)) {
                if (!source_audio_2_kaf(asset->source_path, asset->path)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (extension_is_image(source_extension)) {
                // Always assume y should be flipped on import.
                b8 flip_y = asset->flip_y;
                // NOTE: When importing this way, always use the pixel format as provided by the asset.
                kpixel_format output_format = KPIXEL_FORMAT_UNKNOWN;

                if (!source_image_2_kbi(asset->source_path, asset->path, flip_y, output_format)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else if (strings_equali(source_extension, ".fnt")) {
                if (!fnt_2_kbf(asset->source_path, asset->path)) {
                    goto import_all_from_manifest_cleanup;
                }
            } else {
                KERROR("Unknown file extension (%s) provided in import path '%s'", source_extension, asset->source_path);
                goto import_all_from_manifest_cleanup;
            }

        import_all_from_manifest_cleanup:

            if (source_extension) {
                string_free(source_extension);
            }
        }
    }

    return true;
}

// Returns the index of the option. -1 if not found.
static i16 get_option_index(const char* name, u8 option_count, const import_option* options) {
    if (!name || !option_count || !options) {
        return -1;
    }

    for (u8 i = 0; i < option_count; ++i) {
        if (strings_equali(name, options[i].name)) {
            return (i16)i;
        }
    }

    return -1;
}

static const char* get_option_value(const char* name, u8 option_count, const import_option* options) {
    i16 index = get_option_index(name, option_count, options);
    if (index < 0) {
        return 0;
    }

    return options[index].value;
}

static b8 extension_is_audio(const char* extension) {
    const char* extensions[3] = {".mp3", ".ogg", ".wav"};
    for (u8 i = 0; i < 3; ++i) {
        if (strings_equali(extension, extensions[i])) {
            return true;
        }
    }

    return false;
}

static b8 extension_is_image(const char* extension) {
    const char* extensions[5] = {".jpg", ".jpeg", ".png", ".tga", ".bmp"};
    for (u8 i = 0; i < 5; ++i) {
        if (strings_equali(extension, extensions[i])) {
            return true;
        }
    }

    return false;
}
