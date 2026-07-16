#include "kpackage.h"

#include "assets/kasset_utils.h"
#include "containers/darray.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "parsers/kson_parser.h"
#include "platform/filesystem.h"
#include "strings/kname.h"
#include "strings/kstring.h"

typedef struct asset_entry {
	kname name;
	// If loaded from binary, this will be null.
	const char *path;
	// Should be populated if the asset was imported.
	const char *source_path;

	// Path relative to package.
	const char *local_path;
	// Source path relative to package.
	const char *local_source_path;

	kasset_type type;

	// If loaded from binary, these define where the asset is in the blob.
	u64 offset;
	u64 size;
} asset_entry;

typedef struct kpackage_internal {
	// darray of all asset entries.
	asset_entry *entries;

	asset_manifest_reference *references;
} kpackage_internal;

b8 kpackage_create_from_manifest (const char *manifest_file_path, const asset_manifest *manifest, kpackage *out_package) {
	if (!manifest || !out_package) {
		KERROR("kpackage_create_from_manifest requires valid pointers to manifest and out_package.");
		return false;
	}

	kzero_memory(out_package, sizeof(kpackage));

	if (manifest->name) {
		out_package->name = manifest->name;
	} else {
		KERROR("Manifest must contain a name.");
		return false;
	}

	out_package->is_binary = false;
	out_package->manifest_file_path = string_duplicate(manifest_file_path);

	out_package->internal_data = kallocate(sizeof(kpackage_internal), MEMORY_TAG_PACKAGE);

	out_package->internal_data->references = darray_create(asset_manifest_reference);
	u32 ref_count = darray_length(manifest->references);
	for (u32 i = 0; i < ref_count; ++i) {
		darray_push(out_package->internal_data->references, manifest->references[i]);
		out_package->internal_data->references[i].path = string_duplicate(manifest->references[i].path);
	}

	// Process manifest
	u32 asset_count = darray_length(manifest->assets);
	for (u32 i = 0; i < asset_count; ++i) {
		asset_manifest_asset *asset = &manifest->assets[i];

		asset_entry new_entry = {0};
		new_entry.name = asset->name;
		new_entry.type = asset->type;
		new_entry.path = string_duplicate(asset->path);
		new_entry.local_path = string_duplicate(asset->local_path);
		new_entry.source_path = KNULL;
		if (asset->source_path) {
			new_entry.source_path = string_duplicate(asset->source_path);
		}
		new_entry.local_source_path = KNULL;
		if (asset->local_source_path) {
			new_entry.local_source_path = string_duplicate(asset->local_source_path);
		}
		// NOTE: Size and offset don't get filled out/used with a manifest version of a package.

		// Allocate the entry type array if it isn't already.
		if (!out_package->internal_data->entries) {
			out_package->internal_data->entries = darray_create(asset_entry);
		}
		// Push the asset to it.
		darray_push(out_package->internal_data->entries, new_entry);
	}

	return true;
}

b8 kpackage_create_from_binary (u64 size, void *bytes, kpackage *out_package) {
	if (!size || !bytes || !out_package) {
		KERROR("kpackage_create_from_binary requires valid pointers to bytes and out_package, and size must be nonzero.");
		return false;
	}

	out_package->is_binary = false;

	// Process manifest
	// TODO: the thing.
	KERROR("kpackage_create_from_binary not yet supported.");
	return false;
}

void kpackage_destroy (kpackage *package) {
	if (package) {
		if (package->internal_data->entries) {
			u32 entry_count = darray_length(package->internal_data->entries);
			for (u32 j = 0; j < entry_count; ++j) {
				asset_entry *entry = &package->internal_data->entries[j];
				string_free(entry->path);
				string_free(entry->source_path);
			}
			darray_destroy(package->internal_data->entries);
		}

		if (package->internal_data) {
			kfree(package->internal_data);
		}

		kzero_memory(package, sizeof(kpackage_internal));
	}
}

kname *kpackage_asset_names_by_type (const kpackage *package, kasset_type type, u32 *out_count) {
	u32 entry_count = darray_length(package->internal_data->entries);
	for (u32 j = 0; j < entry_count; ++j) {
		asset_entry *entry = &package->internal_data->entries[j];
		if (entry->type == type) {
			(*out_count)++;
		}
	}
	kname *results = KNULL;
	if (*out_count) {
		results = KALLOC_TYPE_CARRAY(kname, *out_count);
		u32 idx = 0;
		for (u32 j = 0; j < entry_count; ++j) {
			asset_entry *entry = &package->internal_data->entries[j];
			if (entry->type == type) {
				results[idx] = entry->name;
				idx++;
			}
		}
	}
	return results;
}

