/**
 * @file font_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system responsible for the management of bitmap
 * and system fonts.
 * @version 2.0
 * @date 2023-01-18
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 * A "bitmap" font uses an image containing pre-rendered glyphs which are then referenced
 * in an internal lookup table by character codepoint. The display of characters for this
 * font type is thus limited to characters contained within the image and configuration asset.
 *
 * A "system" font uses a .ttf or .ttc font file and generates glyphs to an internal atlas
 * on the fly, as needed (although standard ascii characters are rendered to it by default).
 * Display of characters is limited only by those contained in the font. System fonts have
 * "variants", one per font-size (i.e. a font size of 19 and a size of 20 would be unique variants).
 * Each variant keeps its own internal atlas and list of codepoints contained, based on
 * the needs of the string being rendered. These can be updated on the fly.
 * See font_system_system_font_verify_atlas().
 *
 */
#pragma once

#include "identifiers/khandle.h"
#include "kresources/kresource_types.h"
#include "math/math_types.h"
#include "strings/kname.h"

struct font_system_state;

typedef enum font_type {
    FONT_TYPE_BITMAP,
    FONT_TYPE_SYSTEM
} font_type;

/**
 * Represents a system font size variant and its "base" font.
 * This is used to acquire a system font and its size variant,
 * and contains handles to both.
 */
typedef struct system_font_variant {
    // Handle to the base font.
    khandle base_font;
    // Handle to the font size variant.
    khandle variant;
} system_font_variant;

/**
 * @brief The configuration for a bitmap font in the font system config.
 */
typedef struct font_system_bitmap_font_config {
    // The resource name.
    kname resource_name;
    // The resource name.
    kname package_name;
} font_system_bitmap_font_config;

/**
 * @brief The configuration for a system font in the font system config.
 */
typedef struct font_system_system_font_config {
    // The resource name.
    kname resource_name;
    // The resource name.
    kname package_name;
    // The default font size to be used with the system font.
    u16 default_size;
} font_system_system_font_config;

/**
 * @brief The configuration of the font system.
 * Should be setup by the application during the boot
 * process.
 */
typedef struct font_system_config {
    /** @brief The max number of bitmap fonts that can be loaded. */
    u8 max_bitmap_font_count;
    /** @brief The max number of system fonts that can be loaded. */
    u8 max_system_font_count;

    /** @brief The number of bitmap fonts configured in the system. */
    u8 bitmap_font_count;
    /** @brief A collection of bitmap fonts configured in the system. */
    font_system_bitmap_font_config* bitmap_fonts;

    /** @brief The number of system fonts configured in the system. */
    u8 system_font_count;
    /** @brief A collection of system fonts configured in the system. */
    font_system_system_font_config* system_fonts;
} font_system_config;

/**
 * Geometry generated from either a bitmap or system font.
 */
typedef struct font_geometry {
    /** @brief The number of quads to be drawn. */
    u32 quad_count;
    /** @brief The size of the vertex buffer data in bytes. */
    u64 vertex_buffer_size;
    /** @brief The size of the index buffer data in bytes. */
    u64 index_buffer_size;
    /** @brief The vertex buffer data. */
    vertex_2d* vertex_buffer_data;
    /** @brief The index buffer data. */
    u32* index_buffer_data;
} font_geometry;

/**
 * @brief Deserializes font system configuration from the provided string.
 *
 * @param config_str The configuration in string format to be deserialized. Required.
 * @param out_config A pointer to hold the deserialized configuration. Required.
 * @return True if successful; otherwise false.
 */
b8 font_system_deserialize_config(const char* config_str, font_system_config* out_config);

/**
 * @brief Initializes the font system. As with other systems, this should
 * be called twice; once to get the memory requirement (where memory = 0),
 * and a second time passing allocated memory.
 *
 * @param memory_requirement A pointer to hold the memory requirement.
 * @param memory The allocated memory for the system state.
 * @param config The font system config.
 * @return True on success; otherwise false.
 */
b8 font_system_initialize(u64* memory_requirement, void* memory, font_system_config* config);

/**
 * @brief Shuts down the font system.
 *
 * @param state The system state memory.
 */
void font_system_shutdown(struct font_system_state* state);

/**
 * @brief Attempts to acquire a bitmap font of the given name. Must be a registered/loaded font.
 *
 * @param state A pointer to the font system state.
 * @param name The name of the font to acquire.
 * @param out_font A pointer to hold a handle to bitmap font if successful. Required.
 * @return True on load success; otherwise false.
 */
KAPI b8 font_system_bitmap_font_acquire(struct font_system_state* state, kname name, khandle* out_font);

