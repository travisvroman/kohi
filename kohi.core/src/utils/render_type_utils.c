#include "render_type_utils.h"

#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

b8 uniform_type_is_sampler(shader_uniform_type type) {
    switch (type) {
    case SHADER_UNIFORM_TYPE_SAMPLER:
        return true;
    default:
        return false;
    }
}

b8 uniform_type_is_texture(shader_uniform_type type) {
    switch (type) {
    case SHADER_UNIFORM_TYPE_TEXTURE_1D:
    case SHADER_UNIFORM_TYPE_TEXTURE_2D:
    case SHADER_UNIFORM_TYPE_TEXTURE_3D:
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE:
    case SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY:
    case SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY:
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY:
        return true;
    default:
        return false;
    }
}

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
        KERROR("Unrecognized texture repeat '%s'. Defaulting to TEXTURE_REPEAT_REPEAT", str);
        return TEXTURE_REPEAT_REPEAT;
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

texture_filter string_to_texture_filter_mode(const char* str) {
    if (strings_equali("linear", str)) {
        return TEXTURE_FILTER_MODE_LINEAR;
    } else if (strings_equali("nearest", str)) {
        return TEXTURE_FILTER_MODE_LINEAR;
    } else {
        KERROR("Unrecognized texture filter type '%s'. Defaulting to TEXTURE_FILTER_MODE_LINEAR.", str);
        return TEXTURE_FILTER_MODE_LINEAR;
    }
}

const char* texture_channel_to_string(texture_channel channel) {
    switch (channel) {
    default:
    case TEXTURE_CHANNEL_R:
        return "r";
    case TEXTURE_CHANNEL_G:
        return "g";
    case TEXTURE_CHANNEL_B:
        return "b";
    case TEXTURE_CHANNEL_A:
        return "a";
    }
}

