/**
 * @file gamepad.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Types and functions involving gamepad input.
 * @version 1.0
 * @date 2026-10-31
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#ifndef _KOHI_GAMEPAD_H_
#define _KOHI_GAMEPAD_H_

#include <defines.h>
#include <input_types.h>

struct gamepad_system_state;
struct frame_data;

/**
 * @brief Initializes the gamepad system. Call twice; once to get the memory requirement (pass 0 to state and config) and a second
 * time passing along the state and config once allocated.
 *
 * @param memory_requirement A pointer to hold the numeric amount of bytes needed for the state. Required.
 * @param state A pointer to the state. Pass 0 when getting memory requirement, otherwise pass the block of allocated memory.
 * @return True on success; otherwise false.
 */
b8 gamepad_system_initialize (u64 *memory_requirement, struct gamepad_system_state *state);

/**
 * @brief Shuts the system down.
 *
 * @param state A pointer to the state. Required.
 */
void gamepad_system_shutdown (struct gamepad_system_state *state);

void gamepad_system_update (struct gamepad_system_state *state, const struct frame_data *p_frame_data);

void gamepad_on_attached (u8 device_id);
void gamepad_on_detached (u8 device_id);
void gamepad_on_button_pressed (u8 device_id, gamepad_button button, f32 timestamp);
void gamepad_on_button_released (u8 device_id, gamepad_button button, f32 timestamp);
void gamepad_on_axis_changed (u8 device_id, gamepad_axis axis, f32 value, f32 last_value, f32 timestamp);

KAPI b8 gamepad_is_connected (u8 player_index);
KAPI b8 gamepad_is_button_pressed (u8 player_index, gamepad_button button);
KAPI f32 gamepad_get_axis_state (u8 player_index, gamepad_axis axis);

KAPI b8 gamepad_get_device_info (u8 player_index, i32 *out_vendor_id, i32 *out_product_id, u32 *out_button_count, u32 *out_axis_count);
KAPI const char *gamepad_get_desc (u8 player_index);
KAPI const char *gamepad_get_button_desc (u8 player_index, gamepad_button button);
KAPI const char *gamepad_get_axis_desc (u8 player_index, gamepad_axis axis);

#endif
