#include "render_type_utils.h"

#include "core_render_types.h"
#include "debug/kassert.h"
#include "logger.h"
#include "strings/kstring.h"

const char* texture_repeat_to_string(texture_repeat repeat) {
    switch (repeat) {
    case TEXTURE_REPEAT_REPEAT:
        return "repeat";
    case TEXTURE_REPEAT_CLAMP_TO_EDGE:
        return "clamp_to_edge";
    case TEXTURE_REPEAT_CLAMP_TO_BORDER:
        return "clamp_to_border";
    case TEXTURE_REPEAT_MIRRORED_REPEAT:
        return "mirrored_repeat";
    default:
        KASSERT_MSG(false, "Unrecognized texture repeat.");
        return 0;
    }
}

const char* texture_filter_mode_to_string(texture_filter filter) {
    switch (filter) {
    case TEXTURE_FILTER_MODE_LINEAR:
        return "linear";
    case TEXTURE_FILTER_MODE_NEAREST:
        return "nearest";
    default:
        KASSERT_MSG(false, "Unrecognized texture filter type.");
        return 0;
    }
}

const char* shader_uniform_type_to_string(shader_uniform_type type) {
    switch (type) {
    case SHADER_UNIFORM_TYPE_FLOAT32:
        return "f32";
    case SHADER_UNIFORM_TYPE_FLOAT32_2:
        return "vec2";
    case SHADER_UNIFORM_TYPE_FLOAT32_3:
        return "vec3";
    case SHADER_UNIFORM_TYPE_FLOAT32_4:
        return "vec4";
    case SHADER_UNIFORM_TYPE_INT8:
        return "i8";
    case SHADER_UNIFORM_TYPE_INT16:
        return "i16";
    case SHADER_UNIFORM_TYPE_INT32:
        return "i32";
    case SHADER_UNIFORM_TYPE_UINT8:
        return "u8";
    case SHADER_UNIFORM_TYPE_UINT16:
        return "u16";
    case SHADER_UNIFORM_TYPE_UINT32:
        return "u32";
    case SHADER_UNIFORM_TYPE_MATRIX_4:
        return "mat4";
    case SHADER_UNIFORM_TYPE_TEXTURE_1D:
        return "texture1d";
    case SHADER_UNIFORM_TYPE_TEXTURE_2D:
        return "texture2d";
    case SHADER_UNIFORM_TYPE_TEXTURE_3D:
        return "texture3d";
    case SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY:
        return "texture1dArray";
    case SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY:
        return "texture2dArray";
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE:
        return "textureCube";
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY:
        return "textureCubeArray";
    case SHADER_UNIFORM_TYPE_SAMPLER:
        return "sampler";
    case SHADER_UNIFORM_TYPE_CUSTOM:
        return "custom";
    default:
        KASSERT_MSG(false, "Unrecognized uniform type.");
        return 0;
    }
}

const char* shader_attribute_type_to_string(shader_attribute_type type) {
    switch (type) {
    case SHADER_ATTRIB_TYPE_FLOAT32:
        return "f32";
    case SHADER_ATTRIB_TYPE_FLOAT32_2:
        return "vec2";
    case SHADER_ATTRIB_TYPE_FLOAT32_3:
        return "vec3";
    case SHADER_ATTRIB_TYPE_FLOAT32_4:
        return "vec4";
    case SHADER_ATTRIB_TYPE_MATRIX_4:
        return "mat4";
    case SHADER_ATTRIB_TYPE_INT8:
        return "i8";
    case SHADER_ATTRIB_TYPE_UINT8:
        return "u8";
    case SHADER_ATTRIB_TYPE_INT16:
        return "i16";
    case SHADER_ATTRIB_TYPE_UINT16:
        return "u16";
    case SHADER_ATTRIB_TYPE_INT32:
        return "i32";
    case SHADER_ATTRIB_TYPE_UINT32:
        return "u32";
    }
}

const char* shader_stage_to_string(shader_stage stage) {
    switch (stage) {
    case SHADER_STAGE_VERTEX:
        return "vertex";
    case SHADER_STAGE_GEOMETRY:
        return "geometry";
    case SHADER_STAGE_FRAGMENT:
        return "fragment";
    case SHADER_STAGE_COMPUTE:
        return "compute";
    default:
        return "";
    }
}

const char* shader_scope_to_string(shader_update_frequency frequency) {
    switch (frequency) {
    case SHADER_UPDATE_FREQUENCY_PER_FRAME:
        return "frame";
    case SHADER_UPDATE_FREQUENCY_PER_GROUP:
        return "group";
    case SHADER_UPDATE_FREQUENCY_PER_DRAW:
        return "draw";
    }
}

texture_repeat string_to_texture_repeat(const char* str) {
    if (strings_equali("repeat", str)) {
        return TEXTURE_REPEAT_REPEAT;
    } else if (strings_equali("clamp_to_edge", str)) {
        return TEXTURE_REPEAT_CLAMP_TO_EDGE;
    } else if (strings_equali("clamp_to_border", str)) {
        return TEXTURE_REPEAT_CLAMP_TO_BORDER;
    } else if (strings_equali("mirrored_repeat", str)) {
        return TEXTURE_REPEAT_MIRRORED_REPEAT;
    } else {
        KASSERT_MSG(false, "Unrecognized texture repeat.");
        return TEXTURE_REPEAT_REPEAT;
    }
}