static asset_entry *asset_entry_get (const kpackage *package, kname name) {
	// Search the type lookup's entries for the matching name.
	u32 entry_count = darray_length(package->internal_data->entries);
	for (u32 j = 0; j < entry_count; ++j) {
		asset_entry *entry = &package->internal_data->entries[j];
		if (entry->name == name) {
			return entry;
		}
	}

	KTRACE("Package '%s': No entry called '%s' exists.", kname_string_get(package->name), kname_string_get(name));
	return 0;
}

static kpackage_result asset_get_data (const kpackage *package, b8 is_binary, kname name, u64 *out_size, const void **out_data) {

	const char *package_name = kname_string_get(package->name);
	const char *name_str = kname_string_get(name);
	asset_entry *entry = asset_entry_get(package, name);
	if (!entry) {
		return KPACKAGE_RESULT_ASSET_GET_FAILURE;
	}

	if (package->is_binary) {
		KERROR("binary packages not yet supported.");
		return KPACKAGE_RESULT_INTERNAL_FAILURE;
	} else {
		kpackage_result result = KPACKAGE_RESULT_INTERNAL_FAILURE;

		// Validate asset path.
		const char *asset_path = entry->path;
		if (!asset_path) {
			KTRACE("Package '%s': No path exists for asset '%s'.", package_name, name_str);
			result = KPACKAGE_RESULT_ASSET_GET_FAILURE;
			return result;
		}

		// Validate that the file exists.
		if (!filesystem_exists(asset_path)) {
			KTRACE("Package '%s': Invalid path ('%s') for asset '%s'.", package_name, asset_path, name_str);
			result = KPACKAGE_RESULT_ASSET_GET_FAILURE;
			return result;
		}
		void *data = 0;

		// load the file content from disk.
		file_handle f = {0};
		if (!filesystem_open(asset_path, FILE_MODE_READ, is_binary, &f)) {
			KERROR("Package '%s': Failed to open asset '%s' file at path: '%s'.", package_name, name_str, asset_path);
			result = KPACKAGE_RESULT_ASSET_GET_FAILURE;
			goto get_data_cleanup;
		}

		// Get the file size.
		u64 original_file_size = 0;
		if (!filesystem_size(&f, &original_file_size)) {
			KERROR("Package '%s': Failed to get size for asset '%s' file at path: '%s'.", package_name, name_str, asset_path);
			result = KPACKAGE_RESULT_ASSET_GET_FAILURE;
			goto get_data_cleanup;
		}

		// Account for the null terminator for text files.
		u64 actual_file_size = original_file_size;
		if (!is_binary) {
			actual_file_size++;
		}

		data = kallocate(actual_file_size, is_binary ? MEMORY_TAG_ASSET : MEMORY_TAG_STRING);

		u64 read_size = 0;
		if (is_binary) {
			// Load as binary
			if (!filesystem_read_all_bytes(&f, data, &read_size)) {
				KERROR("Package '%s': Failed to read asset '%s' as binary, at file at path: '%s'.", package_name, name_str, asset_path);
				goto get_data_cleanup;
			}
		} else {
			// Load as text
			if (!filesystem_read_all_text(&f, data, &read_size)) {
				KERROR("Package '%s': Failed to read asset '%s' as text, at file at path: '%s'.", package_name, name_str, asset_path);
				goto get_data_cleanup;
			}
		}

		// Sanity check to make sure the bounds haven't been breached.
		KASSERT_MSG(read_size <= actual_file_size, "File read exceeded bounds of data allocation based on file size.");

		// This means that data is bigger than it needs to be, and that a smaller block of memory can be used.
		if (read_size < original_file_size) {
			KTRACE("Package '%s': asset '%s', file at path: '%s' - Read size/file size mismatch (%llu, %llu).", package_name, name_str, asset_path, read_size, original_file_size);
			void *temp = kallocate(read_size + (is_binary ? 0 : 1), MEMORY_TAG_ASSET);
			kcopy_memory(temp, data, read_size);
			kfree(data);
			data = temp;
			actual_file_size = read_size;
			// Account for the null terminator for text files.
			if (!is_binary) {
				actual_file_size++;
				((char *)data)[actual_file_size - 1] = 0;
			}
		}

		// Set the output.
		*out_data = data;
		*out_size = actual_file_size;

		// Success!
		result = KPACKAGE_RESULT_SUCCESS;

	get_data_cleanup:
		filesystem_close(&f);

		if (result != KPACKAGE_RESULT_SUCCESS) {
			if (data) {
				kfree(data);
			}
		} else {
			// KERROR("Package '%s' does not contain asset '%s'.", package_name, name_str);
		}
		return result;
	}
}

