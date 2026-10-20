#pragma once

#include "kui_system.h"
#include "kui_types.h"

/**
 * @brief A horizontal slider. The player picks a value from min to max by clicking or dragging on
 * the bar. A step above 0 snaps the value to min + n * step. A mouse drag that leaves the bar stops
 * updating the value, so the bar should be wide enough to reach comfortably.
 */
KAPI kui_control kui_slider_create (kui_state *state, const char *name, vec2 size, f32 min, f32 max, f32 step, f32 value);
KAPI void kui_slider_destroy (kui_state *state, kui_control *self);

/** @brief Sets the value, clamped to min..max and snapped to the step. Calls the callback if the value changed. */
KAPI void kui_slider_value_set (kui_state *state, kui_control self, f32 value);
KAPI f32 kui_slider_value_get (kui_state *state, kui_control self);
/** @brief Moves the value by a number of steps (negative moves left). With no step, by a hundredth of the range. */
KAPI void kui_slider_value_adjust (kui_state *state, kui_control self, i32 steps);
/** @brief Called whenever the value changes, whether by the player, the agent channel or kui_slider_value_set. */
KAPI void kui_slider_set_on_value_changed (kui_state *state, kui_control self, PFN_slider_event_callback callback);

KAPI b8 kui_slider_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_slider_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
