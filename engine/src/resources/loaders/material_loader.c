#include "material_loader.h"

#include "containers/darray.h"
#include "core/kmemory.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "loader_utils.h"
#include "math/kmath.h"
#include "resources/resource_types.h"
#include "systems/resource_system.h"

#include "platform/filesystem.h"

static material_map material_map_create_default(const char *name,
                                                const char *texture_name) {

  material_map new_map = {};
  // Set reasonable defaults for old material types.
  new_map.name = string_duplicate(name);
  new_map.repeat_u = new_map.repeat_v = new_map.repeat_w =
      TEXTURE_REPEAT_REPEAT;
  new_map.filter_min = new_map.filter_max = TEXTURE_FILTER_MODE_LINEAR;
  new_map.texture_name = string_duplicate(texture_name);
  return new_map;
}

static material_config_prop
material_config_prop_create(const char *name, shader_uniform_type type,
                            const char *value) {
  material_config_prop prop = {};
  prop.name = string_duplicate(name);
  switch (type) {

  case SHADER_UNIFORM_TYPE_FLOAT32:
    string_to_f32(value, &prop.value_f32);
    break;
  case SHADER_UNIFORM_TYPE_FLOAT32_2:
    string_to_vec2(value, &prop.value_v2);
    break;
  case SHADER_UNIFORM_TYPE_FLOAT32_3:
    string_to_vec3(value, &prop.value_v3);
    break;
  case SHADER_UNIFORM_TYPE_FLOAT32_4:
    string_to_vec4(value, &prop.value_v4);
    break;
  case SHADER_UNIFORM_TYPE_INT8:
    string_to_i8(value, &prop.value_i8);
    break;
  case SHADER_UNIFORM_TYPE_UINT8:
    string_to_u8(value, &prop.value_u8);
    break;
  case SHADER_UNIFORM_TYPE_INT16:
    string_to_i16(value, &prop.value_i16);
    break;
  case SHADER_UNIFORM_TYPE_UINT16:
    string_to_u16(value, &prop.value_u16);
    break;
  case SHADER_UNIFORM_TYPE_INT32:
    string_to_i32(value, &prop.value_i32);
    break;
  case SHADER_UNIFORM_TYPE_UINT32:
    string_to_u32(value, &prop.value_u32);
    break;
  case SHADER_UNIFORM_TYPE_MATRIX_4:
    // TODO: string_to_mat4
    KERROR("Material property type mat4 not supported.");
    break;
  case SHADER_UNIFORM_TYPE_SAMPLER:
  case SHADER_UNIFORM_TYPE_CUSTOM:
  default:
    KERROR("Unsupported material property type.");
    break;
  }
  return prop;
}