kpackage_result kpackage_asset_bytes_get (const kpackage *package, kname name, u64 *out_size, const void **out_data) {
	if (!package || !name || !out_size || !out_data) {
		KERROR("kpackage_asset_bytes_get requires valid pointers to package, name, out_size, and out_data.");
		return 0;
	}

	return asset_get_data(package, true, name, out_size, out_data);
}

kpackage_result kpackage_asset_text_get (const kpackage *package, kname name, u64 *out_size, const char **out_text) {
	if (!package || !name || !out_size || !out_text) {
		KERROR("kpackage_asset_text_get requires valid pointers to package, name, out_size, and out_text.");
		return 0;
	}

	return asset_get_data(package, false, name, out_size, (const void **)out_text);
}

const char *kpackage_path_for_asset (const kpackage *package, kname name) {
	u32 entry_count = darray_length(package->internal_data->entries);
	for (u32 j = 0; j < entry_count; ++j) {
		asset_entry *entry = &package->internal_data->entries[j];
		if (entry->name == name) {
			if (package->is_binary) {
				KERROR("binary packages not yet supported.");
				return 0;
			} else {
				return string_duplicate(entry->path);
			}
		}
	}
	return 0;
}

const char *kpackage_source_path_for_asset (const kpackage *package, kname name) {
	u32 entry_count = darray_length(package->internal_data->entries);
	for (u32 j = 0; j < entry_count; ++j) {
		asset_entry *entry = &package->internal_data->entries[j];
		if (entry->name == name) {
			if (package->is_binary) {
				KERROR("binary packages not yet supported.");
				return 0;
			} else {
				if (entry->source_path) {
					return string_duplicate(entry->source_path);
				}
				return 0;
			}
		}
	}
	return 0;
}

// Writes file to disk for packages using the asset manifest, not binary packages.
static b8 kpackage_asset_write_file_internal (kpackage *package, kname name, u64 size, const void *bytes, b8 is_binary) {
	file_handle f = {0};
	// FIXME: Brute-force lookup, add a hash table or something better...
	u32 entry_count = darray_length(package->internal_data->entries);
	for (u32 i = 0; i < entry_count; ++i) {
		asset_entry *entry = &package->internal_data->entries[i];
		if (entry->name == name) {
			// Found a match.
			if (!filesystem_open(entry->path, FILE_MODE_WRITE, is_binary, &f)) {
				KERROR("Unable to open asset file for writing: '%s'", entry->path);
				return false;
			}

			u64 bytes_written = 0;
			if (!filesystem_write(&f, size, bytes, &bytes_written)) {
				KERROR("Unable to write to asset file: '%s'", entry->path);
				filesystem_close(&f);
				return false;
			}

			if (bytes_written != size) {
				KWARN("Asset bytes written/size mismatch: %llu/%llu", bytes_written, size);
			}

			filesystem_close(&f);

			return true;
		}
	}

	// New asset file, write out.
	KERROR("kpackage_asset_bytes_write attempted to write to an asset that is not in the manifest.");
	return false;
}

b8 kpackage_asset_bytes_write (kpackage *package, kname name, u64 size, const void *bytes) {
	if (!package || !name || !size || !bytes) {
		KERROR("kpackage_asset_bytes_write requires valid pointers to package, name and bytes, and a nonzero size");
		return false;
	}

	if (package->is_binary) {
		// FIXME: do the thing.
		KASSERT_MSG(false, "not yet supported");
		return false;
	}

	if (!kpackage_asset_write_file_internal(package, name, size, bytes, true)) {
		KERROR("Failed to write asset.");
		return false;
	}

	return true;
}

b8 kpackage_asset_text_write (kpackage *package, kname name, u64 size, const char *text) {
	if (!package || !name || !size || !text) {
		KERROR("kpackage_asset_text_write requires valid pointers to package, name and bytes, and a nonzero size");
		return false;
	}

	if (package->is_binary) {
		// FIXME: do the thing.
		KASSERT_MSG(false, "not yet supported");
		return false;
	}

	if (!kpackage_asset_write_file_internal(package, name, size, (void *)text, false)) {
		KERROR("Failed to write asset.");
		return false;
	}

	return true;
}

