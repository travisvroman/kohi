/**
 * @file sim_clock.h
 * @brief Fixed-step simulation clock. Decides how many fixed simulation steps to run each frame,
 * independently of how fast frames are rendered.
 *
 * Three modes:
 * - Variable: no fixed steps. Legacy behaviour; simulation advances by the measured frame time.
 * - Real-time: an accumulator collects wall-clock frame time and releases it in whole fixed steps,
 *   so the simulation runs at the same speed whatever the frame rate. Leftover time is exposed as
 *   an interpolation factor (alpha) for rendering between the last two simulation states.
 * - Lockstep: exactly one fixed step per frame and no wall-clock reads. For headless runs, where
 *   the simulation should go as fast as the CPU allows and be repeatable.
 *
 * In every fixed mode the simulation can be paused, and single steps requested while paused.
 *
 * This module is pure logic (no engine or platform calls), so it can be unit tested directly.
 */
#pragma once

#include <defines.h>

typedef enum ksim_clock_mode {
	/** @brief No fixed steps. The simulation advances by the measured frame time. */
	KSIM_CLOCK_MODE_VARIABLE,
	/** @brief Fixed steps released by an accumulator fed with wall-clock frame time. */
	KSIM_CLOCK_MODE_REALTIME,
	/** @brief One fixed step per frame, never reading the wall clock. */
	KSIM_CLOCK_MODE_LOCKSTEP
} ksim_clock_mode;

typedef struct ksim_clock {
	ksim_clock_mode mode;
	/** @brief Length of one fixed step in seconds. 0 in variable mode. */
	f64 step_seconds;
	/** @brief Upper bound on steps per frame in real-time mode. Stops a slow frame from causing ever more steps. */
	u32 max_steps_per_frame;
	/** @brief Wall-clock time not yet consumed by a step (real-time mode only). Always below one step after a frame begins. */
	f64 accumulator;
	/** @brief Fixed steps completed since the clock was created. */
	u64 step_count;
	/** @brief Simulated time in seconds since the clock was created (step_count * step_seconds in fixed modes). */
	f64 sim_seconds;
	/** @brief When true, no steps run except ones requested with ksim_clock_request_steps. */
	b8 paused;
	/** @brief Steps requested while paused that have not run yet. */
	u32 pending_steps;
	/** @brief Real time dropped because a frame took longer than max_steps_per_frame steps. */
	f64 dropped_seconds;
} ksim_clock;

/**
 * @brief Creates a simulation clock.
 *
 * @param mode The clock mode. If step_hz is 0, the mode is forced to variable.
 * @param step_hz Fixed steps per second. 0 means variable mode.
 * @param max_steps_per_frame Real-time mode only: the most steps one frame may run. 0 is treated as 1.
 * @return The new clock.
 */
KAPI ksim_clock ksim_clock_create (ksim_clock_mode mode, u32 step_hz, u32 max_steps_per_frame);

/**
 * @brief Starts a frame and returns how many fixed steps to run in it. Call ksim_clock_step_complete
 * once after each step runs.
 *
 * @param clock The clock.
 * @param frame_seconds Wall-clock time since the previous frame. Ignored in lockstep mode.
 * @return The number of fixed steps to run this frame. Always 0 in variable mode.
 */
KAPI u32 ksim_clock_frame_begin (ksim_clock *clock, f64 frame_seconds);

/**
 * @brief Records that one fixed step has finished, advancing step count and simulated time.
 * In variable mode, use ksim_clock_variable_advance instead.
 */
KAPI void ksim_clock_step_complete (ksim_clock *clock);

/** @brief Variable mode only: advances simulated time by one frame's time. */
KAPI void ksim_clock_variable_advance (ksim_clock *clock, f64 frame_seconds);

/**
 * @brief How far the clock is between the last completed step and the next one, from 0 to 1.
 * Renderers use it to interpolate between the previous and current simulation state.
 * Always 1 in variable and lockstep modes, and while paused.
 */
KAPI f32 ksim_clock_alpha (const ksim_clock *clock);

/** @brief Pauses or resumes stepping. Pausing in real-time mode discards accumulated time. */
KAPI void ksim_clock_pause_set (ksim_clock *clock, b8 paused);

/**
 * @brief Requests that count steps run while paused. Real-time mode runs at most
 * max_steps_per_frame of them per frame; lockstep runs one per frame. Ignored in variable mode
 * or when not paused.
 */
KAPI void ksim_clock_request_steps (ksim_clock *clock, u32 count);

/** @brief Returns a readable name for a mode, such as "real-time". */
KAPI const char *ksim_clock_mode_name (ksim_clock_mode mode);
