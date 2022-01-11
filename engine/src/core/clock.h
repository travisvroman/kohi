/**
 * @file clock.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains structures and functions for the 
 * engine's clock.
 * @version 1.0
 * @date 2022-01-10
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once
#include "defines.h"

/**
 * @brief Represents a basic clock, which can be used to track time
 * deltas in the system.
 */
typedef struct clock {
    /** @brief The start time of the clock. If never started, this is 0. */
    f64 start_time;
    /** @brief The amount of time in seconds that have elapsed since this
     * clock was started. Only accurate after a call to clock_update. 
     */
    f64 elapsed;
} clock;

/** 
 * @brief Updates the provided clock. Should be called just before checking elapsed time.
 * Has no effect on non-started clocks.
 * @param clock A pointer to the clock to be updated.
 */
KAPI void clock_update(clock* clock);

/** 
 * @brief Starts the provided clock. Resets elapsed time.
 * @param clocl A pointer to the clock to be started.
 */
KAPI void clock_start(clock* clock);

/** 
 * @brief Stops the provided clock. Does not reset elapsed time.
 * @param clock A pointer to the clock to be stopped.
 */
KAPI void clock_stop(clock* clock);