texture_channel string_to_texture_channel(const char* str) {
    if (strings_equali(str, "r")) {
        return TEXTURE_CHANNEL_R;
    } else if (strings_equali(str, "g")) {
        return TEXTURE_CHANNEL_G;
    } else if (strings_equali(str, "b")) {
        return TEXTURE_CHANNEL_B;
    } else if (strings_equali(str, "a")) {
        return TEXTURE_CHANNEL_A;
    } else {
        KERROR("Texture channel not supported: '%s'. Defaulting to TEXTURE_CHANNEL_R.", str);
        return TEXTURE_CHANNEL_R;
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
    case SHADER_UNIFORM_TYPE_STRUCT:
        return "struct";
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
    } else if (strings_equali("texture1d", str)) {
        return SHADER_UNIFORM_TYPE_TEXTURE_1D;
    } else if (strings_equali("texture2d", str) || strings_equali("texture", str)) {
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
    } else if (string_starts_withi(str, "struct")) {
        return SHADER_UNIFORM_TYPE_STRUCT;
    } else if (strings_equali("sampler", str)) {
        return SHADER_UNIFORM_TYPE_SAMPLER;
    } else if (strings_equali("custom", str)) {
        return SHADER_UNIFORM_TYPE_CUSTOM;
    } else {
        KERROR("Unrecognized uniform type '%s'. Defaulting to float.", str);
        return SHADER_UNIFORM_TYPE_FLOAT32;
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

const char* shader_update_frequency_to_string(shader_update_frequency frequency) {
    switch (frequency) {
    case SHADER_UPDATE_FREQUENCY_PER_FRAME:
        return "frame";
    case SHADER_UPDATE_FREQUENCY_PER_GROUP:
        return "group";
    case SHADER_UPDATE_FREQUENCY_PER_DRAW:
        return "draw";
    }
}

shader_update_frequency string_to_shader_update_frequency(const char* str) {
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

const char* face_cull_mode_to_string(face_cull_mode mode) {
    switch (mode) {
    default:
    case FACE_CULL_MODE_NONE:
        return "none";
    case FACE_CULL_MODE_FRONT:
        return "front";
    case FACE_CULL_MODE_BACK:
        return "back";
    case FACE_CULL_MODE_FRONT_AND_BACK:
        return "front_and_back";
    }
}

face_cull_mode string_to_face_cull_mode(const char* str) {
    if (strings_equali(str, "front")) {
        return FACE_CULL_MODE_FRONT;
    } else if (strings_equali(str, "back")) {
        return FACE_CULL_MODE_BACK;
    } else if (strings_equali(str, "front_and_back")) {
        return FACE_CULL_MODE_FRONT_AND_BACK;
    } else if (strings_equali(str, "none")) {
        return FACE_CULL_MODE_NONE;
    } else {
        KERROR("Unknown face cull mode '%s'. Defaulting to FACE_CULL_MODE_NONE.", str);
        return FACE_CULL_MODE_NONE;
    }
}

const char* topology_type_to_string(primitive_topology_type_bits type) {
    switch (type) {
    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT:
        return "triangle_list";
    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT:
        return "triangle_strip";
    case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT:
        return "triangle_fan";
    case PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT:
        return "line_list";
    case PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT:
        return "line_strip";
    case PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT:
        return "point_list";
    default:
    case PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT:
        return "none";
    }
}

primitive_topology_type_bits string_to_topology_type(const char* str) {

    if (strings_equali(str, "triangle_list")) {
        return PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
    } else if (strings_equali(str, "triangle_strip")) {
        return PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_STRIP_BIT;
    } else if (strings_equali(str, "triangle_fan")) {
        return PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_FAN_BIT;
    } else if (strings_equali(str, "line_list")) {
        return PRIMITIVE_TOPOLOGY_TYPE_LINE_LIST_BIT;
    } else if (strings_equali(str, "line_strip")) {
        return PRIMITIVE_TOPOLOGY_TYPE_LINE_STRIP_BIT;
    } else if (strings_equali(str, "point_list")) {
        return PRIMITIVE_TOPOLOGY_TYPE_POINT_LIST_BIT;
    } else if (strings_equali(str, "none")) {
        return PRIMITIVE_TOPOLOGY_TYPE_NONE_BIT;
    } else {
        KERROR("Unrecognized topology type '%s'. Returning default of triangle_list.", str);
        return PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE_LIST_BIT;
    }
}

u16 size_from_shader_attribute_type(shader_attribute_type type) {
    switch (type) {
    case SHADER_ATTRIB_TYPE_FLOAT32:
        return 4;
    case SHADER_ATTRIB_TYPE_FLOAT32_2:
        return 8;
    case SHADER_ATTRIB_TYPE_FLOAT32_3:
        return 12;
    case SHADER_ATTRIB_TYPE_FLOAT32_4:
        return 16;
    case SHADER_ATTRIB_TYPE_UINT8:
        return 1;
    case SHADER_ATTRIB_TYPE_UINT16:
        return 2;
    case SHADER_ATTRIB_TYPE_UINT32:
        return 4;
    case SHADER_ATTRIB_TYPE_INT8:
        return 1;
    case SHADER_ATTRIB_TYPE_INT16:
        return 2;
    case SHADER_ATTRIB_TYPE_INT32:
        return 4;
    case SHADER_ATTRIB_TYPE_MATRIX_4:
        return 64;
    default:
        KFATAL("Attribute type not handled. Check enums.");
        return 0;
    }
}

u16 size_from_shader_uniform_type(shader_uniform_type type) {
    switch (type) {
    case SHADER_UNIFORM_TYPE_FLOAT32:
        return 4;
    case SHADER_UNIFORM_TYPE_FLOAT32_2:
        return 8;
    case SHADER_UNIFORM_TYPE_FLOAT32_3:
        return 12;
    case SHADER_UNIFORM_TYPE_FLOAT32_4:
        return 16;
    case SHADER_UNIFORM_TYPE_UINT8:
        return 1;
    case SHADER_UNIFORM_TYPE_UINT16:
        return 2;
    case SHADER_UNIFORM_TYPE_UINT32:
        return 4;
    case SHADER_UNIFORM_TYPE_INT8:
        return 1;
    case SHADER_UNIFORM_TYPE_INT16:
        return 2;
    case SHADER_UNIFORM_TYPE_INT32:
        return 4;
    case SHADER_UNIFORM_TYPE_MATRIX_4:
        return 64;
    case SHADER_UNIFORM_TYPE_STRUCT:
    case SHADER_UNIFORM_TYPE_CUSTOM:
        KERROR("size_from_shader_uniform_type(): Uniform size cannot be extracted directly from struct or custom types. 0 will be returned.");
        return 0;
    case SHADER_UNIFORM_TYPE_TEXTURE_1D:
    case SHADER_UNIFORM_TYPE_TEXTURE_2D:
    case SHADER_UNIFORM_TYPE_TEXTURE_3D:
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE:
    case SHADER_UNIFORM_TYPE_TEXTURE_1D_ARRAY:
    case SHADER_UNIFORM_TYPE_TEXTURE_2D_ARRAY:
    case SHADER_UNIFORM_TYPE_TEXTURE_CUBE_ARRAY:
    case SHADER_UNIFORM_TYPE_SAMPLER:
        // These don't occupy any "space", so return 0.
        return 0;
        break;
    default:
        KFATAL("Uniform type not handled. Check enums.");
        return 0;
    }
}

#define PX_ALPHA_LESS_THAN_MAX(type, array, array_size, alpha_max, channel_count, alpha_index) \
    {                                                                                          \
        type* px = (type*)array;                                                               \
        for (u32 i = 0; i < array_size; ++i) {                                                 \
            type alpha = px[(sizeof(type) * channel_count * i) + alpha_index];                 \
            if (alpha < alpha_max) {                                                           \
                return true;                                                                   \
            }                                                                                  \
        }                                                                                      \
        return false;                                                                          \
    }

b8 pixel_data_has_transparency(const void* pixels, u32 pixel_array_size, kpixel_format format) {
    if (!pixels || !pixel_array_size) {
        return false;
    }

    switch (format) {
    case KPIXEL_FORMAT_UNKNOWN:
    default:
        KWARN("%s - Unknown pixel format provided. Cannot determine pixel transparency. Defaulting to false.", __FUNCTION__);
        return false;

    case KPIXEL_FORMAT_RGBA8: {
        PX_ALPHA_LESS_THAN_MAX(u8, pixels, pixel_array_size, U8_MAX, 4, 3);
    }
    case KPIXEL_FORMAT_RGBA16: {
        PX_ALPHA_LESS_THAN_MAX(u16, pixels, pixel_array_size, U16_MAX, 4, 3);
    }
    case KPIXEL_FORMAT_RGBA32: {
        PX_ALPHA_LESS_THAN_MAX(u32, pixels, pixel_array_size, U32_MAX, 4, 3);
    } break;

    case KPIXEL_FORMAT_RGB8:
    case KPIXEL_FORMAT_RG8:
    case KPIXEL_FORMAT_R8:
    case KPIXEL_FORMAT_RGB16:
    case KPIXEL_FORMAT_RG16:
    case KPIXEL_FORMAT_R16:
    case KPIXEL_FORMAT_RGB32:
    case KPIXEL_FORMAT_RG32:
    case KPIXEL_FORMAT_R32:
        // No alpha channel, return false.
        return false;
    }
}

u8 channel_count_from_pixel_format(kpixel_format format) {
    switch (format) {
    case KPIXEL_FORMAT_UNKNOWN:
    default:
        KWARN("%s - Unknown pixel format provided. Cannot determine channel count. Returning INVALID_ID_U8.", __FUNCTION__);
        return INVALID_ID_U8;
    case KPIXEL_FORMAT_RGBA8:
    case KPIXEL_FORMAT_RGBA16:
    case KPIXEL_FORMAT_RGBA32:
        return 4;
    case KPIXEL_FORMAT_RGB8:
    case KPIXEL_FORMAT_RGB16:
    case KPIXEL_FORMAT_RGB32:
        return 3;
    case KPIXEL_FORMAT_RG8:
    case KPIXEL_FORMAT_RG16:
    case KPIXEL_FORMAT_RG32:
        return 2;
    case KPIXEL_FORMAT_R8:
    case KPIXEL_FORMAT_R16:
    case KPIXEL_FORMAT_R32:
        return 1;
    }
}

b8 calculate_mip_levels_from_dimension(u32 width, u32 height) {
    // The number of mip levels is calculated by first taking the largest dimension
    // (either width or height), figuring out how many times that number can be divided
    // by 2, taking the floor value (rounding down) and adding 1 to represent the
    // base level. This always leaves a value of at least 1.
    return (u8)(kfloor(klog2(KMAX(width, height))) + 1);
}

const char* kmaterial_type_to_string(kmaterial_type type) {
    switch (type) {
    case KMATERIAL_TYPE_STANDARD:
        return "standard";
    case KMATERIAL_TYPE_WATER:
        return "water";
    case KMATERIAL_TYPE_BLENDED:
        return "blended";
    case KMATERIAL_TYPE_CUSTOM:
        return "custom";
    default:
        KASSERT_MSG(false, "Unrecognized material type.");
        return "standard";
    }
}

kmaterial_type string_to_kmaterial_type(const char* str) {
    if (strings_equali(str, "standard")) {
        return KMATERIAL_TYPE_STANDARD;
    } else if (strings_equali(str, "water")) {
        return KMATERIAL_TYPE_WATER;
    } else if (strings_equali(str, "blended")) {
        return KMATERIAL_TYPE_BLENDED;
    } else if (strings_equali(str, "custom")) {
        return KMATERIAL_TYPE_CUSTOM;
    } else {
        KERROR("Unrecognized material type '%s'. Defaulting to KMATERIAL_TYPE_STANDARD.", str);
        return KMATERIAL_TYPE_STANDARD;
    }
}

const char* kmaterial_model_to_string(kmaterial_model model) {
    switch (model) {
    case KMATERIAL_MODEL_UNLIT:
        return "unlit";
    case KMATERIAL_MODEL_PBR:
        return "pbr";
    case KMATERIAL_MODEL_PHONG:
        return "phong";
    case KMATERIAL_MODEL_CUSTOM:
        return "custom";
    default:
        KASSERT_MSG(false, "Unrecognized material model");
        return 0;
    }
}

kmaterial_model string_to_kmaterial_model(const char* str) {
    if (strings_equali(str, "pbr")) {
        return KMATERIAL_MODEL_PBR;
    } else if (strings_equali(str, "unlit")) {
        return KMATERIAL_MODEL_UNLIT;
    } else if (strings_equali(str, "phong")) {
        return KMATERIAL_MODEL_PHONG;
    } else if (strings_equali(str, "custom")) {
        return KMATERIAL_MODEL_CUSTOM;
    } else {
        KERROR("Unrecognized material model '%s'. Defaulting to KMATERIAL_MODEL_PBR.", str);
        return KMATERIAL_MODEL_PBR;
    }
}