static b8 material_loader_load(struct resource_loader *self, const char *name,
                               void *params, resource *out_resource) {
  if (!self || !name || !out_resource) {
    return false;
  }

  char *format_str = "%s/%s/%s%s";
  char full_file_path[512];
  string_format(full_file_path, format_str, resource_system_base_path(),
                self->type_path, name, ".kmt");

  file_handle f;
  if (!filesystem_open(full_file_path, FILE_MODE_READ, false, &f)) {
    KERROR("material_loader_load - unable to open material file for reading: "
           "'%s'.",
           full_file_path);
    return false;
  }

  out_resource->full_path = string_duplicate(full_file_path);

  material_config *resource_data =
      kallocate(sizeof(material_config), MEMORY_TAG_RESOURCE);
  // Set some defaults.
  resource_data->shader_name = "Builtin.Material"; // Default material.
  resource_data->auto_release = true;
  resource_data->map_count = 0;
  resource_data->maps = darray_create(material_map);
  resource_data->property_count = 0;
  resource_data->properties = darray_create(material_config_prop);
  string_ncopy(resource_data->name, name, MATERIAL_NAME_MAX_LENGTH);

  // Read each line of the file.
  char line_buf[512] = "";
  char *p = &line_buf[0];
  u64 line_length = 0;
  u32 line_number = 1;
  while (filesystem_read_line(&f, 511, &p, &line_length)) {
    // Trim the string.
    char *trimmed = string_trim(line_buf);

    // Get the trimmed length.
    line_length = string_length(trimmed);

    // Skip blank lines and comments.
    if (line_length < 1 || trimmed[0] == '#') {
      line_number++;
      continue;
    }

    // Split into var/value
    i32 equal_index = string_index_of(trimmed, '=');
    if (equal_index == -1) {
      KWARN("Potential formatting issue found in file '%s': '=' token not "
            "found. Skipping line %ui.",
            full_file_path, line_number);
      line_number++;
      continue;
    }

    // Assume a max of 64 characters for the variable name.
    char raw_var_name[64];
    kzero_memory(raw_var_name, sizeof(char) * 64);
    string_mid(raw_var_name, trimmed, 0, equal_index);
    char *trimmed_var_name = string_trim(raw_var_name);

    // Assume a max of 511-65 (446) for the max length of the value to account
    // for the variable name and the '='.
    char raw_value[446];
    kzero_memory(raw_value, sizeof(char) * 446);
    string_mid(raw_value, trimmed, equal_index + 1,
               -1); // Read the rest of the line
    char *trimmed_value = string_trim(raw_value);

    u8 version = 0;

    // Process the variable.
    if (strings_equali(trimmed_var_name, "version")) {
      if (!string_to_u8(trimmed_value, &version)) {
        KERROR("Format error: failed to parse version. Aborting.");
        return false; // TODO: cleanup memory.
      }
    } else if (strings_equali(trimmed_var_name, "name")) {
      string_ncopy(resource_data->name, trimmed_value,
                   MATERIAL_NAME_MAX_LENGTH);
    } else if (strings_equali(trimmed_var_name, "diffuse_map_name")) {
      if (version == 1) {
        material_map new_map =
            material_map_create_default("diffuse", trimmed_value);
        darray_push(resource_data->maps, &new_map);
        resource_data->map_count++;
      } else {
        KERROR("Format error: unexpected variable 'diffuse_map_name', this "
               "should only exist for version 1 materials. Ignored.");
      }
    } else if (strings_equali(trimmed_var_name, "specular_map_name")) {
      if (version == 1) {
        material_map new_map =
            material_map_create_default("normal", trimmed_value);
        darray_push(resource_data->maps, &new_map);
        resource_data->map_count++;
      } else {
        KERROR("Format error: unexpected variable 'diffuse_map_name', this "
               "should only exist for version 1 materials. Ignored.");
      }
    } else if (strings_equali(trimmed_var_name, "normal_map_name")) {
      if (version == 1) {
        material_map new_map =
            material_map_create_default("specular", trimmed_value);
        darray_push(resource_data->maps, &new_map);
        resource_data->map_count++;
      } else {
        KERROR("Format error: unexpected variable 'diffuse_map_name', this "
               "should only exist for version 1 materials. Ignored.");
      }
    } else if (strings_equali(trimmed_var_name, "diffuse_colour")) {
      if (version == 1) {
        material_config_prop new_prop = material_config_prop_create(
            "diffuse_colour", SHADER_UNIFORM_TYPE_FLOAT32_4, trimmed_value);
        darray_push(resource_data->properties, &new_prop);
        resource_data->property_count++;
      } else {
        KERROR("Format error: unexpected variable 'diffuse_colour', this "
               "should only exist for version 1 materials. Ignored.");
      }
    } else if (strings_equali(trimmed_var_name, "shader")) {
      // Take a copy of the material name.
      resource_data->shader_name = string_duplicate(trimmed_value);
    } else if (strings_equali(trimmed_var_name, "shininess")) {
      if (version == 1) {
        material_config_prop new_prop = material_config_prop_create(
            "shininess", SHADER_UNIFORM_TYPE_FLOAT32, trimmed_value);
        darray_push(resource_data->properties, &new_prop);
        resource_data->property_count++;
      } else {
        KERROR("Format error: unexpected variable 'shininess', this "
               "should only exist for version 1 materials. Ignored.");
      }
    } else if (strings_equali(trimmed_var_name, "type")) {
      if (version >= 2) {
        if (strings_equali(trimmed_value, "phong")) {
          resource_data->type = MATERIAL_TYPE_PHONG;
        } else if (strings_equali(trimmed_value, "pbr")) {
          resource_data->type = MATERIAL_TYPE_PBR;
        } else if (strings_equali(trimmed_value, "custom")) {
          resource_data->type = MATERIAL_TYPE_CUSTOM;
        } else {
          KERROR("Format error: Unexpected material type '%s'", trimmed_value);
        }
      } else {
        KERROR("Format error: Unexpected variable 'type', this should only "
               "exist for version 2+ materials.");
      }
    }

    // LEFTOFF: Parse custom material properties and get them loaded up into the
    // material config. Then tie these into the materials themselves, followed
    // by the material system.

    // TODO: more fields.

    // Clear the line buffer.
    kzero_memory(line_buf, sizeof(char) * 512);
    line_number++;
  }

  filesystem_close(&f);

  out_resource->data = resource_data;
  out_resource->data_size = sizeof(material_config);
  out_resource->name = name;

  return true;
}

static void material_loader_unload(struct resource_loader *self,
                                   resource *resource) {
  if (!resource_unload(self, resource, MEMORY_TAG_RESOURCE)) {
    KWARN("material_loader_unload called with nullptr for self or resource.");
  }
}

resource_loader material_resource_loader_create(void) {
  resource_loader loader;
  loader.type = RESOURCE_TYPE_MATERIAL;
  loader.custom_type = 0;
  loader.load = material_loader_load;
  loader.unload = material_loader_unload;
  loader.type_path = "materials";

  return loader;
}
