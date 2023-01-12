#include "systems_manager.h"

#include "core/logger.h"

// Systems
#include "engine.h"
#include "core/console.h"
#include "core/kvar.h"
#include "core/event.h"
#include "core/input.h"
#include "platform/platform.h"

static b8 register_known_systems(systems_manager_state* state, application_config* app_config);
static void shutdown_known_systems(systems_manager_state* state);

b8 systems_manager_initialize(systems_manager_state* state, application_config* app_config) {
    // Create a linear allocator for all systems (except memory) to use.
    linear_allocator_create(MEBIBYTES(64), 0, &state->systems_allocator);

    // Register known systems
    return register_known_systems(state, app_config);
}

void systems_manager_shutdown(systems_manager_state* state) {
    shutdown_known_systems(state);
}

b8 systems_manager_update(systems_manager_state* state, u32 delta_time) {
    return true;
}

b8 systems_manager_register(
    systems_manager_state* state,
    u16 type,
    PFN_system_initialize initialize,
    PFN_system_shutdown shutdown,
    PFN_system_update update,
    void* config) {
    k_system sys;
    sys.initialize = initialize;
    sys.shutdown = shutdown;
    sys.update = update;

    // Call initialize, alloc memory, call initialize again w/ allocated block.
    if (sys.initialize) {
        if (!sys.initialize(&sys.state_size, 0, config)) {
            KERROR("Failed to register system - initialize call failed.");
            return false;
        }

        sys.state = linear_allocator_allocate(&state->systems_allocator, sys.state_size);

        if (!sys.initialize(&sys.state_size, sys.state, config)) {
            KERROR("Failed to register system - initialize call failed.");
            return false;
        }
    } else {
        if (type != K_SYSTEM_TYPE_MEMORY) {
            KERROR("Initialize is required for types except K_SYSTEM_TYPE_MEMORY.");
            return false;
        }
    }

    state->systems[type] = sys;

    return true;
}

b8 register_known_systems(systems_manager_state* state, application_config* app_config) {
    // Console
    if (!systems_manager_register(state, K_SYSTEM_TYPE_CONSOLE, console_initialize, console_shutdown, 0, 0)) {
        KERROR("Failed to register console system.");
        return false;
    }

    // KVars
    if (!systems_manager_register(state, K_SYSTEM_TYPE_KVAR, kvar_initialize, kvar_shutdown, 0, 0)) {
        KERROR("Failed to register KVar system.");
        return false;
    }

    // Events
    if (!systems_manager_register(state, K_SYSTEM_TYPE_EVENT, event_system_initialize, event_system_shutdown, 0, 0)) {
        KERROR("Failed to register event system.");
        return false;
    }

    // Logging
    if (!systems_manager_register(state, K_SYSTEM_TYPE_LOGGING, logging_initialize, logging_shutdown, 0, 0)) {
        KERROR("Failed to register logging system.");
        return false;
    }

    // Input
    if (!systems_manager_register(state, K_SYSTEM_TYPE_INPUT, input_system_initialize, input_system_shutdown, 0, 0)) {
        KERROR("Failed to register input system.");
        return false;
    }

    // Platform
    // TODO(travis): This causes an issue where the swapchain generation fails with a size of 0/0
    // until a window resize happens. But hwhy?
    platform_system_config plat_config = {0};
    plat_config.application_name = app_config->name;
    plat_config.x = app_config->start_pos_x;
    plat_config.y = app_config->start_pos_y;
    plat_config.width = app_config->start_width;
    plat_config.height = app_config->start_height;
    if (!systems_manager_register(state, K_SYSTEM_TYPE_PLATFORM, platform_system_startup, platform_system_shutdown, 0, &plat_config)) {
        KERROR("Failed to register platform system.");
        return false;
    }

    return true;
}

void shutdown_known_systems(systems_manager_state* state) {
    state->systems[K_SYSTEM_TYPE_CONSOLE].shutdown(state->systems[K_SYSTEM_TYPE_CONSOLE].state);
    state->systems[K_SYSTEM_TYPE_KVAR].shutdown(state->systems[K_SYSTEM_TYPE_KVAR].state);
    state->systems[K_SYSTEM_TYPE_EVENT].shutdown(state->systems[K_SYSTEM_TYPE_EVENT].state);
    state->systems[K_SYSTEM_TYPE_LOGGING].shutdown(state->systems[K_SYSTEM_TYPE_LOGGING].state);
    state->systems[K_SYSTEM_TYPE_INPUT].shutdown(state->systems[K_SYSTEM_TYPE_INPUT].state);
    state->systems[K_SYSTEM_TYPE_PLATFORM].shutdown(state->systems[K_SYSTEM_TYPE_PLATFORM].state);
}