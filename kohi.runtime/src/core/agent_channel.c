#include "agent_channel.h"

#include <logger.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <strings/kstring.h>
#include <threads/kmutex.h>
#include <threads/kthread.h>

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if KPLATFORM_WINDOWS
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <unistd.h>
#endif

#include "core/console.h"
#include "core/engine.h"
#include "core/event.h"
#include "core/input.h"
#include "physics/kphysics_system.h"

#define AGENT_MAX_FIELDS 16
#define AGENT_MAX_COMMANDS 64

typedef enum agent_value_type {
	AGENT_VALUE_STRING,
	AGENT_VALUE_NUMBER,
	AGENT_VALUE_BOOL,
	AGENT_VALUE_NULL
} agent_value_type;

typedef struct agent_field {
	char *key;
	agent_value_type type;
	char *string;
	f64 number;
	b8 boolean;
	/** @brief The value exactly as written, so "id" can be echoed back unchanged. */
	char *raw;
} agent_field;

struct agent_request {
	agent_field fields[AGENT_MAX_FIELDS];
	u32 field_count;
};

struct agent_reply {
	/** @brief The fields after "id" and "ok", as JSON text starting with a comma. */
	char *fields;
	char *error;
};

typedef struct agent_command {
	char *name;
	char *help;
	void *listener;
	PFN_agent_command handler;
} agent_command;

/**
 * @brief Lines read from stdin by the reader thread. The reader thread can outlive the engine
 * (it blocks in fgets), so this part uses malloc and is never freed while the thread may run.
 */
typedef struct agent_input_queue {
	kmutex lock;
	char **lines;
	u32 count;
	u32 capacity;
	b8 eof;
} agent_input_queue;

typedef struct agent_channel_state {
	b8 enabled;
	b8 started;
	/** @brief Set once stdin has closed and every request has run. */
	b8 input_closed;
	agent_command commands[AGENT_MAX_COMMANDS];
	u32 command_count;

	kthread reader;
	agent_input_queue *queue;

	/** @brief A step request waiting for its steps to run. Later requests wait behind it. */
	b8 step_waiting;
	char *step_id;
	u64 step_target;
} agent_channel_state;

static agent_channel_state state;

// JSON writing.

void agent_json_append (char **buffer, const char *text) {
	u64 old_length = *buffer ? strlen(*buffer) : 0;
	u64 add_length = strlen(text);
	char *grown = kallocate(old_length + add_length + 1, MEMORY_TAG_STRING);
	if (*buffer) {
		kcopy_memory(grown, *buffer, old_length);
		string_free(*buffer);
	}
	kcopy_memory(grown + old_length, text, add_length + 1);
	*buffer = grown;
}

void agent_json_append_string (char **buffer, const char *value) {
	agent_json_append(buffer, "\"");
	char chunk[8];
	for (const char *c = value ? value : ""; *c; ++c) {
		switch (*c) {
		case '"':
			agent_json_append(buffer, "\\\"");
			break;
		case '\\':
			agent_json_append(buffer, "\\\\");
			break;
		case '\n':
			agent_json_append(buffer, "\\n");
			break;
		case '\r':
			agent_json_append(buffer, "\\r");
			break;
		case '\t':
			agent_json_append(buffer, "\\t");
			break;
		default:
			if ((unsigned char)*c < 0x20) {
				snprintf(chunk, sizeof(chunk), "\\u%04x", (unsigned char)*c);
			} else {
				chunk[0] = *c;
				chunk[1] = 0;
			}
			agent_json_append(buffer, chunk);
			break;
		}
	}
	agent_json_append(buffer, "\"");
}

static void json_append_number (char **buffer, f64 value, int digits) {
	if (isnan(value) || isinf(value)) {
		agent_json_append(buffer, "null");
		return;
	}
	char text[64];
	snprintf(text, sizeof(text), "%.*g", digits, value);
	agent_json_append(buffer, text);
}

