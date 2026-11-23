/**
 * @file engine.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "gamepad.h"
#include "defines.h"
#include "input_types.h"
#include "logger.h"
#include "strings/kstring.h"

#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <utils/input_utils.h>

typedef struct gamepad {
	u32 device_id;
	u16 vendor_id, product_id;
	u32 button_states;
	u32 prev_button_states;
} gamepad;

typedef struct gamepad_system_state {
	gamepad gamepads[MAX_GAMEPADS];
} gamepad_system_state;

static gamepad_system_state *state_ptr = KNULL;

b8 gamepad_system_initialize (u64 *memory_requirement, struct gamepad_system_state *state) {
	if (!memory_requirement) {
		return false;
	}

	*memory_requirement = sizeof(gamepad_system_state);

	if (!state) {
		return true;
	}

	kzero_memory(state, sizeof(gamepad_system_state));
	for (u8 i = 0; i < MAX_GAMEPADS; ++i) {
		state->gamepads[i].device_id = INVALID_ID_U32;
	}

	state_ptr = state;

	return true;
}

void gamepad_system_shutdown (struct gamepad_system_state *state) {
	if (state) {
		//
	}
}

void gamepad_system_update (struct gamepad_system_state *state, const struct frame_data *p_frame_data) {
	if (state) {
		// NOTE: Test polling here
#if 0
		u8 player_index = 0;
		b8 nd = gamepad_is_button_pressed(player_index, GAMEPAD_BUTTON_NORTH);
		b8 sd = gamepad_is_button_pressed(player_index, GAMEPAD_BUTTON_SOUTH);
		b8 ed = gamepad_is_button_pressed(player_index, GAMEPAD_BUTTON_EAST);
		b8 wd = gamepad_is_button_pressed(player_index, GAMEPAD_BUTTON_WEST);

		vec3 left, right;
		left.x = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_LX);
		left.y = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_LY);
		left.z = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_LZ);
		right.x = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_RX);
		right.y = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_RY);
		right.z = gamepad_get_axis_state(player_index, GAMEPAD_AXIS_RZ);

		KDEBUG("Is face down: N=%u, S=%u, E=%u, W=%u, L=%V3.3f R=%V3.3f",
			   nd, sd, ed, wd, &left, &right);
#endif
	}
}

static u8 get_device_index (gamepad_system_state *state, u8 device_id) {
	for (u8 i = 0; i < MAX_GAMEPADS; ++i) {
		if (state->gamepads[i].device_id == device_id) {
			return i;
		}
	}

	return INVALID_ID_U8;
}

static b8 find_device_index (gamepad_system_state *state, u8 *out_index) {
	for (u8 i = 0; i < MAX_GAMEPADS; ++i) {
		if (state->gamepads[i].device_id == INVALID_ID_U32) {
			*out_index = i;
			return true;
		}
	}

	return false;
}

void gamepad_on_attached (u8 device_id) {
	KTRACE("Gamepad attached: %hhu", device_id);

	i32 vendor_id, product_id;
	u32 button_count, axis_count;
	if (!platform_gamepad_get_device_info(device_id, &vendor_id, &product_id, &button_count, &axis_count)) {
		KERROR("Unable to get gamepad device info.");
		return;
	}

	u8 index;
	if (!find_device_index(state_ptr, &index)) {
		KTRACE("All controller inputs are full. Not assigning this one.");
		return;
	}

	gamepad *g = &state_ptr->gamepads[index];
	g->device_id = device_id;
	g->vendor_id = (u16)vendor_id;
	g->product_id = (u16)product_id;
	g->button_states = 0;

	const char *desc = platform_gamepad_get_device_desc(device_id);
	KINFO(
		"Gamepad attached: desc='%s' device_id=%hhu, vendor_id=0x%X, product_id=0x%X, btn_cnt=%u, axis_cnt=%u",
		desc,
		device_id,
		vendor_id,
		product_id,
		button_count,
		axis_count);
	string_free(desc);

	b8 result = platform_gamepad_set_device_player_index(g->device_id, index);
	KTRACE("Set gamepad player index %s.", result ? "successfully" : "failed");
}
void gamepad_on_detached (u8 device_id) {
	u8 player_index = get_device_index(state_ptr, device_id);
	KINFO("Gamepad detached: %hhu (slot=%u)", device_id, player_index);
	state_ptr->gamepads[player_index].device_id = INVALID_ID_U32;
	// TODO: Query the platform layer for any currently-connected controllers that
	// weren't accepted by this system because it was previously full.
}
void gamepad_on_button_pressed (u8 device_id, gamepad_button button, f32 timestamp) {
	/* KINFO("Gamepad (id=%hhu) button pressed - button_id= %hhu, timestamp = %f", device_id, button_id, timestamp); */
	u8 slot = get_device_index(state_ptr, device_id);
	if (slot == INVALID_ID_U8) {
		KTRACE("%s() - Ignoring input.", __FUNCTION__);
		return;
	}
	gamepad *g = &state_ptr->gamepads[slot];
	b8 currently_pressed = FLAG_GET(g->button_states, (u32)button);
	FLAG_SET(g->prev_button_states, button, currently_pressed);
	FLAG_SET(g->button_states, button, true);
