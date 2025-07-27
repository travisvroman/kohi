#pragma once

#include "defines.h"

typedef struct timeline_system_config {
    u32 dummy;
} timeline_system_config;

typedef u16 ktimeline;
#define KTIMELINE_INVALID INVALID_ID_U16

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
b8 ktimeline_system_initialize(u64* memory_requirement, void* memory, void* config);

/**
 * @brief Updates the job system. Should happen once an update cycle.
 */
b8 ktimeline_system_update(void* state, f32 engine_delta_time);

/**
 * @brief Shuts down the timeline system.
 *
 * @param state A pointer to the system state.
 */
void ktimeline_system_shutdown(void* state);

KAPI ktimeline ktimeline_system_create(f32 scale);

KAPI void ktimeline_system_destroy(ktimeline timeline);

KAPI f32 ktimeline_system_scale_get(ktimeline timeline);
KAPI void ktimeline_system_scale_set(ktimeline timeline, f32 scale);

// Total time since timeline start.
KAPI f32 ktimeline_system_total_get(ktimeline timeline);

// Time in seconds since the last frame.
KAPI f32 ktimeline_system_delta_get(ktimeline timeline);

KAPI ktimeline ktimeline_system_get_engine(void);
KAPI ktimeline ktimeline_system_get_game(void);