void agent_json_append_f64 (char **buffer, f64 value) {
	// 17 significant digits round-trip any f64.
	json_append_number(buffer, value, 17);
}

void agent_json_append_f32 (char **buffer, f32 value) {
	// 9 significant digits round-trip any f32.
	json_append_number(buffer, (f64)value, 9);
}

static void reply_key_append (agent_reply *reply, const char *key) {
	agent_json_append(&reply->fields, ",");
	agent_json_append_string(&reply->fields, key);
	agent_json_append(&reply->fields, ":");
}

void agent_reply_string_add (agent_reply *reply, const char *key, const char *value) {
	reply_key_append(reply, key);
	agent_json_append_string(&reply->fields, value);
}

void agent_reply_number_add (agent_reply *reply, const char *key, f64 value) {
	reply_key_append(reply, key);
	agent_json_append_f64(&reply->fields, value);
}

void agent_reply_int_add (agent_reply *reply, const char *key, i64 value) {
	char text[32];
	snprintf(text, sizeof(text), "%lld", (long long)value);
	reply_key_append(reply, key);
	agent_json_append(&reply->fields, text);
}

void agent_reply_bool_add (agent_reply *reply, const char *key, b8 value) {
	reply_key_append(reply, key);
	agent_json_append(&reply->fields, value ? "true" : "false");
}

void agent_reply_json_add (agent_reply *reply, const char *key, const char *json) {
	reply_key_append(reply, key);
	agent_json_append(&reply->fields, json);
}

void agent_reply_error (agent_reply *reply, const char *message) {
	if (reply->error) {
		string_free(reply->error);
	}
	reply->error = string_duplicate(message);
}

static void reply_send (const char *id, agent_reply *reply, b8 ok) {
	char *line = 0;
	agent_json_append(&line, "{\"id\":");
	agent_json_append(&line, id ? id : "null");
	if (ok) {
		agent_json_append(&line, ",\"ok\":true");
		if (reply->fields) {
			agent_json_append(&line, reply->fields);
		}
	} else {
		agent_json_append(&line, ",\"ok\":false,\"error\":");
		agent_json_append_string(&line, reply->error ? reply->error : "command failed");
	}
	agent_json_append(&line, "}\n");
	fputs(line, stdout);
	fflush(stdout);
	string_free(line);

	if (reply->fields) {
		string_free(reply->fields);
		reply->fields = 0;
	}
	if (reply->error) {
		string_free(reply->error);
		reply->error = 0;
	}
}

static void reply_error_send (const char *id, const char *message) {
	agent_reply reply = {0};
	agent_reply_error(&reply, message);
	reply_send(id, &reply, false);
}

// JSON reading: one flat object with string, number, bool or null values.

static const char *skip_space (const char *c) {
	while (*c == ' ' || *c == '\t' || *c == '\r' || *c == '\n') {
		++c;
	}
	return c;
}

/** @brief Parses a JSON string starting at the opening quote. Returns the position after it, or 0. */
static const char *parse_string (const char *c, char **out) {
	if (*c != '"') {
		return 0;
	}
	++c;
	char *value = kallocate(strlen(c) + 1, MEMORY_TAG_STRING);
	u64 length = 0;
	while (*c && *c != '"') {
		if (*c == '\\') {
			++c;
			switch (*c) {
			case 'n':
				value[length++] = '\n';
				break;
			case 't':
				value[length++] = '\t';
				break;
			case 'r':
				value[length++] = '\r';
				break;
			case 'b':
				value[length++] = '\b';
				break;
			case 'f':
				value[length++] = '\f';
				break;
			case 'u': {
				// Only ASCII is decoded; anything else becomes '?'.
				char hex[5] = {0};
				for (u32 i = 0; i < 4; ++i) {
					if (!c[1 + i]) {
						kfree(value);
						return 0;
					}
					hex[i] = c[1 + i];
				}
				long code = strtol(hex, 0, 16);
				value[length++] = code < 0x80 ? (char)code : '?';
				c += 4;
			} break;
			case 0:
				kfree(value);
				return 0;
			default:
				// \" \\ \/
				value[length++] = *c;
				break;
			}
			++c;
		} else {
			value[length++] = *c++;
		}
	}
	if (*c != '"') {
		kfree(value);
		return 0;
	}
	value[length] = 0;
	*out = value;
	return c + 1;
}