#if KOHI_DEBUG
	const char *desc = util_gamepad_get_button_desc(g->vendor_id, g->product_id, button);
	KDEBUG("Gamepad (id=%hhu) button pressed - button='%s', timestamp = %f", device_id, desc, timestamp);
#endif
}
void gamepad_on_button_released (u8 device_id, gamepad_button button, f32 timestamp) {
	/* KINFO("Gamepad (id=%hhu) button released - button_id= %hhu, timestamp = %f", device_id, button_id, timestamp); */
	u8 slot = get_device_index(state_ptr, device_id);
	if (slot == INVALID_ID_U8) {
		KTRACE("%s() - Ignoring input.", __FUNCTION__);
		return;
	}
	gamepad *g = &state_ptr->gamepads[slot];
	b8 currently_pressed = FLAG_GET(g->button_states, (u32)button);
	FLAG_SET(g->prev_button_states, button, currently_pressed);
	FLAG_SET(g->button_states, button, false);
#if KOHI_DEBUG
	const char *desc = util_gamepad_get_button_desc(g->vendor_id, g->product_id, button);
	KDEBUG("Gamepad (id=%hhu) button released - button='%s', timestamp = %f", device_id, desc, timestamp);
#endif
}
void gamepad_on_axis_changed (u8 device_id, gamepad_axis axis, f32 value, f32 last_value, f32 timestamp) {

	u8 slot = get_device_index(state_ptr, device_id);
	if (slot == INVALID_ID_U8) {
		/* KTRACE("%s() - Ignoring input.", __FUNCTION__); */
		return;
	}
	gamepad *g = &state_ptr->gamepads[slot];
	const char *desc = util_gamepad_get_axis_desc(g->vendor_id, g->product_id, axis);
	if (kabs(value) > 0.2652f) {
		KINFO(
			"Gamepad (id=%hhu) axis value changed - %s, value=%f, last_value=%f, timestamp = %f",
			device_id,
			desc,
			value,
			last_value,
			timestamp);
	}
}

b8 gamepad_is_connected (u8 player_index) {
	if (player_index > 3) {
		return false;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	return g->device_id != INVALID_ID_U32;
}

b8 gamepad_is_button_pressed (u8 player_index, gamepad_button button) {
	if (player_index > 3) {
		return false;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		// TODO: This state could be lifted out of the platform layer and tracked here instead.
		return platform_gamepad_is_button_pressed(g->device_id, button);
	}

	return false;
}

b8 gamepad_was_button_pressed (u8 player_index, gamepad_button button) {
	if (player_index < 4) {
		gamepad *g = &state_ptr->gamepads[player_index];
		if (g->device_id != INVALID_ID_U32) {
			return FLAG_GET(g->prev_button_states, (u32)button);
		}
	}

	return false;
}
b8 gamepad_is_button_just_pressed (u8 player_index, gamepad_button button) {
	if (player_index < 4) {
		gamepad *g = &state_ptr->gamepads[player_index];
		if (g->device_id != INVALID_ID_U32) {
			return (!FLAG_GET(g->prev_button_states, (u32)button) && FLAG_GET(g->button_states, (u32)button));
		}
	}

	return false;
}

f32 gamepad_get_axis_state (u8 player_index, gamepad_axis axis) {
	if (player_index > 3) {
		return 0.0f;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		return platform_gamepad_get_axis_state(g->device_id, axis);
	}

	return 0.0f;
}

b8 gamepad_get_device_info (u8 player_index, i32 *out_vendor_id, i32 *out_product_id, u32 *out_button_count, u32 *out_axis_count) {
	if (player_index > 3) {
		return false;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		return platform_gamepad_get_device_info(g->device_id, out_vendor_id, out_product_id, out_button_count, out_axis_count);
	}

	return false;
}

const char *gamepad_get_desc (u8 player_index) {
	if (player_index > 3) {
		return KNULL;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		return platform_gamepad_get_device_desc(g->device_id);
	}

	return KNULL;
}
const char *gamepad_get_button_desc (u8 player_index, gamepad_button button) {
	if (player_index > 3) {
		return KNULL;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		return util_gamepad_get_button_desc(g->vendor_id, g->product_id, button);
	}

	return KNULL;
}

const char *gamepad_get_axis_desc (u8 player_index, gamepad_axis axis) {
	if (player_index > 3) {
		return KNULL;
	}

	gamepad *g = &state_ptr->gamepads[player_index];
	if (g->device_id != INVALID_ID_U32) {
		return util_gamepad_get_axis_desc(g->vendor_id, g->product_id, axis);
	}

	return KNULL;
}