b8 kpackage_parse_manifest_file_content (const char *path, asset_manifest *out_manifest) {
	if (!path || !out_manifest) {
		KERROR("kpackage_parse_manifest_file_content requires valid pointers to path and out_manifest, ya dingus!");
		return false;
	}

	b8 success = false;
	const char *file_content = filesystem_read_entire_text_file(path);
	if (!file_content) {
		KERROR("Failed to load asset manifest '%s'.", path);
		return false;
	}

	// Parse manifest
	kson_tree tree;
	if (!kson_tree_from_string(file_content, &tree)) {
		KERROR("Failed to parse asset manifest file '%s'. See logs for details.", path);
		return false;
	}

	// Extract properties from file.
	if (!kson_object_property_value_get_string_as_kname(&tree.root, "package_name", &out_manifest->name)) {
		KERROR("Asset manifest format - 'package_name' is required but not found.");
		goto kpackage_parse_cleanup;
	}

	// Take a copy of the file path.
	out_manifest->file_path = string_duplicate(path);

	// Take a copy of the directory to the file path.
	out_manifest->path = string_directory_from_path(path);

	// Process references.
	kson_array references = {0};
	b8 contains_references = kson_object_property_value_get_array(&tree.root, "references", &references);
	if (contains_references) {
		u32 reference_array_count = 0;
		if (!kson_array_element_count_get(&references, &reference_array_count)) {
			KWARN("Failed to get array count for references. Skipping.");
		} else {

			// Stand up a darray for references.
			out_manifest->references = darray_create(asset_manifest_reference);

			for (u32 i = 0; i < reference_array_count; ++i) {
				kson_object ref_obj = {0};
				if (!kson_array_element_value_get_object(&references, i, &ref_obj)) {
					KWARN("Failed to get object at array index %u. Skipping.", i);
					continue;
				}

				asset_manifest_reference ref = {0};

				// Reference name.
				const char *ref_name;
				if (!kson_object_property_value_get_string(&ref_obj, "name", &ref_name)) {
					KWARN("Failed to get reference name at array index %u. Skipping.", i);
					string_free(ref_name);
					continue;
				}
				ref.name = kname_create(ref_name);
				string_free(ref_name);

				// Reference path.
				if (!kson_object_property_value_get_string(&ref_obj, "path", &ref.path)) {
					KWARN("Failed to get reference path at array index %u. Skipping.", i);
					continue;
				}

				// Add to references
				darray_push(out_manifest->references, ref);
			}
		}
	}

	// Process assets.
	kson_array assets = {0};
	b8 contains_assets = kson_object_property_value_get_array(&tree.root, "assets", &assets);
	if (contains_assets) {
		u32 asset_array_count = 0;
		if (!kson_array_element_count_get(&assets, &asset_array_count)) {
			KWARN("Failed to get array count for assets. Skipping.");
		} else {

			// Stand up a darray for assets.
			out_manifest->assets = darray_create(asset_manifest_asset);

			for (u32 i = 0; i < asset_array_count; ++i) {
				kson_object asset_obj = {0};
				if (!kson_array_element_value_get_object(&assets, i, &asset_obj)) {
					KWARN("Failed to get object at array index %u. Skipping.", i);
					continue;
				}

				asset_manifest_asset asset = {0};
				// Asset name.
				if (!kson_object_property_value_get_string_as_kname(&asset_obj, "name", &asset.name)) {
					KWARN("Failed to get asset name at array index %u. Skipping.", i);
					continue;
				}

				const char *type_str;
				if (!kson_object_property_value_get_string(&asset_obj, "type", &type_str)) {
					KWARN("Failed to get asset type at array index %u. Skipping.", i);
					continue;
				}
				asset.type = kasset_type_from_string(type_str);
				string_free(type_str);

				// Verify that the asset name doesn't already exist in the manifest.
				// TODO: This really only needs to be unique per asset type.
				u32 asset_count = darray_length(out_manifest->assets);
				for (u32 a = 0; a < asset_count; ++a) {
					if (out_manifest->assets[a].name == asset.name) {
						// A collision exists. This makes the manifest invalid, and
						// should fail the process completely.
						KERROR("Failed to process asset manifest for package '%s'. An asset named '%k' already exists.", kname_string_get(out_manifest->name), asset.name);
						return false;
					}
				}

				// Path
				if (!kson_object_property_value_get_string(&asset_obj, "path", &asset.local_path)) {
					KWARN("Failed to get asset path at array index %u. Skipping.", i);
					continue;
				}
				// Full path of the asset.
				asset.path = string_format("%s/%s", out_manifest->path, asset.local_path);

				// Source Path - optional
				if (kson_object_property_value_get_string(&asset_obj, "source_path", &asset.local_source_path)) {
					// Full source path of the asset.
					asset.source_path = string_format("%s/%s", out_manifest->path, asset.local_source_path);
				}

				// Add to assets
				darray_push(out_manifest->assets, asset);
			}
		}
	}

	success = true;
kpackage_parse_cleanup:
	if (file_content) {
		string_free(file_content);
	}
	kson_tree_cleanup(&tree);
	if (!success) {
		// Clean up manifest
		if (out_manifest->references) {
			u32 ref_count = darray_length(out_manifest->references);
			for (u32 i = 0; i < ref_count; ++i) {
				string_free(out_manifest->references[i].path);
			}
			darray_destroy(out_manifest->references);
			out_manifest->references = 0;
		}
	}
	return success;
}