static char *copy_range (const char *start, const char *end) {
	u64 length = (u64)(end - start);
	char *copy = kallocate(length + 1, MEMORY_TAG_STRING);
	kcopy_memory(copy, start, length);
	copy[length] = 0;
	return copy;
}

static void request_free (agent_request *request) {
	for (u32 i = 0; i < request->field_count; ++i) {
		agent_field *f = &request->fields[i];
		kfree(f->key);
		kfree(f->raw);
		if (f->string) {
			kfree(f->string);
		}
	}
	request->field_count = 0;
}

static b8 request_parse (const char *line, agent_request *out, const char **out_error) {
	kzero_memory(out, sizeof(agent_request));
	const char *c = skip_space(line);
	if (*c != '{') {
		*out_error = "a request must be a JSON object";
		return false;
	}
	c = skip_space(c + 1);
	if (*c == '}') {
		return true;
	}

	while (true) {
		if (out->field_count == AGENT_MAX_FIELDS) {
			*out_error = "too many fields";
			return false;
		}
		agent_field *f = &out->fields[out->field_count];
		c = parse_string(c, &f->key);
		if (!c) {
			*out_error = "expected a quoted field name";
			return false;
		}
		c = skip_space(c);
		if (*c != ':') {
			kfree(f->key);
			*out_error = "expected ':' after a field name";
			return false;
		}
		c = skip_space(c + 1);

		const char *value_start = c;
		if (*c == '"') {
			f->type = AGENT_VALUE_STRING;
			c = parse_string(c, &f->string);
			if (!c) {
				kfree(f->key);
				*out_error = "unterminated string";
				return false;
			}
		} else if (strncmp(c, "true", 4) == 0) {
			f->type = AGENT_VALUE_BOOL;
			f->boolean = true;
			c += 4;
		} else if (strncmp(c, "false", 5) == 0) {
			f->type = AGENT_VALUE_BOOL;
			f->boolean = false;
			c += 5;
		} else if (strncmp(c, "null", 4) == 0) {
			f->type = AGENT_VALUE_NULL;
			c += 4;
		} else {
			char *end = 0;
			f->number = strtod(c, &end);
			if (end == c) {
				kfree(f->key);
				*out_error = "values must be strings, numbers, booleans or null";
				return false;
			}
			f->type = AGENT_VALUE_NUMBER;
			c = end;
		}
		f->raw = copy_range(value_start, c);
		out->field_count++;

		c = skip_space(c);
		if (*c == ',') {
			c = skip_space(c + 1);
			continue;
		}
		if (*c == '}') {
			return true;
		}
		*out_error = "expected ',' or '}'";
		return false;
	}
}

static const agent_field *field_find (const agent_request *request, const char *key) {
	for (u32 i = 0; i < request->field_count; ++i) {
		if (strings_equal(request->fields[i].key, key)) {
			return &request->fields[i];
		}
	}
	return 0;
}

b8 agent_request_string_get (const agent_request *request, const char *key, const char **out_value) {
	const agent_field *f = field_find(request, key);
	if (!f || f->type != AGENT_VALUE_STRING) {
		return false;
	}
	*out_value = f->string;
	return true;
}

b8 agent_request_number_get (const agent_request *request, const char *key, f64 *out_value) {
	const agent_field *f = field_find(request, key);
	if (!f || f->type != AGENT_VALUE_NUMBER) {
		return false;
	}
	*out_value = f->number;
	return true;
}

