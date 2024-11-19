#include "renderer_utils.h"

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