void kpackage_manifest_destroy (asset_manifest *manifest) {
	if (manifest) {
		string_free(manifest->path);
		string_free(manifest->file_path);
		if (manifest->references) {
			u32 ref_count = darray_length(manifest->references);
			for (u32 i = 0; i < ref_count; ++i) {
				asset_manifest_reference *ref = &manifest->references[i];
				string_free(ref->path);
			}
			darray_destroy(manifest->references);
		}

		if (manifest->assets) {
			u32 ref_count = darray_length(manifest->assets);
			for (u32 i = 0; i < ref_count; ++i) {
				asset_manifest_asset *asset = &manifest->assets[i];
				string_free(asset->path);
				string_free(asset->source_path);
				string_free(asset->local_path);
				string_free(asset->local_source_path);
			}
			darray_destroy(manifest->assets);
		}
		kzero_memory(manifest, sizeof(asset_manifest));
	}
}

#if KOHI_DEBUG
b8 kpackage_add_asset (kpackage *package, const asset_manifest_asset *asset) {
	// Add to internal asset list for the package.
	asset_entry new_entry = {
		.name = asset->name,
		.type = asset->type,
		.path = string_duplicate(asset->path),
		.source_path = string_duplicate(asset->path),
		.local_path = string_duplicate(asset->local_path),
		.local_source_path = string_duplicate(asset->local_source_path),
	};

	// Push the asset to it.
	darray_push(package->internal_data->entries, new_entry);

	return true;
}

b8 kpackage_save (kpackage *package) {
	// Create an asset manifest struct from the package
	u32 asset_count = darray_length(package->internal_data->entries);
	asset_manifest manifest = {
		.path = KNULL, // FIXME: get stored path
		.references = package->internal_data->references,
		.file_path = package->manifest_file_path,
		.name = package->name,
		.assets = KALLOC_TYPE_CARRAY(asset_manifest_asset, asset_count)};
	for (u32 i = 0; i < asset_count; ++i) {
		asset_entry *entry = &package->internal_data->entries[i];
		asset_manifest_asset *a = &manifest.assets[i];

		a->name = entry->name;
		a->path = entry->path;
		a->source_path = entry->source_path;
		a->type = entry->type;
		a->local_path = entry->local_path;
		a->local_source_path = entry->local_source_path;
	}

	// Serialize it
	kson_tree tree = {
		.root = kson_object_create()};
	kson_object_value_add_kname_as_string(&tree.root, "package_name", manifest.name);

	// references
	kson_array refs = kson_array_create();
	u32 ref_count = darray_length(package->internal_data->references);
	for (u32 i = 0; i < ref_count; ++i) {
		kson_object ref_obj = kson_object_create();
		kson_object_value_add_kname_as_string(&ref_obj, "name", manifest.references[i].name);
		kson_object_value_add_string(&ref_obj, "path", manifest.references[i].path);
		kson_array_value_add_object(&refs, ref_obj);
	}
	kson_object_value_add_array(&tree.root, "references", refs);

	kson_array assets = kson_array_create();
	for (u32 i = 0; i < asset_count; ++i) {
		kson_object asset_obj = kson_object_create();
		asset_manifest_asset *ma = &manifest.assets[i];
		kson_object_value_add_kname_as_string(&asset_obj, "name", ma->name);

		const char *type_str = kasset_type_to_string(ma->type);
		kson_object_value_add_string(&asset_obj, "type", type_str);
		string_free(type_str);

		// Use the paths relative to the package.
		kson_object_value_add_string(&asset_obj, "path", ma->local_path);
		if (ma->local_source_path) {
			kson_object_value_add_string(&asset_obj, "source_path", ma->local_source_path);
		}

		kson_array_value_add_object(&assets, asset_obj);
	}
	kson_object_value_add_array(&tree.root, "assets", assets);

	const char *serialized = kson_tree_to_string(&tree);

	kson_tree_cleanup(&tree);

	/* KTRACE("Serialized: \n%s", serialized); */

	// Write it to disk.
	if (!filesystem_write_entire_text_file(package->manifest_file_path, serialized)) {
		KERROR("Failed to write asset manifest. See logs for details");
		return false;
	}

	return true;
}
#endif
