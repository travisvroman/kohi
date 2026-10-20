#include "sim_clock.h"

ksim_clock ksim_clock_create (ksim_clock_mode mode, u32 step_hz, u32 max_steps_per_frame) {
	ksim_clock clock = {0};
	if (step_hz == 0) {
		mode = KSIM_CLOCK_MODE_VARIABLE;
	}
	clock.mode = mode;
	clock.step_seconds = (mode == KSIM_CLOCK_MODE_VARIABLE) ? 0.0 : 1.0 / (f64)step_hz;
	clock.max_steps_per_frame = max_steps_per_frame ? max_steps_per_frame : 1;
	return clock;
}

static u32 take_pending_steps (ksim_clock *clock, u32 limit) {
	u32 steps = clock->pending_steps < limit ? clock->pending_steps : limit;
	clock->pending_steps -= steps;
	return steps;
}

u32 ksim_clock_frame_begin (ksim_clock *clock, f64 frame_seconds) {
	switch (clock->mode) {
	case KSIM_CLOCK_MODE_VARIABLE:
		return 0;

	case KSIM_CLOCK_MODE_LOCKSTEP:
		if (clock->paused) {
			return take_pending_steps(clock, 1);
		}
		return 1;

	case KSIM_CLOCK_MODE_REALTIME: {
		if (clock->paused) {
			return take_pending_steps(clock, clock->max_steps_per_frame);
		}

		if (frame_seconds < 0.0) {
			frame_seconds = 0.0;
		}
		clock->accumulator += frame_seconds;

		u32 steps = (u32)(clock->accumulator / clock->step_seconds);
		if (steps > clock->max_steps_per_frame) {
			// The frame was too slow to catch up on. Run the maximum and drop the rest, so the
			// simulation falls behind real time instead of spending ever longer catching up.
			f64 kept = (f64)clock->max_steps_per_frame * clock->step_seconds;
			clock->dropped_seconds += clock->accumulator - kept;
			clock->accumulator = kept;
			steps = clock->max_steps_per_frame;
		}
		clock->accumulator -= (f64)steps * clock->step_seconds;
		if (clock->accumulator < 0.0) {
			// Guards against floating-point error leaving a tiny negative remainder.
			clock->accumulator = 0.0;
		}
		return steps;
	}
	}
	return 0;
}

void ksim_clock_step_complete (ksim_clock *clock) {
	clock->step_count++;
	// Derived from the step count rather than summed, so it does not drift over long runs.
	clock->sim_seconds = (f64)clock->step_count * clock->step_seconds;
}

void ksim_clock_variable_advance (ksim_clock *clock, f64 frame_seconds) {
	if (clock->mode == KSIM_CLOCK_MODE_VARIABLE && !clock->paused) {
		clock->sim_seconds += frame_seconds;
	}
}

f32 ksim_clock_alpha (const ksim_clock *clock) {
	if (clock->mode != KSIM_CLOCK_MODE_REALTIME || clock->paused) {
		return 1.0f;
	}
	return (f32)(clock->accumulator / clock->step_seconds);
}

void ksim_clock_pause_set (ksim_clock *clock, b8 paused) {
	clock->paused = paused;
	clock->accumulator = 0.0;
	if (!paused) {
		clock->pending_steps = 0;
	}
}

void ksim_clock_request_steps (ksim_clock *clock, u32 count) {
	if (clock->mode == KSIM_CLOCK_MODE_VARIABLE || !clock->paused) {
		return;
	}
	clock->pending_steps += count;
}

const char *ksim_clock_mode_name (ksim_clock_mode mode) {
	switch (mode) {
	case KSIM_CLOCK_MODE_VARIABLE:
		return "variable";
	case KSIM_CLOCK_MODE_REALTIME:
		return "real-time";
	case KSIM_CLOCK_MODE_LOCKSTEP:
		return "lockstep";
	}
	return "unknown";
}
