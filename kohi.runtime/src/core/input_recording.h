/**
 * @file input_recording.h
 * @brief Records keyboard and mouse input per simulation step to a text file, and replays it.
 *
 * Start the engine with `--record <path>` to write every key, mouse button, mouse move and
 * wheel event, live or injected by an agent, with the simulation step it arrived before. Start
 * it with `--replay <path>` to feed those events back before the same steps, ignoring live
 * input. With the recording's seed (applied automatically) and fixed steps, a replay reaches
 * the same state as the recorded run.
 *
 * The file is plain text, one entry per line, so agents can also write one by hand:
 *
 *   kohi_input_recording 1
 *   seed 42
 *   fixed_step_hz 60
 *   0 key space down
 *   12 key space up
 *   30 move 640 360
 *   30 button left down
 *   31 button left up
 *   45 wheel -1
 *   end 600
 *
 * The number is the step the event runs before (0 runs before the first step). Keys use the
 * names from input_keycode_str, or a key code number. Lines starting with '#' are comments.
 *
 * Events between the same two steps replay in one frame. A lockstep recording replays exactly;
 * in real-time mode, per-frame queries such as input_was_key_down can see different frames.
 */
#pragma once

#include <defines.h>

#include "input_types.h"

/** @brief Record input to path. Called from main() for --record, before engine_create. */
KAPI void input_recording_record_path_set (const char *path);

/** @brief Replay input from path. Called from main() for --replay, before engine_create. */
KAPI void input_recording_replay_path_set (const char *path);

// Engine-only.

/**
 * @brief Loads the replay file, if one was set. Called before seeding the RNGs.
 * @param out_has_seed Set to true when the recording names a seed.
 * @param out_seed The recording's seed.
 * @returns False if a replay was requested but could not be loaded.
 */
b8 input_recording_replay_load (b8 *out_has_seed, u64 *out_seed);

/** @brief Checks the replay against the config, and opens the record file and writes its header, if set. */
b8 input_recording_start (u64 seed, u32 fixed_step_hz);

/** @brief Writes the end line and closes the record file. */
void input_recording_shutdown (u64 step_count);

/** @brief Applies the replayed events that run before this step. Called before each step. */
void input_recording_before_step (u64 step);

/** @brief False while replaying, so live input is ignored. Replayed events are let through. */
b8 input_recording_live_input_allowed (void);

/** @brief Records one input event, if recording. Called by the input system. */
void input_recording_key (keys key, b8 pressed, b8 is_repeat);
void input_recording_button (mouse_buttons button, b8 pressed);
void input_recording_mouse_move (i16 x, i16 y);
void input_recording_mouse_wheel (i8 z_delta);
