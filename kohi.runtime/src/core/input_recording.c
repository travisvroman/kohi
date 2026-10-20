#include "input_recording.h"

#include <containers/darray.h>
#include <logger.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <strings/kstring.h>

#include <stdio.h>

#include "core/engine.h"
#include "core/input.h"

#define RECORDING_VERSION 1

typedef enum recorded_input_type {
	RECORDED_INPUT_KEY,
	RECORDED_INPUT_BUTTON,
	RECORDED_INPUT_MOVE,
	RECORDED_INPUT_WHEEL
} recorded_input_type;

typedef struct recorded_input {
	// The input runs before this simulation step.
	u64 step;
	recorded_input_type type;
	// Key or button, x, or wheel delta.
	i32 a;
	// Pressed, or y.
	i32 b;
	// Key repeat.
	b8 repeat;
} recorded_input;

typedef struct input_recording_state {
	const char *record_path;
	const char *replay_path;

	FILE *record_file;

	recorded_input *replay_events; // darray
	u32 replay_cursor;
	u64 replay_end_step;
	u32 replay_fixed_step_hz;
	b8 replaying;
	// True while a replayed event is being applied, so it gets past the live-input filter.
	b8 injecting;
	b8 replay_done_reported;
} input_recording_state;

// Set from main() before the engine exists, so this lives outside engine-managed memory.
static input_recording_state state;

static const char *button_names[MOUSE_BUTTON_MAX] = {"left", "right", "middle"};

void input_recording_record_path_set (const char *path) {
	state.record_path = path;
}

void input_recording_replay_path_set (const char *path) {
	state.replay_path = path;
}

static b8 button_from_name (const char *name, i32 *out_button) {
	for (i32 i = 0; i < MOUSE_BUTTON_MAX; ++i) {
		if (strings_equali(name, button_names[i])) {
			*out_button = i;
			return true;
		}
	}
	return false;
}

static b8 key_from_text (const char *text, i32 *out_key) {
	keys key;
	if (input_key_from_name(text, &key)) {
		*out_key = (i32)key;
		return true;
	}
	i32 code = 0;
	if (string_to_i32(text, &code) && code > 0 && code < KEYS_MAX_KEYS) {
		*out_key = code;
		return true;
	}
	return false;
}

// Parses one event line ("<step> <type> <values>"). Returns false on a malformed line.
static b8 event_parse (const char *line, recorded_input *out_event) {
	unsigned long long step = 0;
	char type[16] = {0};
	char arg0[32] = {0};
	char arg1[32] = {0};
	int count = sscanf(line, "%llu %15s %31s %31s", &step, type, arg0, arg1);
	if (count < 3) {
		return false;
	}
	*out_event = (recorded_input){.step = step};

	if (strings_equal(type, "key")) {
		out_event->type = RECORDED_INPUT_KEY;
		if (count != 4 || !key_from_text(arg0, &out_event->a)) {
			return false;
		}
		if (strings_equal(arg1, "down")) {
			out_event->b = true;
		} else if (strings_equal(arg1, "repeat")) {
			out_event->b = true;
			out_event->repeat = true;
		} else if (!strings_equal(arg1, "up")) {
			return false;
		}
		return true;
	}
	if (strings_equal(type, "button")) {
		out_event->type = RECORDED_INPUT_BUTTON;
		if (count != 4 || !button_from_name(arg0, &out_event->a)) {
			return false;
		}
		if (strings_equal(arg1, "down")) {
			out_event->b = true;
		} else if (!strings_equal(arg1, "up")) {
			return false;
		}
		return true;
	}
	if (strings_equal(type, "move")) {
		out_event->type = RECORDED_INPUT_MOVE;
		return count == 4 && string_to_i32(arg0, &out_event->a) && string_to_i32(arg1, &out_event->b);
	}
	if (strings_equal(type, "wheel")) {
		out_event->type = RECORDED_INPUT_WHEEL;
		return count == 3 && string_to_i32(arg0, &out_event->a);
	}
	return false;
}

b8 input_recording_replay_load (b8 *out_has_seed, u64 *out_seed) {
	*out_has_seed = false;
	if (!state.replay_path) {
		return true;
	}

	const char *text = filesystem_read_entire_text_file(state.replay_path);
	if (!text) {
		KERROR("Cannot read the input recording '%s'.", state.replay_path);
		return false;
	}

	b8 success = true;
	b8 has_version = false;
	state.replay_events = darray_create(recorded_input);

	const char *cursor = text;
	u32 line_number = 0;
	while (*cursor && success) {
		const char *line_end = cursor;
		while (*line_end && *line_end != '\n') {
			++line_end;
		}
		++line_number;

		char line[256];
		u64 length = (u64)(line_end - cursor);
		if (length >= sizeof(line)) {
			KERROR("%s:%u: line is too long.", state.replay_path, line_number);
			success = false;
			break;
		}
		kcopy_memory(line, cursor, length);
		line[length] = 0;
		string_trim(line);
		cursor = *line_end ? line_end + 1 : line_end;

		if (line[0] == 0 || line[0] == '#') {
			continue;
		}

		unsigned long long value = 0;
		unsigned int version = 0;
		if (sscanf(line, "kohi_input_recording %u", &version) == 1) {
			if (version != RECORDING_VERSION) {
				KERROR("%s: recording version %u is not supported (expected %u).", state.replay_path, version, RECORDING_VERSION);
				success = false;
			}
			has_version = true;
		} else if (sscanf(line, "seed %llu", &value) == 1) {
			*out_has_seed = true;
			*out_seed = value;
		} else if (sscanf(line, "fixed_step_hz %llu", &value) == 1) {
			// Checked against the app config in input_recording_start.
			state.replay_fixed_step_hz = (u32)value;
		} else if (sscanf(line, "end %llu", &value) == 1) {
			state.replay_end_step = value;
		} else {
			recorded_input event;
			if (!event_parse(line, &event)) {
				KERROR("%s:%u: cannot read '%s'.", state.replay_path, line_number, line);
				success = false;
			} else {
				u32 count = darray_length(state.replay_events);
				if (count && event.step < state.replay_events[count - 1].step) {
					KERROR("%s:%u: steps must not decrease.", state.replay_path, line_number);
					success = false;
				} else {
					darray_push(state.replay_events, &event);
				}
			}
		}
	}
	string_free(text);

	if (success && !has_version) {
		KERROR("%s is not a Kohi input recording (no 'kohi_input_recording' line).", state.replay_path);
		success = false;
	}
	if (!success) {
		darray_destroy(state.replay_events);
		state.replay_events = 0;
		return false;
	}

	state.replaying = true;
	KINFO("Replaying %u input events from '%s'. Live input is ignored.", darray_length(state.replay_events), state.replay_path);
	return true;
}

