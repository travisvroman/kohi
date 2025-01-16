#pragma once

#include "identifiers/khandle.h"
#include "defines.h"

typedef struct timeline_system_config {
    u32 dummy;
} timeline_system_config;

/**
 * @brief Initializes the timeline system using the supplied configuration.
 * NOTE: Call this twice, once to obtain memory requirement (memory = 0) and a second time
 * including allocated memory.
 *
 * @param memory_requirement A pointer to hold the memory requirement of this system in bytes.
 * @param memory A memory block to be used to hold the state of this system. Pass 0 on the first call to get memory requirement.
 * @param config The configuration (timeline_system_config) to be used when initializing the system.
 * @return b8 True on success; otherwise false.
 */
b8 timeline_system_initialize(u64* memory_requirement, void* memory, void* config);

/**
 * @brief Updates the job system. Should happen once an update cycle.
 */
b8 timeline_system_update(void* state, f32 engine_delta_time);

/**
 * @brief Shuts down the timeline system.
 *
 * @param state A pointer to the system state.
 */
void timeline_system_shutdown(void* state);

KAPI khandle timeline_system_create(f32 scale);

KAPI void timeline_system_destroy(khandle timeline);

KAPI f32 timeline_system_scale_get(khandle timeline);
KAPI void timeline_system_scale_set(khandle timeline, f32 scale);

// Total time since timeline start.
KAPI f32 timeline_system_total_get(khandle timeline);

// Time in seconds since the last frame.
KAPI f32 timeline_system_delta_get(khandle timeline);

KAPI khandle timeline_system_get_engine(void);
KAPI khandle timeline_system_get_game(void);