b8 agent_request_bool_get (const agent_request *request, const char *key, b8 *out_value) {
	const agent_field *f = field_find(request, key);
	if (!f || f->type != AGENT_VALUE_BOOL) {
		return false;
	}
	*out_value = f->boolean;
	return true;
}

// Commands.

b8 agent_command_register (const char *name, const char *help, void *listener, PFN_agent_command handler) {
	if (!state.enabled) {
		// Nothing would ever call it.
		return true;
	}
	for (u32 i = 0; i < state.command_count; ++i) {
		if (strings_equal(state.commands[i].name, name)) {
			state.commands[i].listener = listener;
			state.commands[i].handler = handler;
			return true;
		}
	}
	if (state.command_count == AGENT_MAX_COMMANDS) {
		KERROR("Too many agent commands; '%s' was not registered.", name);
		return false;
	}
	agent_command *command = &state.commands[state.command_count++];
	command->name = string_duplicate(name);
	command->help = string_duplicate(help ? help : "");
	command->listener = listener;
	command->handler = handler;
	return true;
}

static const agent_command *command_find (const char *name) {
	for (u32 i = 0; i < state.command_count; ++i) {
		if (strings_equal(state.commands[i].name, name)) {
			return &state.commands[i];
		}
	}
	return 0;
}

static void status_fields_add (agent_reply *reply) {
	agent_reply_int_add(reply, "step", (i64)engine_simulation_step_count_get());
	agent_reply_number_add(reply, "time", engine_simulation_time_get());
	agent_reply_number_add(reply, "step_seconds", engine_simulation_step_seconds_get());
	agent_reply_bool_add(reply, "paused", engine_simulation_paused());
}

static b8 cmd_commands (const agent_request *request, agent_reply *reply, void *listener) {
	char *list = 0;
	agent_json_append(&list, "[");
	for (u32 i = 0; i < state.command_count; ++i) {
		if (i) {
			agent_json_append(&list, ",");
		}
		agent_json_append(&list, "{\"name\":");
		agent_json_append_string(&list, state.commands[i].name);
		agent_json_append(&list, ",\"help\":");
		agent_json_append_string(&list, state.commands[i].help);
		agent_json_append(&list, "}");
	}
	agent_json_append(&list, "]");
	agent_reply_json_add(reply, "commands", list);
	string_free(list);
	return true;
}

static b8 cmd_status (const agent_request *request, agent_reply *reply, void *listener) {
	status_fields_add(reply);
	return true;
}

static b8 cmd_pause (const agent_request *request, agent_reply *reply, void *listener) {
	engine_simulation_pause_set(true);
	status_fields_add(reply);
	return true;
}

static b8 cmd_resume (const agent_request *request, agent_reply *reply, void *listener) {
	engine_simulation_pause_set(false);
	status_fields_add(reply);
	return true;
}

static b8 key_from_name (const char *name, keys *out_key) {
	for (u32 k = 0; k < KEYS_MAX_KEYS; ++k) {
		const char *key_name = input_keycode_str((keys)k);
		if (!strings_equal(key_name, "undefined") && strings_equali(key_name, name)) {
			*out_key = (keys)k;
			return true;
		}
	}
	return false;
}

static b8 cmd_key (const agent_request *request, agent_reply *reply, void *listener) {
	const char *name = 0;
	b8 pressed = false;
	if (!agent_request_string_get(request, "key", &name) || !agent_request_bool_get(request, "pressed", &pressed)) {
		agent_reply_error(reply, "key needs \"key\" (a key name such as \"a\" or \"space\") and \"pressed\" (true or false)");
		return false;
	}
	keys key;
	if (!key_from_name(name, &key)) {
		char *message = string_format("unknown key '%s'", name);
		agent_reply_error(reply, message);
		string_free(message);
		return false;
	}
	input_process_key(key, pressed, false);
	return true;
}