b8 input_recording_start (u64 seed, u32 fixed_step_hz) {
	if ((state.replaying || state.record_path) && fixed_step_hz == 0) {
		KERROR("Input recording and replay need fixed simulation steps. Set fixed_step_hz in the app config.");
		return false;
	}
	if (state.replaying && state.replay_fixed_step_hz && state.replay_fixed_step_hz != fixed_step_hz) {
		KWARN("The recording was made at fixed_step_hz %u but this run uses %u; the replay will not match.", state.replay_fixed_step_hz, fixed_step_hz);
	}
	if (!state.record_path) {
		return true;
	}

	state.record_file = fopen(state.record_path, "w");
	if (!state.record_file) {
		KERROR("Cannot open '%s' to record input.", state.record_path);
		return false;
	}
	fprintf(state.record_file, "kohi_input_recording %u\n", RECORDING_VERSION);
	fprintf(state.record_file, "# Each line: <step> <event>. The event runs before that simulation step.\n");
	fprintf(state.record_file, "seed %llu\n", (unsigned long long)seed);
	fprintf(state.record_file, "fixed_step_hz %u\n", fixed_step_hz);
	fflush(state.record_file);
	KINFO("Recording input to '%s'.", state.record_path);
	return true;
}

void input_recording_shutdown (u64 step_count) {
	if (state.record_file) {
		fprintf(state.record_file, "end %llu\n", (unsigned long long)step_count);
		fclose(state.record_file);
		state.record_file = 0;
	}
	if (state.replay_events) {
		darray_destroy(state.replay_events);
		state.replay_events = 0;
	}
	state.replaying = false;
}

void input_recording_before_step (u64 step) {
	if (!state.replaying) {
		return;
	}

	u32 count = darray_length(state.replay_events);
	state.injecting = true;
	while (state.replay_cursor < count && state.replay_events[state.replay_cursor].step <= step) {
		const recorded_input *event = &state.replay_events[state.replay_cursor++];
		switch (event->type) {
		case RECORDED_INPUT_KEY:
			input_process_key((keys)event->a, event->b, event->repeat);
			break;
		case RECORDED_INPUT_BUTTON:
			input_process_button((mouse_buttons)event->a, event->b);
			break;
		case RECORDED_INPUT_MOVE:
			input_process_mouse_move((i16)event->a, (i16)event->b);
			break;
		case RECORDED_INPUT_WHEEL:
			input_process_mouse_wheel((i8)event->a);
			break;
		}
	}
	state.injecting = false;

	if (!state.replay_done_reported && state.replay_cursor == count) {
		if (state.replay_end_step) {
			KINFO("Input replay: all %u events applied before step %llu. The recording ran %llu steps.", count, (unsigned long long)step, (unsigned long long)state.replay_end_step);
		} else {
			KINFO("Input replay: all %u events applied before step %llu.", count, (unsigned long long)step);
		}
		state.replay_done_reported = true;
	}
}

b8 input_recording_live_input_allowed (void) {
	return !state.replaying || state.injecting;
}

static u64 current_step (void) {
	return engine_simulation_step_count_get();
}

void input_recording_key (keys key, b8 pressed, b8 is_repeat) {
	if (!state.record_file) {
		return;
	}
	const char *name = input_keycode_str(key);
	if (strings_equal(name, "undefined")) {
		fprintf(state.record_file, "%llu key %d %s\n", (unsigned long long)current_step(), (int)key, is_repeat ? "repeat" : (pressed ? "down" : "up"));
	} else {
		fprintf(state.record_file, "%llu key %s %s\n", (unsigned long long)current_step(), name, is_repeat ? "repeat" : (pressed ? "down" : "up"));
	}
	fflush(state.record_file);
}

void input_recording_button (mouse_buttons button, b8 pressed) {
	if (!state.record_file || button >= MOUSE_BUTTON_MAX) {
		return;
	}
	fprintf(state.record_file, "%llu button %s %s\n", (unsigned long long)current_step(), button_names[button], pressed ? "down" : "up");
	fflush(state.record_file);
}

void input_recording_mouse_move (i16 x, i16 y) {
	if (!state.record_file) {
		return;
	}
	fprintf(state.record_file, "%llu move %d %d\n", (unsigned long long)current_step(), x, y);
	fflush(state.record_file);
}

void input_recording_mouse_wheel (i8 z_delta) {
	if (!state.record_file) {
		return;
	}
	fprintf(state.record_file, "%llu wheel %d\n", (unsigned long long)current_step(), z_delta);
	fflush(state.record_file);
}