texture_filter string_to_texture_filter_mode(const char* str) {
    if (strings_equali("linear", str)) {
        return TEXTURE_FILTER_MODE_LINEAR;
    } else if (strings_equali("nearest", str)) {
        return TEXTURE_FILTER_MODE_LINEAR;
    } else {
        KASSERT_MSG(false, "Unrecognized texture filter type.");
        return TEXTURE_FILTER_MODE_LINEAR;
    }
}

shader_uniform_type string_to_shader_uniform_type(const char* str) {
    if (strings_equali("f32", str)) {
        return SHADER_UNIFORM_TYPE_FLOAT32;
    } else if (strings_equali("vec2", str)) {
        return SHADER_UNIFORM_TYPE_FLOAT32_2;
    } else if (strings_equali("vec3", str)) {
        return SHADER_UNIFORM_TYPE_FLOAT32_3;
    } else if (strings_equali("vec4", str)) {
        return SHADER_UNIFORM_TYPE_FLOAT32_4;
    } else if (strings_equali("i8", str)) {
        return SHADER_UNIFORM_TYPE_INT8;
    } else if (strings_equali("i16", str)) {
        return SHADER_UNIFORM_TYPE_INT16;
    } else if (strings_equali("i32", str)) {
        return SHADER_UNIFORM_TYPE_INT32;
    } else if (strings_equali("u8", str)) {
        return SHADER_UNIFORM_TYPE_UINT8;
    } else if (strings_equali("u16", str)) {
        return SHADER_UNIFORM_TYPE_UINT16;
    } else if (strings_equali("u32", str)) {
        return SHADER_UNIFORM_TYPE_UINT32;
    } else if (strings_equali("mat4", str)) {
        return SHADER_UNIFORM_TYPE_MATRIX_4;
    } else if (strings_equali("sampler1d", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_1D;
    } else if (strings_equali("texture2d", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_2D;
    } else if (strings_equali("texture3d", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_3D;
    } else if (strings_equali("texture1dArray", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY;
    } else if (strings_equali("texture2dArray", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY;
    } else if (strings_equali("textureCube", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_CUBE;
    } else if (strings_equali("textureCubeArray", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY;
    } else if (strings_equali("sampler", str)) {
        return SHADER_UNIFORM_TYPE_SAMPLER;
    } else if (strings_equali("custom", str)) {
        return SHADER_UNIFORM_TYPE_CUSTOM;
    } else {
        KASSERT_MSG(false, "Unrecognized uniform type.");
        return SHADER_UNIFORM_TYPE_FLOAT32;
    }
}

shader_attribute_type string_to_shader_attribute_type(const char* str) {
    if (strings_equali("f32", str) || strings_equali("float", str)) {
        return SHADER_ATTRIB_TYPE_FLOAT32;
    } else if (strings_equali("vec2", str)) {
        return SHADER_ATTRIB_TYPE_FLOAT32_2;
    } else if (strings_equali("vec3", str)) {
        return SHADER_ATTRIB_TYPE_FLOAT32_3;
    } else if (strings_equali("vec4", str)) {
        return SHADER_ATTRIB_TYPE_FLOAT32_4;
    } else if (strings_equali("mat4", str)) {
        return SHADER_ATTRIB_TYPE_MATRIX_4;
    } else if (strings_equali("i8", str)) {
        return SHADER_ATTRIB_TYPE_INT8;
    } else if (strings_equali("u8", str)) {
        return SHADER_ATTRIB_TYPE_UINT8;
    } else if (strings_equali("i16", str)) {
        return SHADER_ATTRIB_TYPE_INT16;
    } else if (strings_equali("u16", str)) {
        return SHADER_ATTRIB_TYPE_UINT16;
    } else if (strings_equali("i32", str) || strings_equali("int", str)) {
        return SHADER_ATTRIB_TYPE_INT32;
    } else if (strings_equali("u32", str)) {
        return SHADER_ATTRIB_TYPE_UINT32;
    } else {
        KERROR("Unrecognized attribute type '%s'. Defaulting to i32", str);
        return SHADER_ATTRIB_TYPE_INT32;
    }
}

shader_stage string_to_shader_stage(const char* str) {
    if (strings_equali("vertex", str) || strings_equali("vert", str)) {
        return SHADER_STAGE_VERTEX;
    } else if (strings_equali("geometry", str) || strings_equali("geom", str)) {
        return SHADER_STAGE_GEOMETRY;
    } else if (strings_equali("fragment", str) || strings_equali("frag", str)) {
        return SHADER_STAGE_FRAGMENT;
    } else if (strings_equali("compute", str) || strings_equali("comp", str)) {
        return SHADER_STAGE_COMPUTE;
    } else {
        KERROR("Unknown shader stage '%s'. Defaulting to vertex.", str);
        return SHADER_STAGE_VERTEX;
    }
}

shader_update_frequency string_to_shader_scope(const char* str) {
    if (strings_equali("frame", str)) {
        return SHADER_UPDATE_FREQUENCY_PER_FRAME;
    } else if (strings_equali("group", str)) {
        return SHADER_UPDATE_FREQUENCY_PER_GROUP;
    } else if (strings_equali("draw", str)) {
        return SHADER_UPDATE_FREQUENCY_PER_DRAW;
    } else {
        KERROR("Unknown shader scope '%s'. Defaulting to per-frame.", str);
        return SHADER_UPDATE_FREQUENCY_PER_FRAME;
    }
}
