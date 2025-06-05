#include "kasset_importer.h"

#include "importers/kasset_importer_material_obj_mtl.h"
#include "importers/kasset_importer_static_mesh_obj.h"
#include "logger.h"
#include "platform/filesystem.h"
#include "strings/kname.h"
#include "strings/kstring.h"

b8 import_from_path(const char* source_path, const char* target_path, const char* asset_name, const char* package_name) {
    if (!source_path || !string_length(source_path)) {
        KERROR("Path is required. Import failed.");
        return false;
    }

    const char* extension = string_extension_from_path(source_path, true);
    if (!extension) {
        return false;
    }

    const char* target_folder = string_directory_from_path(target_path);
    if (!target_folder) {
        return false;
    }

    const char* target_filename = string_filename_no_extension_from_path(target_path);
    if (!target_filename) {
        return false;
    }

    kname pkg_name = kname_create(package_name);

    b8 success = false;

    // NOTE: No VFS state available here. Use raw filesystem instead here.

    if (strings_equali(extension, ".obj")) {
        // OBJ import
        const char* content = filesystem_read_entire_text_file(source_path);
        if (!content) {
            KERROR("Failed to read file content for path '%s'. Import failed.", source_path);
            goto import_from_path_cleanup;
        }

        u32 material_file_count = 0;
        const char** material_file_paths = 0;
        success = kasset_static_mesh_obj_import(target_folder, target_filename, pkg_name, content, &material_file_count, &material_file_paths);

        // Secondary import of materials. If these fail, should not count as a static mesh import failure.
        for (u32 i = 0; i < material_file_count; ++i) {
            const char* mtl_file_name = string_filename_from_path(material_file_paths[i]);
            const char* mtl_directory = string_directory_from_path(material_file_paths[i]);
            const char* data = filesystem_read_entire_text_file(material_file_paths[i]);
            // FIXME: Need a different path for the materials here...
            // Perhaps this should try reading from the asset manifest?
            // For now, assume that the 'materials' folder should be used, that's next to the current folder (probably 'models').
            // 'models' should have a 'source' subfolder, so the model would be in 'models/source/' and the material would export to 'materials/'
            // right next to it.
            const char* kmt_target_file_path = string_format("%s/../../materials/", mtl_directory);
            b8 mtl_result = kasset_material_obj_mtl_import(kmt_target_file_path, mtl_file_name, pkg_name, data);
            string_free(mtl_file_name);
            string_free(mtl_directory);
            string_free(kmt_target_file_path);
            string_free(data);
            if (!mtl_result) {
                KWARN("Material file import failed. See logs for details");
            }
        }
    } else if (strings_equali(extension, ".mtl")) {
        // MTL import
        /* const char* mtl_file_name = string_filename_from_path(source_path); */
        const char* data = filesystem_read_entire_text_file(source_path);
        success = kasset_material_obj_mtl_import(target_folder, target_filename, pkg_name, data);
        /* string_free(mtl_file_name); */
        string_free(data);
        if (!success) {
            KERROR("Material file import failed (%s). See logs for details.", source_path);
            goto import_from_path_cleanup;
        }
    }

    success = true;
import_from_path_cleanup:

    if (extension) {
        string_free(extension);
    }

    return success;
}
