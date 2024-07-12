/**
 * @file font_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A system responsible for the management of bitmap
 * and system fonts.
 * @version 1.0
 * @date 2023-01-18
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */
#pragma once

#include "math/math_types.h"
#include "renderer/renderer_types.h"
#include "resources/resource_types.h"

/**
 * @brief The configuration for a system font.
 */
typedef struct system_font_config {
    /** @brief The name of the font. */
    const char* name;
    /** @brief The default size of the font. */
    u16 default_size;
    /** @brief The name of the resource containing the font data. */
    const char* resource_name;
} system_font_config;

/**
 * @brief The configuration for a bitmap font.
 */
typedef struct bitmap_font_config {
    /** @brief The name of the font. */
    const char* name;
    /** @brief The size of the font. */
    u16 size;
    /** @brief The name of the resource containing the font data. */
    const char* resource_name;
} bitmap_font_config;

/**
 * @brief The configuration of the font system.
 * Should be setup by the application during the boot
 * process.
 */
typedef struct font_system_config {
    /** @brief The default system font config. */
    system_font_config default_system_font;
    /** @brief The default bitmap font config. */
    bitmap_font_config default_bitmap_font;
    /** @brief Indicates if fonts should auto-release when not used. */
    b8 auto_release;
} font_system_config;

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
 * @param memory The system state memory.
 */
void font_system_shutdown(void* memory);

/**
 * @brief Loads a system font from the following config.
 *
 * @param config A pointer to the config to use for loading.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_system_font_load(system_font_config* config);
/**
 * @brief Loads a bitmap font from the following config.
 *
 * @param config A pointer to the config to use for loading.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_bitmap_font_load(bitmap_font_config* config);

/**
 * @brief Attempts to acquire a font of the given name and assign it to the given ui_text.
 *
 * @param font_name The name of the font to acquire. Must be an already loaded font.
 * @param font_size The font size. Ignored for bitmap fonts.
 * @param type The type of the font to acquire.
 * @return A pointer to font data if successful; otherwise 0/null.
 */
KAPI font_data* font_system_acquire(const char* font_name, u16 font_size, font_type type);

/**
 * @brief Releases references to the font held by the provided ui_text.
 *
 * @param font_name The name of the font to acquire. Must be an already loaded font.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_release(const char* font_name);

/**
 * @brief Verifies the atlas of the provided font contains
 * the characters in text.
 *
 * @param font A pointer to the font to be verified.
 * @param text The text containing the characters required.
 * @return True on success; otherwise false.
 */
KAPI b8 font_system_verify_atlas(font_data* font, const char* text);

/**
 * @brief Measures the given string to find out how large it is at the widest/tallest point.
 *
 * @param font A pointer to the font to use for measuring.
 * @param text The text to be measured.
 */
KAPI vec2 font_system_measure_string(font_data* font, const char* text);