/**
 * @brief Attempts to load a bitmap font from the given named resource.
 *
 * @param state A pointer to the font system state.
 * @param resource_name The name of the font resource to load.
 * @param package_name The name of the package containing the resource.
 * @return True on load success; otherwise false.
 */
KAPI b8 font_system_bitmap_font_load(struct font_system_state* state, kname resource_name, kname package_name);

/**
 * @brief Measures the given string to find out how large it is at the widest/tallest point
 * using the given bitmap font.
 *
 * @param state A pointer to the font system state.
 * @param font A handle to the bitmap font to use for measuring.
 * @param text The text to be measured.
 * @param out_size A pointer to hold the measured size, if successful. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_bitmap_font_measure_string(struct font_system_state* state, khandle font, const char* text, vec2* out_size);

/**
 * @brief Gets a pointer to the font's atlas.
 *
 * @param state A pointer to the font system state.
 * @param font A handle to the bitmap font to use for measuring.
 * @return A pointer to the font's atlas if successful; otherwise 0.
 */
KAPI kresource_texture* font_system_bitmap_font_atlas_get(struct font_system_state* state, khandle font);

/**
 * @brief Gets the line height of the given font.
 *
 * @param state A pointer to the font system state.
 * @param font A handle to the bitmap font to use.
 * @return The line height. Can return 0 if font is not found.
 */
KAPI f32 font_system_bitmap_font_line_height_get(struct font_system_state* state, khandle font);

/**
 * @brief Generates geometry data for a bitmap font.
 *
 * @param state A pointer to the font system state.
 * @param font A handle to the bitmap font to use for generation.
 * @param text The text to use for generation.
 * @param out_size A pointer to hold the generated font geometry, if successful. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_bitmap_font_generate_geometry(struct font_system_state* state, khandle font, const char* text, font_geometry* out_geometry);

/**
 * @brief Attempts to acquire a system font variant of the given name and size. Must be a registered/loaded font.
 *
 * @param state A pointer to the font system state.
 * @param name The name of the font to acquire.
 * @param font_size The font size. Ignored for bitmap fonts.
 * @param out_variant A pointer to hold a system font variant, with handles to both the "base" font and the size variant. if successful. Required.
 * @return True if successful; otherwise false.
 */
KAPI b8 font_system_system_font_acquire(struct font_system_state* state, kname name, u16 font_size, system_font_variant* out_variant);

/**
 * @brief Attempts to load a system font from the given named resource.
 *
 * @param state A pointer to the font system state.
 * @param resource_name The name of the font resource to load.
 * @param package_name The name of the package containing the resource.
 * @param default_size The default font size. Clamped to an acceptable range.
 * @return True on load success; otherwise false.
 */
KAPI b8 font_system_system_font_load(struct font_system_state* state, kname resource_name, kname package_name, u16 default_size);

/**
 * @brief Verifies the atlas of the provided system font contains
 * the characters in text.
 *
 * @param state A pointer to the font system state.
 * @param variant The system font variant to verify.
 * @param text The text containing the characters required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_system_font_verify_atlas(struct font_system_state* state, system_font_variant variant, const char* text);

/**
 * @brief Measures the given string to find out how large it is at the widest/tallest point
 * using the given system font.
 *
 * @param state A pointer to the font system state.
 * @param variant The system font variant to use for measuring.
 * @param text The text to be measured.
 * @param out_size A pointer to hold the measured size, if successful. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_system_font_measure_string(struct font_system_state* state, system_font_variant variant, const char* text, vec2* out_size);

/**
 * @brief Gets the line height of the given font.
 *
 * @param state A pointer to the font system state.
 * @param variant The system font variant to use.
 * @return The line height. Can return 0 if font is not found.
 */
KAPI f32 font_system_system_font_line_height_get(struct font_system_state* state, system_font_variant variant);

/**
 * @brief Generates geometry data for a system font variant.
 *
 * @param state A pointer to the font system state.
 * @param variant The system font variant to use for generation.
 * @param text The text to use for generation.
 * @param out_size A pointer to hold the generated font geometry, if successful. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_system_font_generate_geometry(struct font_system_state* state, system_font_variant variant, const char* text, font_geometry* out_geometry);

/**
 * @brief Gets a pointer to the font's atlas.
 *
 * @param state A pointer to the font system state.
 * @param variant The system font variant to use for retrieval.
 * @return A pointer to the font's atlas if successful; otherwise 0.
 */
KAPI kresource_texture* font_system_system_font_atlas_get(struct font_system_state* state, system_font_variant variant);
