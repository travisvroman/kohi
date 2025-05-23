#include "kasset_importer.h"

#include "importers/kasset_importer_material_obj_mtl.h"
#include "importers/kasset_importer_static_mesh_obj.h"
#include "logger.h"
#include "platform/filesystem.h"
#include "strings/kname.h"
#include "strings/kstring.h"

b8 import_from_path(const char* path, const char* asset_name, const char* package_name) {
    if (!path || !string_length(path)) {
        KERROR("Path is required. Import failed.");
        return false;
    }

    const char* extension = string_extension_from_path(path, true);
    if (!extension) {
        return false;
    }

    kname pkg_name = kname_create(package_name);

    b8 success = false;

    // NOTE: No VFS state available here. Use raw filesystem instead here.

    if (strings_equali(extension, ".obj")) {
        // OBJ import
        const char* content = filesystem_read_entire_text_file(path);
        if (!content) {
            KERROR("Failed to read file content for path '%s'. Import failed.", path);
            goto import_from_path_cleanup;
        }

        u32 material_file_count = 0;
        const char** material_file_paths = 0;
        success = kasset_static_mesh_obj_import(kname_create(asset_name), kname_create(package_name), content, &material_file_count, &material_file_paths);

        // Secondary import of materials. If these fail, should not count as a static mesh import failure.
        for (u32 i = 0; i < material_file_count; ++i) {
            const char* mtl_file_name = string_filename_from_path(material_file_paths[i]);
            const char* data = filesystem_read_entire_text_file(material_file_paths[i]);
            b8 mtl_result = kasset_material_obj_mtl_import(kname_create(mtl_file_name), pkg_name, data);
            string_free(mtl_file_name);
            string_free(data);
            if (!mtl_result) {
                KWARN("Material file import failed. See logs for details");
            }
        }
    } else if (strings_equali(extension, ".mtl")) {
        // MTL import
        const char* mtl_file_name = string_filename_from_path(path);
        const char* data = filesystem_read_entire_text_file(path);
        success = kasset_material_obj_mtl_import(kname_create(mtl_file_name), pkg_name, data);
        string_free(mtl_file_name);
        string_free(data);
        if (!success) {
            KERROR("Material file import failed (%s). See logs for details.", path);
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