static b8 cmd_mouse_button (const agent_request *request, agent_reply *reply, void *listener) {
	const char *name = 0;
	b8 pressed = false;
	if (!agent_request_string_get(request, "button", &name) || !agent_request_bool_get(request, "pressed", &pressed)) {
		agent_reply_error(reply, "mouse_button needs \"button\" (left, right or middle) and \"pressed\" (true or false)");
		return false;
	}
	mouse_buttons button;
	if (strings_equali(name, "left")) {
		button = MOUSE_BUTTON_LEFT;
	} else if (strings_equali(name, "right")) {
		button = MOUSE_BUTTON_RIGHT;
	} else if (strings_equali(name, "middle")) {
		button = MOUSE_BUTTON_MIDDLE;
	} else {
		agent_reply_error(reply, "button must be left, right or middle");
		return false;
	}
	input_process_button(button, pressed);
	return true;
}

static b8 cmd_mouse_move (const agent_request *request, agent_reply *reply, void *listener) {
	f64 x = 0, y = 0;
	if (!agent_request_number_get(request, "x", &x) || !agent_request_number_get(request, "y", &y)) {
		agent_reply_error(reply, "mouse_move needs \"x\" and \"y\" in window pixels");
		return false;
	}
	input_process_mouse_move((i16)x, (i16)y);
	return true;
}

static b8 cmd_mouse_wheel (const agent_request *request, agent_reply *reply, void *listener) {
	f64 delta = 0;
	if (!agent_request_number_get(request, "delta", &delta)) {
		agent_reply_error(reply, "mouse_wheel needs \"delta\" (positive scrolls up)");
		return false;
	}
	input_process_mouse_wheel((i8)delta);
	return true;
}

static b8 cmd_console (const agent_request *request, agent_reply *reply, void *listener) {
	const char *line = 0;
	if (!agent_request_string_get(request, "line", &line)) {
		agent_reply_error(reply, "console needs \"line\", a console command such as \"sim_status\"");
		return false;
	}
	if (!console_command_execute(line)) {
		agent_reply_error(reply, "console command failed; see the log on stderr");
		return false;
	}
	return true;
}

static b8 cmd_physics_hash (const agent_request *request, agent_reply *reply, void *listener) {
	struct kphysics_system_state *physics = engine_systems_get()->physics_system;
	if (!physics) {
		agent_reply_error(reply, "the physics system is not running");
		return false;
	}
	char hash[24];
	snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)kphysics_state_hash(physics));
	agent_reply_string_add(reply, "hash", hash);
	agent_reply_int_add(reply, "step", (i64)engine_simulation_step_count_get());
	return true;
}

static b8 cmd_quit (const agent_request *request, agent_reply *reply, void *listener) {
	event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){0});
	return true;
}

/** @brief Registers a built-in command unless the application already registered one with that name. */
static void builtin_register (const char *name, const char *help, void *listener, PFN_agent_command handler) {
	if (!command_find(name)) {
		agent_command_register(name, help, listener, handler);
	}
}

static void builtin_commands_register (void) {
	builtin_register("commands", "Lists the available commands.", 0, cmd_commands);
	builtin_register("status", "Returns the simulation step, time, step length and whether it is paused.", 0, cmd_status);
	// "step" is handled by the channel itself, because its reply waits for the steps to run.
	builtin_register("step", "Runs \"count\" simulation steps (default 1), then replies with the status.", 0, 0);
	builtin_register("pause", "Pauses the simulation.", 0, cmd_pause);
	builtin_register("resume", "Lets the simulation run on its own clock.", 0, cmd_resume);
	builtin_register("key", "Presses or releases a key: {\"key\": \"w\", \"pressed\": true}.", 0, cmd_key);
	builtin_register("mouse_button", "Presses or releases a mouse button: {\"button\": \"left\", \"pressed\": true}.", 0, cmd_mouse_button);
	builtin_register("mouse_move", "Moves the mouse to {\"x\", \"y\"} in window pixels.", 0, cmd_mouse_move);
	builtin_register("mouse_wheel", "Scrolls the mouse wheel by \"delta\".", 0, cmd_mouse_wheel);
	builtin_register("console", "Runs a console command given as \"line\".", 0, cmd_console);
	builtin_register("physics_hash", "Returns a hash of the full physics state.", 0, cmd_physics_hash);
	builtin_register("quit", "Shuts the engine down.", 0, cmd_quit);
}

