#pragma once

#include <containers/array.h>
#include <math/math_types.h>
#include <strings/kname.h>

/**
 * @brief Represents various types of textures.
 */
typedef enum ktexture_type {
    /** @brief A standard two-dimensional texture. */
    KTEXTURE_TYPE_2D,
    /** @brief A 2d array texture. */
    KTEXTURE_TYPE_2D_ARRAY,
    /** @brief A cube texture, used for cubemaps. */
    KTEXTURE_TYPE_CUBE,
    /** @brief A cube array texture, used for arrays of cubemaps. */
    KTEXTURE_TYPE_CUBE_ARRAY,
    KTEXTURE_TYPE_COUNT
} ktexture_type;

typedef enum ktexture_flag {
    /** @brief Indicates if the texture has transparency. */
    KTEXTURE_FLAG_HAS_TRANSPARENCY = 0x01,
    /** @brief Indicates if the texture can be written (rendered) to. */
    KTEXTURE_FLAG_IS_WRITEABLE = 0x02,
    /** @brief Indicates if the texture was created via wrapping vs traditional
       creation. */
    KTEXTURE_FLAG_IS_WRAPPED = 0x04,
    /** @brief Indicates the texture is a depth texture. */
    KTEXTURE_FLAG_DEPTH = 0x08,
    /** @brief Indicates the texture is a stencil texture. */
    KTEXTURE_FLAG_STENCIL = 0x10,
    /** @brief Indicates that this texture should account for renderer buffering (i.e. double/triple buffering) */
    KTEXTURE_FLAG_RENDERER_BUFFERING = 0x20,
} ktexture_flag;

/** @brief Holds bit flags for textures.. */
typedef u8 ktexture_flag_bits;

/**
 * @brief Represents a texture to be used for rendering purposes,
 * stored on the GPU (VRAM)
 */
typedef u16 ktexture;

// The id representing an invalid texture.
#define INVALID_KTEXTURE INVALID_ID_U16

/**
 * @brief Represents a single static mesh, which contains geometry.
 */
typedef u16 kstatic_mesh;

#define INVALID_KSTATIC_MESH INVALID_ID_U16

typedef struct font_glyph {
    i32 codepoint;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    i16 x_offset;
    i16 y_offset;
    i16 x_advance;
    u8 page_id;
} font_glyph;

typedef struct font_kerning {
    i32 codepoint_0;
    i32 codepoint_1;
    i16 amount;
} font_kerning;

typedef struct font_page {
    kname image_asset_name;
} font_page;

ARRAY_TYPE(font_glyph);
ARRAY_TYPE(font_kerning);
ARRAY_TYPE(font_page);

/**
 * Represents a Kohi Audio.
 */
typedef u16 kaudio;

// The id representing an invalid kaudio.
#define INVALID_KAUDIO INVALID_ID_U16
