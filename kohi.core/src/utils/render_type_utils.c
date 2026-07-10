#include "render_type_utils.h"

#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "math/kmath.h"
#include "math/math_types.h"
#include "strings/kstring.h"

const char *texture_repeat_to_string (texture_repeat repeat) {
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

texture_repeat string_to_texture_repeat (const char *str) {
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

const char *texture_filter_mode_to_string (texture_filter filter) {
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

texture_filter string_to_texture_filter_mode (const char *str) {
	if (strings_equali("linear", str)) {
		return TEXTURE_FILTER_MODE_LINEAR;
	} else if (strings_equali("nearest", str)) {
		return TEXTURE_FILTER_MODE_LINEAR;
	} else {
		KERROR("Unrecognized texture filter type '%s'. Defaulting to TEXTURE_FILTER_MODE_LINEAR.", str);
		return TEXTURE_FILTER_MODE_LINEAR;
	}
}

const char *texture_channel_to_string (texture_channel channel) {
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

texture_channel string_to_texture_channel (const char *str) {
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

const char *shader_attribute_type_to_string (shader_attribute_type type) {
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
	case SHADER_ATTRIB_TYPE_INT32_2:
		return "ivec2";
	case SHADER_ATTRIB_TYPE_INT32_3:
		return "ivec3";
	case SHADER_ATTRIB_TYPE_INT32_4:
		return "ivec4";
	case SHADER_ATTRIB_TYPE_UINT32_2:
		return "uvec2";
	case SHADER_ATTRIB_TYPE_UINT32_3:
		return "uvec3";
	case SHADER_ATTRIB_TYPE_UINT32_4:
		return "uvec4";
		break;
	}
}

shader_attribute_type string_to_shader_attribute_type (const char *str) {
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
	} else if (strings_equali("ivec2", str)) {
		return SHADER_ATTRIB_TYPE_INT32_2;
	} else if (strings_equali("ivec3", str)) {
		return SHADER_ATTRIB_TYPE_INT32_3;
	} else if (strings_equali("ivec4", str)) {
		return SHADER_ATTRIB_TYPE_INT32_4;
	} else if (strings_equali("u32", str) || strings_equali("uint", str)) {
		return SHADER_ATTRIB_TYPE_UINT32;
	} else if (strings_equali("uvec2", str)) {
		return SHADER_ATTRIB_TYPE_UINT32_2;
	} else if (strings_equali("uvec3", str)) {
		return SHADER_ATTRIB_TYPE_UINT32_3;
	} else if (strings_equali("uvec4", str)) {
		return SHADER_ATTRIB_TYPE_UINT32_4;
	} else {
		KERROR("Unrecognized attribute type '%s'. Defaulting to i32", str);
		return SHADER_ATTRIB_TYPE_INT32;
	}
}

const char *shader_stage_to_string (shader_stage stage) {
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

shader_stage string_to_shader_stage (const char *str) {
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

const char *face_cull_mode_to_string (face_cull_mode mode) {
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

face_cull_mode string_to_face_cull_mode (const char *str) {
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

const char *topology_type_to_string (primitive_topology_type_bits type) {
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

primitive_topology_type_bits string_to_topology_type (const char *str) {

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

u16 size_from_shader_attribute_type (shader_attribute_type type) {
	switch (type) {
	case SHADER_ATTRIB_TYPE_FLOAT32:
		return 4;
	case SHADER_ATTRIB_TYPE_FLOAT32_2:
	case SHADER_ATTRIB_TYPE_INT32_2:
	case SHADER_ATTRIB_TYPE_UINT32_2:
		return 8;
	case SHADER_ATTRIB_TYPE_FLOAT32_3:
	case SHADER_ATTRIB_TYPE_INT32_3:
	case SHADER_ATTRIB_TYPE_UINT32_3:
		return 12;
	case SHADER_ATTRIB_TYPE_FLOAT32_4:
	case SHADER_ATTRIB_TYPE_INT32_4:
	case SHADER_ATTRIB_TYPE_UINT32_4:
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

#define PX_ALPHA_LESS_THAN_MAX(type, array, pixel_count, alpha_max, channel_count, alpha_index) \
	{                                                                                           \
		type *px = (type *)array;                                                               \
		for (u32 i = 0; i < pixel_count; ++i) {                                                 \
			type alpha = px[(sizeof(type) * channel_count * i) + alpha_index];                  \
			if (alpha < alpha_max) {                                                            \
				return true;                                                                    \
			}                                                                                   \
		}                                                                                       \
		return false;                                                                           \
	}

b8 pixel_data_has_transparency (const void *pixels, u32 pixel_count, kpixel_format format) {
	if (!pixels || !pixel_count) {
		return false;
	}

	switch (format) {
	case KPIXEL_FORMAT_UNKNOWN:

	case KPIXEL_FORMAT_RGBA8: {
		PX_ALPHA_LESS_THAN_MAX(u8, pixels, pixel_count, U8_MAX, 4, 3);
	}
	case KPIXEL_FORMAT_RGBA16: {
		PX_ALPHA_LESS_THAN_MAX(u16, pixels, pixel_count, U16_MAX, 4, 3);
	}
	case KPIXEL_FORMAT_RGBA32: {
		PX_ALPHA_LESS_THAN_MAX(u32, pixels, pixel_count, U32_MAX, 4, 3);
	}

	case KPIXEL_FORMAT_RGB8:
	case KPIXEL_FORMAT_RG8:
	case KPIXEL_FORMAT_R8:
	case KPIXEL_FORMAT_RGB16:
	case KPIXEL_FORMAT_RG16:
	case KPIXEL_FORMAT_R16:
	case KPIXEL_FORMAT_RGB32:
	case KPIXEL_FORMAT_RG32:
	case KPIXEL_FORMAT_R32:
	case KPIXEL_FORMAT_D32:
	case KPIXEL_FORMAT_D24:
	case KPIXEL_FORMAT_S8:
		// No alpha channel, return false.
		return false;
	}
}

u8 channel_count_from_pixel_format (kpixel_format format) {
	switch (format) {
	case KPIXEL_FORMAT_UNKNOWN:
	case KPIXEL_FORMAT_RGBA8:
	case KPIXEL_FORMAT_RGBA16:
	case KPIXEL_FORMAT_RGBA32:
	case KPIXEL_FORMAT_D32:
		return 4;
	case KPIXEL_FORMAT_RGB8:
	case KPIXEL_FORMAT_RGB16:
	case KPIXEL_FORMAT_RGB32:
	case KPIXEL_FORMAT_D24:
		return 3;
	case KPIXEL_FORMAT_RG8:
	case KPIXEL_FORMAT_RG16:
	case KPIXEL_FORMAT_RG32:
		return 2;
	case KPIXEL_FORMAT_R8:
	case KPIXEL_FORMAT_R16:
	case KPIXEL_FORMAT_R32:
	case KPIXEL_FORMAT_S8:
		return 1;
	}
}

const char *string_from_kpixel_format (kpixel_format format) {
	switch (format) {
	case KPIXEL_FORMAT_UNKNOWN:
	case KPIXEL_FORMAT_RGBA8:
		return "rgba8";
	case KPIXEL_FORMAT_RGBA16:
		return "rgba16";
	case KPIXEL_FORMAT_RGBA32:
		return "rgba32";
	case KPIXEL_FORMAT_RGB8:
		return "rgb8";
	case KPIXEL_FORMAT_RGB16:
		return "rgb16";
	case KPIXEL_FORMAT_RGB32:
		return "rgb32";
	case KPIXEL_FORMAT_RG8:
		return "rg8";
	case KPIXEL_FORMAT_RG16:
		return "rg16";
	case KPIXEL_FORMAT_RG32:
		return "rg32";
	case KPIXEL_FORMAT_R8:
		return "r8";
	case KPIXEL_FORMAT_R16:
		return "r16";
	case KPIXEL_FORMAT_R32:
		return "r2";
	case KPIXEL_FORMAT_D32:
		return "d32";
	case KPIXEL_FORMAT_D24:
		return "d24";
	case KPIXEL_FORMAT_S8:
		return "s8";
	}
}

kpixel_format string_to_kpixel_format (const char *str) {
	if (!str) {
		return KPIXEL_FORMAT_UNKNOWN;
	}

	if (strings_equali(str, "rgba8")) {
		return KPIXEL_FORMAT_RGBA8;
	} else if (strings_equali(str, "rgba16")) {
		return KPIXEL_FORMAT_RGBA16;
	} else if (strings_equali(str, "rgba32")) {
		return KPIXEL_FORMAT_RGBA32;
	} else if (strings_equali(str, "rgb8")) {
		return KPIXEL_FORMAT_RGB8;
	} else if (strings_equali(str, "rgb16")) {
		return KPIXEL_FORMAT_RGB16;
	} else if (strings_equali(str, "rgb32")) {
		return KPIXEL_FORMAT_RGB32;
	} else if (strings_equali(str, "rg8")) {
		return KPIXEL_FORMAT_RG8;
	} else if (strings_equali(str, "rg16")) {
		return KPIXEL_FORMAT_RG16;
	} else if (strings_equali(str, "rg32")) {
		return KPIXEL_FORMAT_RG32;
	} else if (strings_equali(str, "r8")) {
		return KPIXEL_FORMAT_R8;
	} else if (strings_equali(str, "r16")) {
		return KPIXEL_FORMAT_R16;
	} else if (strings_equali(str, "r32")) {
		return KPIXEL_FORMAT_R32;
	} else if (strings_equali(str, "d32")) {
		return KPIXEL_FORMAT_D32;
	} else if (strings_equali(str, "d24")) {
		return KPIXEL_FORMAT_D24;
	} else if (strings_equali(str, "s8")) {
		return KPIXEL_FORMAT_S8;
	}

	// Fall back to unknown.
	return KPIXEL_FORMAT_UNKNOWN;
}

u8 calculate_mip_levels_from_dimension (u32 width, u32 height) {
	// The number of mip levels is calculated by first taking the largest dimension
	// (either width or height), figuring out how many times that number can be divided
	// by 2, taking the floor value (rounding down) and adding 1 to represent the
	// base level. This always leaves a value of at least 1.
	return (u8)(kfloor(klog2(KMAX(width, height))) + 1);
}

const char *kmaterial_type_to_string (kmaterial_type type) {
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

kmaterial_type string_to_kmaterial_type (const char *str) {
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

const char *kmaterial_model_to_string (kmaterial_model model) {
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

kmaterial_model string_to_kmaterial_model (const char *str) {
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

mat4 generate_projection_matrix (rect_2di rect, f32 fov, f32 near_clip, f32 far_clip, projection_matrix_type matrix_type) {
	switch (matrix_type) {
	default:
	case PROJECTION_MATRIX_TYPE_PERSPECTIVE:
		return mat4_perspective(fov, (f32)rect.width / rect.height, near_clip, far_clip);
	case PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC:
		// NOTE: may need to reverse y/w
		return mat4_orthographic(rect.x, rect.width, rect.height, rect.y, near_clip, far_clip);
	case PROJECTION_MATRIX_TYPE_ORTHOGRAPHIC_CENTERED: {
		f32 mod = fov;
		return mat4_orthographic(-rect.width * mod, rect.width * mod, -rect.height * mod, rect.height * mod, near_clip, far_clip);
	} break;
	}
}

shader_binding_type shader_binding_type_from_string (const char *str) {
	if (strings_equali(str, "ubo")) {
		return SHADER_BINDING_TYPE_UBO;
	} else if (strings_equali(str, "ssbo") || strings_equali(str, "storage")) {
		return SHADER_BINDING_TYPE_SSBO;
	} else if (strings_equali(str, "texture")) {
		return SHADER_BINDING_TYPE_TEXTURE;
	} else if (strings_equali(str, "sampler")) {
		return SHADER_BINDING_TYPE_SAMPLER;
	} else {
		KASSERT_MSG(false, str);
		return SHADER_BINDING_TYPE_UBO;
	}
}

const char *shader_binding_type_to_string (shader_binding_type type) {
	switch (type) {
	case SHADER_BINDING_TYPE_UBO:
		return "ubo";
	case SHADER_BINDING_TYPE_SSBO:
		return "storage";
	case SHADER_BINDING_TYPE_TEXTURE:
		return "texture";
	case SHADER_BINDING_TYPE_SAMPLER:
		return "sampler";
	default:
		KASSERT_MSG(false, "Unknown binding type");
		return 0;
	}
}

ktexture_type ktexture_type_from_string (const char *str) {
	if (strings_equali(str, "texture1D") || strings_equali(str, "1D")) {
		return KTEXTURE_TYPE_1D;
	} else if (strings_equali(str, "texture2D") || strings_equali(str, "2D")) {
		return KTEXTURE_TYPE_2D;
	} else if (strings_equali(str, "texture3D") || strings_equali(str, "3D")) {
		return KTEXTURE_TYPE_3D;
	} else if (strings_equali(str, "textureCube") || strings_equali(str, "cube")) {
		return KTEXTURE_TYPE_CUBE;
	} else if (strings_equali(str, "texture1DArray") || strings_equali(str, "1DArray")) {
		return KTEXTURE_TYPE_1D_ARRAY;
	} else if (strings_equali(str, "texture2DArray") || strings_equali(str, "2DArray")) {
		return KTEXTURE_TYPE_2D_ARRAY;
	} else if (strings_equali(str, "textureCubeArray") || strings_equali(str, "cubeArray")) {
		return KTEXTURE_TYPE_CUBE_ARRAY;
	} else {
		KWARN("%s - Unknown texture type '%s', defaulting to 2d");
		return KTEXTURE_TYPE_2D;
	}
}

const char *ktexture_type_to_string (ktexture_type type) {
	switch (type) {
	case KTEXTURE_TYPE_1D:
		return "texture1D";
	case KTEXTURE_TYPE_2D:
	default:
		return "texture2D";
	case KTEXTURE_TYPE_3D:
		return "texture3D";
	case KTEXTURE_TYPE_CUBE:
		return "textureCube";
	case KTEXTURE_TYPE_1D_ARRAY:
		return "texture1DArray";
	case KTEXTURE_TYPE_2D_ARRAY:
		return "texture2DArray";
	case KTEXTURE_TYPE_CUBE_ARRAY:
		return "textureCubeArray";
	}
}

shader_sampler_type shader_sampler_type_from_string (const char *str) {
	if (strings_equali(str, "sampler1D") || strings_equali(str, "1D")) {
		return SHADER_SAMPLER_TYPE_1D;
	} else if (strings_equali(str, "sampler2D") || strings_equali(str, "2D")) {
		return SHADER_SAMPLER_TYPE_2D;
	} else if (strings_equali(str, "sampler3D") || strings_equali(str, "3D")) {
		return SHADER_SAMPLER_TYPE_3D;
	} else if (strings_equali(str, "samplerCube") || strings_equali(str, "cube")) {
		return SHADER_SAMPLER_TYPE_CUBE;
	} else if (strings_equali(str, "sampler1DArray") || strings_equali(str, "1DArray")) {
		return SHADER_SAMPLER_TYPE_1D_ARRAY;
	} else if (strings_equali(str, "sampler2DArray") || strings_equali(str, "2DArray")) {
		return SHADER_SAMPLER_TYPE_2D_ARRAY;
	} else if (strings_equali(str, "samplerCubeArray") || strings_equali(str, "cubeArray")) {
		return SHADER_SAMPLER_TYPE_CUBE_ARRAY;
	} else {
		KWARN("%s - Unknown sampler type '%s', defaulting to 2d");
		return SHADER_SAMPLER_TYPE_2D;
	}
}

const char *shader_sampler_type_to_string (shader_sampler_type type) {
	switch (type) {
	case SHADER_SAMPLER_TYPE_1D:
		return "sampler1D";
	case SHADER_SAMPLER_TYPE_2D:
		return "sampler2D";
	case SHADER_SAMPLER_TYPE_3D:
		return "sampler3D";
	case SHADER_SAMPLER_TYPE_CUBE:
		return "samplerCube";
	case SHADER_SAMPLER_TYPE_1D_ARRAY:
		return "sampler1DArray";
	case SHADER_SAMPLER_TYPE_2D_ARRAY:
		return "sampler2DArray";
	case SHADER_SAMPLER_TYPE_CUBE_ARRAY:
		return "samplerCubeArray";
	}
}