// Reader thread.

static b8 input_queue_push (agent_input_queue *queue, char *line) {
	kmutex_lock(&queue->lock);
	if (queue->count == queue->capacity) {
		u32 capacity = queue->capacity ? queue->capacity * 2 : 16;
		char **grown = realloc(queue->lines, sizeof(char *) * capacity);
		if (!grown) {
			kmutex_unlock(&queue->lock);
			return false;
		}
		queue->lines = grown;
		queue->capacity = capacity;
	}
	queue->lines[queue->count++] = line;
	kmutex_unlock(&queue->lock);
	return true;
}

/**
 * @brief Reads raw bytes from stdin. Returns the count, or 0 at end of input.
 * Not stdio: fgets would hold the stdin lock while blocked, and exit() waits on that lock.
 */
static u64 stdin_read (char *buffer, u64 capacity) {
#if KPLATFORM_WINDOWS
	DWORD count = 0;
	if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, (DWORD)capacity, &count, 0)) {
		return 0;
	}
	return count;
#else
	while (true) {
		ssize_t count = read(STDIN_FILENO, buffer, capacity);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		return count > 0 ? (u64)count : 0;
	}
#endif
}

/** @brief Queues one line, dropping a trailing '\r'. Takes ownership of line. */
static void line_finish (agent_input_queue *queue, char *line, u64 length) {
	if (length && line[length - 1] == '\r') {
		line[--length] = 0;
	}
	if (length) {
		input_queue_push(queue, line);
	} else {
		free(line);
	}
}

static b8 reader_thread_run (void *params) {
	agent_input_queue *queue = params;
	char chunk[4096];
	char *line = 0;
	u64 length = 0;
	u64 capacity = 0;
	u64 count;
	while ((count = stdin_read(chunk, sizeof(chunk))) > 0) {
		for (u64 i = 0; i < count; ++i) {
			if (chunk[i] == '\n') {
				line_finish(queue, line ? line : calloc(1, 1), length);
				line = 0;
				length = 0;
				capacity = 0;
				continue;
			}
			if (length + 2 > capacity) {
				u64 new_capacity = capacity ? capacity * 2 : 256;
				char *grown = realloc(line, new_capacity);
				if (!grown) {
					break;
				}
				line = grown;
				capacity = new_capacity;
			}
			line[length++] = chunk[i];
			line[length] = 0;
		}
	}
	if (line) {
		line_finish(queue, line, length);
	}
	kmutex_lock(&queue->lock);
	queue->eof = true;
	kmutex_unlock(&queue->lock);
	return true;
}

/** @brief Takes the oldest queued line, or returns 0. Sets *out_eof once stdin is closed and drained. */
static char *input_queue_pop (agent_input_queue *queue, b8 *out_eof) {
	char *line = 0;
	kmutex_lock(&queue->lock);
	if (queue->count) {
		line = queue->lines[0];
		memmove(queue->lines, queue->lines + 1, sizeof(char *) * (queue->count - 1));
		queue->count--;
	}
	*out_eof = queue->eof && !line;
	kmutex_unlock(&queue->lock);
	return line;
}

// Channel.

void agent_channel_enable (void) {
	state.enabled = true;
	platform_console_stderr_only_set(true);
}

b8 agent_channel_enabled (void) {
	return state.enabled;
}

