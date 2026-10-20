#include "sim_clock_tests.h"

#include <core/sim_clock.h>
#include <defines.h>

#include "../expect.h"
#include "../test_manager.h"

// Runs a frame and completes every step it asks for, like the engine loop does.
static u32 run_frame (ksim_clock *clock, f64 frame_seconds) {
	u32 steps = ksim_clock_frame_begin(clock, frame_seconds);
	for (u32 i = 0; i < steps; ++i) {
		ksim_clock_step_complete(clock);
	}
	return steps;
}

static u8 sim_clock_zero_hz_is_variable (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, 0, 8);
	expect_should_be(KSIM_CLOCK_MODE_VARIABLE, clock.mode);
	expect_should_be(0, run_frame(&clock, 0.5));
	expect_float_to_be(1.0f, ksim_clock_alpha(&clock));
	return true;
}

// The point of the accumulator: the same wall-clock time gives the same number of steps,
// whatever the frame rate.
static u8 sim_clock_realtime_steps_independent_of_frame_rate (void) {
	const u32 hz = 60;

	ksim_clock fast = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, hz, 8);
	for (u32 i = 0; i < 240; ++i) {
		run_frame(&fast, 1.0 / 240.0); // 1 s at 240 FPS
	}

	ksim_clock slow = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, hz, 8);
	for (u32 i = 0; i < 30; ++i) {
		run_frame(&slow, 1.0 / 30.0); // 1 s at 30 FPS
	}

	// Floating-point remainders may leave the last step in the accumulator.
	expect_should_be(1, (fast.step_count >= 59 && fast.step_count <= 60));
	expect_should_be(1, (slow.step_count >= 59 && slow.step_count <= 60));
	return true;
}

static u8 sim_clock_realtime_alpha_tracks_remainder (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, 10, 8); // 0.1 s steps
	expect_should_be(0, run_frame(&clock, 0.05));
	expect_float_to_be(0.5f, ksim_clock_alpha(&clock));
	expect_should_be(1, run_frame(&clock, 0.075));
	expect_float_to_be(0.25f, ksim_clock_alpha(&clock));
	return true;
}

static u8 sim_clock_realtime_caps_steps_per_frame (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, 100, 4);
	// A 1 s hitch would need 100 steps; only 4 run and the rest is dropped.
	expect_should_be(4, run_frame(&clock, 1.0));
	expect_float_to_be(0.96f, (f32)clock.dropped_seconds);
	// The next normal frame is not burdened by the hitch.
	expect_should_be(1, run_frame(&clock, 0.01));
	return true;
}

static u8 sim_clock_lockstep_ignores_wall_clock (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_LOCKSTEP, 50, 8);
	expect_should_be(1, run_frame(&clock, 0.0));
	expect_should_be(1, run_frame(&clock, 10.0));
	expect_should_be(1, run_frame(&clock, 0.001));
	expect_should_be(3, clock.step_count);
	expect_float_to_be(0.06f, (f32)clock.sim_seconds);
	return true;
}

static u8 sim_clock_pause_and_step_requests (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_LOCKSTEP, 60, 8);
	ksim_clock_pause_set(&clock, true);
	expect_should_be(0, run_frame(&clock, 0.0));

	ksim_clock_request_steps(&clock, 2);
	expect_should_be(1, run_frame(&clock, 0.0));
	expect_should_be(1, run_frame(&clock, 0.0));
	expect_should_be(0, run_frame(&clock, 0.0));
	expect_should_be(2, clock.step_count);

	// Real-time mode runs requested steps in batches of up to max_steps_per_frame,
	// and does not build up wall-clock time while paused.
	ksim_clock rt = ksim_clock_create(KSIM_CLOCK_MODE_REALTIME, 60, 4);
	ksim_clock_pause_set(&rt, true);
	expect_should_be(0, run_frame(&rt, 5.0));
	ksim_clock_request_steps(&rt, 6);
	expect_should_be(4, run_frame(&rt, 5.0));
	expect_should_be(2, run_frame(&rt, 5.0));
	ksim_clock_pause_set(&rt, false);
	expect_should_be(1, run_frame(&rt, 1.0 / 60.0 + 0.0001));
	return true;
}

static u8 sim_clock_sim_time_does_not_drift (void) {
	ksim_clock clock = ksim_clock_create(KSIM_CLOCK_MODE_LOCKSTEP, 60, 8);
	for (u32 i = 0; i < 60 * 60 * 10; ++i) { // ten simulated minutes
		run_frame(&clock, 0.0);
	}
	expect_should_be(1, (clock.sim_seconds == 600.0));
	return true;
}

void sim_clock_register_tests (void) {
	test_manager_register_test(sim_clock_zero_hz_is_variable, "sim_clock: 0 Hz falls back to a variable time step.");
	test_manager_register_test(sim_clock_realtime_steps_independent_of_frame_rate, "sim_clock: real-time step count does not depend on frame rate.");
	test_manager_register_test(sim_clock_realtime_alpha_tracks_remainder, "sim_clock: alpha is the leftover fraction of a step.");
	test_manager_register_test(sim_clock_realtime_caps_steps_per_frame, "sim_clock: slow frames are capped at max_steps_per_frame.");
	test_manager_register_test(sim_clock_lockstep_ignores_wall_clock, "sim_clock: lockstep runs one step per frame regardless of wall time.");
	test_manager_register_test(sim_clock_pause_and_step_requests, "sim_clock: pause stops steps; requested steps run while paused.");
	test_manager_register_test(sim_clock_sim_time_does_not_drift, "sim_clock: simulated time is exact after many steps.");
}
