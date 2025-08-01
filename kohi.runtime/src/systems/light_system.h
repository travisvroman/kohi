/**
 * @file light_system.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the implementation of the light system, which
 * manages all lighting objects within the engine.
 * @version 1.0
 * @date 2023-03-02
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */
#pragma once

#include "defines.h"
#include "math/math_types.h"
#include "strings/kname.h"

typedef struct directional_light_data {
    /** @brief The light colour. */
    vec4 colour;
    /** @brief The direction of the light. The w component is ignored.*/
    vec4 direction;

    f32 shadow_distance;
    f32 shadow_fade_distance;
    f32 shadow_split_mult;
    f32 padding;
} directional_light_data;

/**
 * @brief A directional light, typically used to emulate sun/moon light.
 */
typedef struct directional_light {
    /** @brief The name of the directional light. */
    kname name;

    /** @bried The generation of the light, incremented on change. Can be used to tell when a shader upload is required. */
    u32 generation;
    /** @brief The directional light shader data. */
    directional_light_data data;
    /** @brief Debug data assigned to the light. */
    void* debug_data;
} directional_light;

// FIXME: colour.a is not used, roll one of the floats below into it. (linear?)
// FIXME: position.w is also not used, roll one of the floats below into it. (quadratic?)
// Also remove the floats from below, and assume constant_f is always 1.0
typedef struct point_light_data {
    /** @brief The light colour. */
    vec4 colour;
    /** @brief The position of the light in the world. The w component is ignored.*/
    vec4 position;
    /** @brief Usually 1, make sure denominator never gets smaller than 1 */
    f32 constant_f;
    /** @brief Reduces light intensity linearly */
    f32 linear;
    /** @brief Makes the light fall off slower at longer distances. */
    f32 quadratic;
    /** @brief Additional padding used for memory alignment purposes. Ignored. */
    f32 padding;
} point_light_data;

/**
 * @brief A point light, the most common light source, which radiates out from the
 * given position.
 */
typedef struct point_light {
    /** @brief The name of the light. */
    kname name;
    /** @brief The generation of the light, incremented on every update. Can be used to detect when a shader upload is required. */
    u32 generation;
    /** @brief The shader data for the point light. */
    point_light_data data;
    /** @brief Debug data assigned to the light. */
    void* debug_data;

    // The positional offset from whatever this may be attached to.
    vec4 position;
} point_light;

/**
 * @brief Initializes the light system. As with most systems, this should be called
 * twice, the first time to obtain the memory requirement (where memory=0), and a
 * second time passing allocated memory the size of memory_requirement.
 *
 * @param memory_requirement A pointer to hold the memory requirement.
 * @param memory Block of allocated memory, or 0 if requesting memory requirement.
 * @param config Configuration for this system. Currently unused.
 * @return True on success; otherwise false.
 */
b8 light_system_initialize(u64* memory_requirement, void* memory, void* config);

/**
 * @brief Shuts down the light system, releasing all resources.
 *
 * @param state The state/memory block for the system.
 */
void light_system_shutdown(void* state);

/**
 * @brief Attempts to add a directional light to the system. Only one may be present
 * at once, and is overwritten when one is passed here.
 *
 * @param light A pointer to the light to be added.
 * @return True on success; otherwise false.
 */
KAPI b8 light_system_directional_add(directional_light* light);

/**
 * @brief Attempts to add a point light to the system.
 *
 * @param light A pointer to the light to be added.
 * @return True on success; otherwise false.
 */
KAPI b8 light_system_point_add(point_light* light);

/**
 * @brief Attempts to remove the given light from the system. A pointer comparison
 * is done, meaning the light to be removed must be the original that was added.
 *
 * @param light A pointer to the light to be removed.
 * @return True on successful removal; otherwise false.
 */
KAPI b8 light_system_directional_remove(directional_light* light);

/**
 * @brief Attempts to remove the given light from the system. A pointer comparison
 * is done, meaning the light to be removed must be the original that was added.
 *
 * @param light A pointer to the light to be removed.
 * @return True on successful removal; otherwise false.
 */
KAPI b8 light_system_point_remove(point_light* light);

/**
 * @brief Obtains a pointer to the current directional light. Can be NULL if one
 * has not been added.
 *
 * @return A pointer to the current directional light.
 */
KAPI directional_light* light_system_directional_light_get(void);

/**
 * @brief Returns the total number of point lights currently in the system.
 *
 * @return The total number of point lights currently in the system.
 */
KAPI u32 light_system_point_light_count(void);

/**
 * @brief Fills in the required array of point lights within the system. Array
 * must already exist and have at least enough space as determined by a call to
 * light_system_point_light_count().
 *
 * @param p_lights An array of point lights. These lights are *copies* of the original.
 * @return True on success; otherwise false.
 */
KAPI b8 light_system_point_lights_get(point_light* p_lights);