b8 agent_channel_start (void) {
	if (!state.enabled || state.started) {
		return true;
	}
	if (engine_simulation_step_seconds_get() <= 0.0) {
		KERROR("Agent mode needs fixed simulation steps. Set fixed_step_hz in the app config.");
		return false;
	}

	builtin_commands_register();

	state.queue = calloc(1, sizeof(agent_input_queue));
	if (!state.queue || !kmutex_create(&state.queue->lock)) {
		KERROR("Failed to create the agent input queue.");
		return false;
	}
	// Detached: it blocks reading stdin and is left running at shutdown.
	if (!kthread_create(reader_thread_run, state.queue, true, &state.reader)) {
		KERROR("Failed to start the agent input thread.");
		return false;
	}

	// The agent decides when the simulation advances.
	engine_simulation_pause_set(true);
	state.started = true;
	KINFO("Agent channel ready: JSON requests on stdin, replies on stdout, logs on stderr.");
	return true;
}

void agent_channel_shutdown (void) {
	if (!state.started) {
		return;
	}
	if (state.step_id) {
		reply_error_send(state.step_id, "the engine shut down before the steps ran");
		kfree(state.step_id);
		state.step_id = 0;
	}
	for (u32 i = 0; i < state.command_count; ++i) {
		string_free(state.commands[i].name);
		string_free(state.commands[i].help);
	}
	state.command_count = 0;
	// The queue stays allocated: the reader thread may still be blocked on stdin.
	state.started = false;
}

static void step_reply_send (void) {
	agent_reply reply = {0};
	status_fields_add(&reply);
	reply_send(state.step_id, &reply, true);
	kfree(state.step_id);
	state.step_id = 0;
	state.step_waiting = false;
}

/** @brief Starts a step request. Returns false if it failed and was answered already. */
static b8 step_begin (const agent_request *request, const char *id) {
	f64 count = 1;
	if (agent_request_number_get(request, "count", &count) && (count < 0 || count > 1e9)) {
		reply_error_send(id, "count must be between 0 and 1000000000");
		return false;
	}
	engine_simulation_pause_set(true);
	u64 steps = (u64)count;
	state.step_target = engine_simulation_step_count_get() + engine_simulation_pending_steps_get() + steps;
	engine_simulation_steps_request((u32)steps);
	state.step_id = string_duplicate(id ? id : "null");
	state.step_waiting = true;
	return true;
}

static void request_run (const char *line) {
	agent_request request;
	const char *parse_error = 0;
	if (!request_parse(line, &request, &parse_error)) {
		char *message = string_format("invalid request: %s", parse_error);
		reply_error_send(0, message);
		string_free(message);
		request_free(&request);
		return;
	}

	const agent_field *id_field = field_find(&request, "id");
	const char *id = id_field ? id_field->raw : 0;

	const char *name = 0;
	if (!agent_request_string_get(&request, "cmd", &name)) {
		reply_error_send(id, "a request needs a \"cmd\" field");
		request_free(&request);
		return;
	}

	if (strings_equal(name, "step")) {
		step_begin(&request, id);
		request_free(&request);
		return;
	}

	const agent_command *command = command_find(name);
	if (!command || !command->handler) {
		char *message = string_format("unknown command '%s'; send {\"cmd\": \"commands\"} for the list", name);
		reply_error_send(id, message);
		string_free(message);
		request_free(&request);
		return;
	}

	agent_reply reply = {0};
	b8 ok = command->handler(&request, &reply, command->listener);
	reply_send(id, &reply, ok);
	request_free(&request);
}

void agent_channel_update (void) {
	if (!state.started || state.input_closed) {
		return;
	}

	if (state.step_waiting) {
		if (engine_simulation_step_count_get() < state.step_target) {
			return;
		}
		step_reply_send();
	}

	// Run requests in order until one has to wait for simulation steps.
	while (!state.step_waiting) {
		b8 eof = false;
		char *line = input_queue_pop(state.queue, &eof);
		if (!line) {
			if (eof) {
				KINFO("Agent input closed; shutting down.");
				event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){0});
				state.input_closed = true;
			}
			return;
		}
		request_run(line);
		free(line);
	}
}
