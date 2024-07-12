/**
 * @file input.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains everything having to do with input on deskop
 * environments from keyboards and mice. Gamepads and touch controls will
 * likely be handled separately at a future date.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */
#pragma once

#include "defines.h"
#include "input_types.h"

struct keymap;
struct frame_data;

/**
 * @brief Initializes the input system. Call twice; once to obtain memory requirement (passing
 * state = 0), then a second time passing allocated memory to state.
 *
 * @param memory_requirement The required size of the state memory.
 * @param state Either 0 or the allocated block of state memory.
 * @param config Ignored.
 * @returns True on success; otherwise false.
 */
b8 input_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts the input system down.
 * @param state A pointer to the system state.
 */
void input_system_shutdown(void* state);

/**
 * @brief Updates the input system every frame.
 * @param p_frame_data A constant pointer to the current frame's data.
 * NOTE: Does not use system manager update because it must be called at end of a frame.
 */
void input_update(const struct frame_data* p_frame_data);

// keyboard input

/**
 * @brief Enables/disables keyboard key repeats.
 * @param enable Indicates if key repeats should be enabled.
 */
KAPI void input_key_repeats_enable(b8 enable);

/**
 * @brief Indicates if the given key is currently pressed down.
 * @param key They key to be checked.
 * @returns True if currently pressed; otherwise false.
 */
KAPI b8 input_is_key_down(keys key);

/**
 * @brief Indicates if the given key is NOT currently pressed down.
 * @param key They key to be checked.
 * @returns True if currently released; otherwise false.
 */
KAPI b8 input_is_key_up(keys key);

/**
 * @brief Indicates if the given key was previously pressed down on the last frame.
 * @param key They key to be checked.
 * @returns True if was previously pressed; otherwise false.
 */
KAPI b8 input_was_key_down(keys key);

/**
 * @brief Indicates if the given key was previously pressed down in the last frame.
 * @param key They key to be checked.
 * @returns True if previously released; otherwise false.
 */
KAPI b8 input_was_key_up(keys key);

/**
 * @brief Sets the state for the given key.
 * @param key The key to be processed.
 * @param pressed Indicates whether the key is currently pressed.
 */
void input_process_key(keys key, b8 pressed);

// mouse input

/**
 * @brief Indicates if the given mouse button is currently pressed.
 * @param button The button to check.
 * @returns True if currently pressed; otherwise false.
 */
KAPI b8 input_is_button_down(mouse_buttons button);

/**
 * @brief Indicates if the given mouse button is currently released.
 * @param button The button to check.
 * @returns True if currently released; otherwise false.
 */
KAPI b8 input_is_button_up(mouse_buttons button);

/**
 * @brief Indicates if the given mouse button was previously pressed in the last frame.
 * @param button The button to check.
 * @returns True if previously pressed; otherwise false.
 */
KAPI b8 input_was_button_down(mouse_buttons button);

/**
 * @brief Indicates if the given mouse button was previously released in the last frame.
 * @param button The button to check.
 * @returns True if previously released; otherwise false.
 */
KAPI b8 input_was_button_up(mouse_buttons button);

/**
 * @brief Indicates if the mouse is currently being dragged with the provided button
 * being held down.
 *
 * @param button The button to check.
 * @returns True if dragging; otherwise false.
 */
KAPI b8 input_is_button_dragging(mouse_buttons button);

/**
 * @brief Obtains the current mouse position.
 * @param x A pointer to hold the current mouse position on the x-axis.
 * @param y A pointer to hold the current mouse position on the y-axis.
 */
KAPI void input_get_mouse_position(i32* x, i32* y);

/**
 * @brief Obtains the previous mouse position.
 * @param x A pointer to hold the previous mouse position on the x-axis.
 * @param y A pointer to hold the previous mouse position on the y-axis.
 */
KAPI void input_get_previous_mouse_position(i32* x, i32* y);

/**
 * @brief Sets the press state of the given mouse button.
 * @param button The mouse button whose state to set.
 * @param pressed Indicates if the mouse button is currently pressed.
 */
void input_process_button(mouse_buttons button, b8 pressed);

/**
 * @brief Sets the current position of the mouse to the given x and y positions.
 * Also updates the previous position beforehand.
 */
void input_process_mouse_move(i16 x, i16 y);

/**
 * @brief Processes mouse wheel scrolling.
 * @param z_delta The amount of scrolling which occurred on the z axis (mouse wheel)
 */
void input_process_mouse_wheel(i8 z_delta);

/**
 * @brief Returns a string representation of the provided key. Ex. "tab" for the tab key.
 *
 * @param key
 * @return const char*
 */
KAPI const char* input_keycode_str(keys key);

/**
 * @brief Pushes a new keymap onto the keymap stack, making it the active keymap.
 * A copy of the keymap is taken when pushing onto the stack.
 *
 * @param map A constant pointer to the keymap to be pushed.
 */
KAPI void input_keymap_push(const struct keymap* map);

/**
 * @brief Attempts to pop the top-most keymap from the stack, if there is one.
 *
 * @return True if a keymap was popped; otherwise false.
 */
KAPI b8 input_keymap_pop(void);
