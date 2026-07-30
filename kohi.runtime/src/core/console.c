#include "console.h"

#include "containers/darray.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"
#include "strings/kstring_id.h"
#include "threads/kmutex.h"

typedef struct console_consumer {
	PFN_console_consumer_write callback;
	void *instance;
} console_consumer;

typedef struct console_command {
	const char *name;
	u8 min_arg_count;
	u8 max_arg_count;
	PFN_console_command func;
	void *listener;
} console_command;

typedef struct console_object {
	const char *name;
	console_object_type type;
	void *block;
	// darray
	struct console_object *properties;
} console_object;

typedef struct console_state {
	// darray of registered console consumers.
	console_consumer *consumers;

	// darray of registered commands.
	console_command *registered_commands;

	// darray of registered console objects.
	console_object *registered_objects;

	kmutex write_mutex;
} console_state;

static b8 on_exec (console_state *state, const char *exec_text);

static console_state *state_ptr;

b8 console_initialize (u64 *memory_requirement, struct console_state *state, void *config) {
	*memory_requirement = sizeof(console_state);

	if (!state) {
		return true;
	}

	kzero_memory(state, *memory_requirement);
	state_ptr = state;
	state->consumers = darray_create(console_consumer);

	state->registered_commands = darray_create(console_command);
	state->registered_objects = darray_create(console_object);

	// Tell the logger about the console.
	logger_console_write_hook_set(console_write);

	kmutex_create(&state->write_mutex);

	return true;
}

void console_shutdown (struct console_state *state) {
	if (state) {
		kmutex_lock(&state->write_mutex);
		u32 command_count = darray_length(state->registered_commands);
		for (u32 i = 0; i < command_count; ++i) {
			if (state->registered_commands[i].name) {
				string_free(state->registered_commands[i].name);
			}
		}

		darray_destroy(state->registered_commands);
		darray_destroy(state->registered_objects);

		darray_destroy(state->consumers);
		kmutex_unlock(&state->write_mutex);

		kmutex_destroy(&state->write_mutex);
	}

	state_ptr = 0;
}

void console_consumer_register (void *inst, PFN_console_consumer_write callback, u8 *out_consumer_id) {
	if (state_ptr) {
		kmutex_lock(&state_ptr->write_mutex);
		*out_consumer_id = INVALID_ID_U8;
		u8 len = darray_length(state_ptr->consumers);
		for (u32 i = 0; i < len; ++i) {
			if (!state_ptr->consumers[i].callback) {
				*out_consumer_id = i;
				break;
			}
		}
		if (*out_consumer_id == INVALID_ID_U8) {
			darray_push(state_ptr->consumers, &(console_consumer){0});
			*out_consumer_id = len;
		}

		console_consumer *consumer = &state_ptr->consumers[*out_consumer_id];
		consumer->instance = inst;
		consumer->callback = callback;
		kmutex_unlock(&state_ptr->write_mutex);
	}
}

void console_consumer_unregister (u8 consumer_id) {
	if (state_ptr) {
		kmutex_lock(&state_ptr->write_mutex);
		console_consumer *consumer = &state_ptr->consumers[consumer_id];
		consumer->instance = KNULL;
		consumer->callback = KNULL;
		kmutex_unlock(&state_ptr->write_mutex);
	}
}

void console_consumer_update (u8 consumer_id, void *inst, PFN_console_consumer_write callback) {
	if (state_ptr) {
		KASSERT_MSG(consumer_id < darray_length(state_ptr->consumers), "Consumer id is invalid.");

		console_consumer *consumer = &state_ptr->consumers[consumer_id];
		consumer->instance = inst;
		consumer->callback = callback;
	}
}

void console_write (log_level level, const char *message) {
	if (state_ptr) {
		kmutex_lock(&state_ptr->write_mutex);
		// Notify each consumer that a line has been added.
		u8 len = darray_length(state_ptr->consumers);
		for (u8 i = 0; i < len; ++i) {
			console_consumer *consumer = &state_ptr->consumers[i];
			if (consumer->callback) {
				consumer->callback(consumer->instance, level, message);
			}
		}
		kmutex_unlock(&state_ptr->write_mutex);
	}
}

b8 console_command_register (const char *command, u8 min_arg_count, u8 max_arg_count, void *listener, PFN_console_command func) {
	KASSERT_MSG(state_ptr && command, "console_register_command requires state and valid command");
	KASSERT(string_length(command));

	// Make sure it doesn't already exist.
	u32 command_count = darray_length(state_ptr->registered_commands);
	for (u32 i = 0; i < command_count; ++i) {
		if (strings_equali(state_ptr->registered_commands[i].name, command)) {
			KERROR("Command already registered: %s", command);
			return false;
		}
	}

	console_command new_command = {
		.min_arg_count = min_arg_count,
		.max_arg_count = max_arg_count,
		.func = func,
		.name = string_duplicate(command),
		.listener = listener,
	};

	darray_push(state_ptr->registered_commands, &new_command);

	return true;
}

b8 console_command_unregister (const char *command) {
	KASSERT_MSG(state_ptr && command, "console_update_command requires state and valid command");

	// Make sure it doesn't already exist.
	u32 command_count = darray_length(state_ptr->registered_commands);
	for (u32 i = 0; i < command_count; ++i) {
		if (strings_equali(state_ptr->registered_commands[i].name, command)) {
			// Command found, remove it.
			console_command popped_command = {0};
			darray_pop_at(state_ptr->registered_commands, i, &popped_command);
			if (popped_command.name) {
				string_free(popped_command.name);
			}
			return true;
		}
	}

	return false;
}

static console_object *console_object_get (console_object *parent, const char *name) {
	if (parent) {
		u32 property_count = darray_length(parent->properties);
		for (u32 i = 0; i < property_count; ++i) {
			console_object *obj = &parent->properties[i];
			if (strings_equali(obj->name, name)) {
				return obj;
			}
		}
	} else {
		u32 registered_object_len = darray_length(state_ptr->registered_objects);
		for (u32 i = 0; i < registered_object_len; ++i) {
			console_object *obj = &state_ptr->registered_objects[i];
			if (strings_equali(obj->name, name)) {
				return obj;
			}
		}
	}
	return 0;
}

/* static u32 console_object_to_u32(const console_object* obj) {
	return *((u32*)obj->block);
}
static i32 console_object_to_i32(const console_object* obj) {
	return *((i32*)obj->block);
}
static f32 console_object_to_f32(const console_object* obj) {
	return *((f32*)obj->block);
}
static b8 console_object_to_b8(const console_object* obj) {
	return *((b8*)obj->block);
} */

static void console_object_print (u8 indent, console_object *obj) {
	char indent_buffer[513] = {0};
	u16 idx = 0;
	for (; idx < (indent * 2); idx += 2) {
		indent_buffer[idx + 0] = ' ';
		indent_buffer[idx + 1] = ' ';
	}
	indent_buffer[idx] = 0;

	switch (obj->type) {
	case CONSOLE_OBJECT_TYPE_INT32:
		KINFO("%s%i", indent_buffer, *((i32 *)obj->block));
		break;
	case CONSOLE_OBJECT_TYPE_UINT32:
		KINFO("%s%u", indent_buffer, *((u32 *)obj->block));
		break;
	case CONSOLE_OBJECT_TYPE_F32:
		KINFO("%s%f", indent_buffer, *((f32 *)obj->block));
		break;
	case CONSOLE_OBJECT_TYPE_BOOL: {
		b8 val = *((b8 *)obj->block);
		KINFO("%s%s", indent_buffer, val ? "true" : "false");
	} break;
	case CONSOLE_OBJECT_TYPE_STRUCT:
		if (obj->properties) {
			KINFO("%s", obj->name);
			indent++;
			u32 len = darray_length(obj->properties);
			for (u32 i = 0; i < len; ++i) {
				console_object_print(indent, &obj->properties[i]);
			}
		}
		break;
	}
}

b8 console_command_execute (const char *command) {
	if (!command) {
		return false;
	}
	// If executing code, short-circuit to that directly.
	// This lets all processing after the exec command be handled
	// by the exec processor.
	if (strings_nequali(command, "exec ", 5)) {
		u32 len = string_length(command);
		char *exec_text = KALLOC_TYPE_CARRAY(char, len - 5 + 1);
		for (u32 i = 0; i < len - 5; ++i) {
			exec_text[i] = command[i + 5];
		}
		exec_text[len - 5] = 0;
		b8 result = on_exec(state_ptr, exec_text);
		string_free(exec_text);
		return result;
	}

	// Otherwise, process this the normal way.
	b8 has_error = true;
	char *temp = 0;
	char **parts = darray_create(char *);
	u32 part_count = string_split(command, ' ', &parts, true, false, true);
	if (part_count < 1) {
		has_error = true;
		goto console_command_execute_cleanup;
	}

	// Write the line back out to the console for reference.
	temp = string_format("-->%s\n", command);
	console_write(LOG_LEVEL_INFO, temp);
	string_free(temp);
	temp = 0;

	// Yep, strings are slow. But it's a console. It doesn't need to be lightning fast...
	b8 command_found = false;
	u32 command_count = darray_length(state_ptr->registered_commands);
	// Look through registered commands for a match.
	for (u32 i = 0; i < command_count; ++i) {
		console_command *cmd = &state_ptr->registered_commands[i];
		if (strings_equali(cmd->name, parts[0])) {
			command_found = true;
			u8 arg_count = part_count - 1;
			// Must be in range of required number of args
			if (arg_count < state_ptr->registered_commands[i].min_arg_count || arg_count > state_ptr->registered_commands[i].max_arg_count) {
				KERROR("The console command '%s' requires argumet count between %u and %u but %u were provided.", cmd->name, cmd->min_arg_count, cmd->max_arg_count, arg_count);
				has_error = true;
			} else {
				// Execute it, passing along arguments if needed.
				console_command_context context = {0};
				context.command = string_duplicate(command);
				context.command_name = string_duplicate(cmd->name);
				context.argument_count = arg_count;
				if (context.argument_count > 0) {
					context.arguments = KALLOC_TYPE_CARRAY(console_command_argument, arg_count);
					for (u8 j = 0; j < arg_count; ++j) {
						context.arguments[j].value = parts[j + 1];
					}
				}

				context.listener = cmd->listener;

				cmd->func(context);

				if (context.arguments) {
					kfree(context.arguments);
				}
				string_free(context.command);
				string_free(context.command_name);
				string_cleanup_split_darray(parts);
			}
			break;
		}
	}

	if (!command_found) {
		KERROR("The command '%s' does not exist.", string_trim(parts[0]));
		has_error = true;
	}

console_command_execute_cleanup:
	string_cleanup_split_darray(parts);
	darray_destroy(parts);

	return !has_error;
}

b8 console_object_register (const char *object_name, void *object, console_object_type type) {
	if (!object || !object_name) {
		KERROR("console_object_register requires a valid pointer to object and object_name");
		return false;
	}

	// Make sure it doesn't already exist.
	u32 object_count = darray_length(state_ptr->registered_objects);
	for (u32 i = 0; i < object_count; ++i) {
		if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
			KERROR("Console object already registered: '%s'.", object_name);
			return false;
		}
	}

	console_object new_object = {};
	new_object.name = string_duplicate(object_name);
	new_object.type = type;
	new_object.block = object;
	new_object.properties = 0;
	darray_push(state_ptr->registered_objects, &new_object);

	return true;
}

b8 console_object_unregister (const char *object_name) {
	if (!object_name) {
		KERROR("console_object_register requires a valid pointer object_name");
		return false;
	}

	// Make sure it exists.
	u32 object_count = darray_length(state_ptr->registered_objects);
	for (u32 i = 0; i < object_count; ++i) {
		if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
			// Object found, remove it.
			console_object popped_object;
			darray_pop_at(state_ptr->registered_objects, i, &popped_object);
			return true;
		}
	}

	return false;
}

b8 console_object_add_property (const char *object_name, const char *property_name, void *property, console_object_type type) {
	if (!property || !object_name || !property_name) {
		KERROR("console_object_add_property requires a valid pointer to property, property_name and object_name");
		return false;
	}

	// Make sure the object exists first.
	u32 object_count = darray_length(state_ptr->registered_objects);
	for (u32 i = 0; i < object_count; ++i) {
		if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
			console_object *obj = &state_ptr->registered_objects[i];
			// Found the object, now make sure a property with that name does not exist.
			if (obj->properties) {
				u32 property_count = darray_length(obj->properties);
				for (u32 j = 0; j < property_count; ++j) {
					if (strings_equali(obj->properties[j].name, property_name)) {
						KERROR("Object '%s' already has a property named '%s'.", object_name, property_name);
						return false;
					}
				}
			} else {
				obj->properties = darray_create(console_object);
			}

			// Create the new property, which is just another object.
			console_object new_object = {};
			new_object.name = string_duplicate(property_name);
			new_object.type = type;
			new_object.block = property;
			new_object.properties = 0;
			darray_push(obj->properties, &new_object);

			return true;
		}
	}

	KERROR("Console object not found: '%s'.", object_name);
	return false;
}

static void console_object_destroy (console_object *obj) {
	string_free((char *)obj->name);
	obj->block = 0;
	if (obj->properties) {
		u32 len = darray_length(obj->properties);
		for (u32 i = 0; i < len; ++i) {
			console_object_destroy(&obj->properties[i]);
		}
		darray_destroy(obj->properties);
		obj->properties = 0;
	}
}

b8 console_object_remove_property (const char *object_name, const char *property_name) {
	if (!object_name || !property_name) {
		KERROR("console_object_remove_property requires a valid pointer to property, property_name and object_name");
		return false;
	}

	// Make sure the object exists first.
	u32 object_count = darray_length(state_ptr->registered_objects);
	for (u32 i = 0; i < object_count; ++i) {
		if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
			console_object *obj = &state_ptr->registered_objects[i];
			// Found the object, now make sure a property with that name does not exist.
			if (obj->properties) {
				u32 property_count = darray_length(obj->properties);
				for (u32 j = 0; j < property_count; ++j) {
					if (strings_equali(obj->properties[j].name, property_name)) {
						console_object popped_property;
						darray_pop_at(obj->properties, j, &popped_property);
						console_object_destroy(&popped_property);
						return true;
					}
				}
			}

			KERROR("Property '%s' not found on console object '%s'.", object_name, property_name);
			return false;
		}
	}

	KERROR("Console object not found: '%s'.", object_name);
	return false;
}

// Custom console execution parsing/handling.

typedef enum ctoken_type {
	CTOKEN_TYPE_UNKNOWN,
	CTOKEN_TYPE_WHITESPACE,
	CTOKEN_TYPE_COMMENT,
	CTOKEN_TYPE_BLOCK_COMMENT_BEGIN,
	CTOKEN_TYPE_BLOCK_COMMENT_END,
	CTOKEN_TYPE_IDENTIFIER,
	CTOKEN_TYPE_OPERATOR_EQUAL,
	CTOKEN_TYPE_OPERATOR_MINUS,
	CTOKEN_TYPE_OPERATOR_PLUS,
	CTOKEN_TYPE_OPERATOR_SLASH,
	CTOKEN_TYPE_OPERATOR_ASTERISK,
	CTOKEN_TYPE_OPERATOR_DOT,
	CTOKEN_TYPE_STRING_LITERAL,
	CTOKEN_TYPE_NUMERIC_LITERAL,
	CTOKEN_TYPE_BOOLEAN,
	CTOKEN_TYPE_CURLY_BRACE_OPEN,
	CTOKEN_TYPE_CURLY_BRACE_CLOSE,
	CTOKEN_TYPE_BRACKET_OPEN,
	CTOKEN_TYPE_BRACKET_CLOSE,
	CTOKEN_TYPE_PAREN_OPEN,
	CTOKEN_TYPE_PAREN_CLOSE,
	CTOKEN_TYPE_NEWLINE,
	CTOKEN_TYPE_STATEMENT_TERMINATOR,
	CTOKEN_TYPE_EOF
} ctoken_type;

typedef enum cvar_type {
	CVAR_TYPE_UNKNOWN,
	CVAR_TYPE_INT,
	CVAR_TYPE_FLOAT,
	CVAR_TYPE_STRING,
	CVAR_TYPE_BOOLEAN,
	// A console object
	CVAR_TYPE_OBJECT,
	// A container to hold other cvars
	CVAR_TYPE_ARRAY,
	// A function. params and return type are contained in it's definition.
	CVAR_TYPE_FUNCTION
} cvar_type;

typedef struct ctoken {
	ctoken_type type;
	u32 start;
	u32 end;
	// The line number (0-based)
	u32 line_num;
	// Position within the line.
	u32 col_num;
#if KOHI_DEBUG
	const char *content;
#endif
} ctoken;

struct cproperty;
struct cfunction;

typedef struct cobject {
	const char *name;
	kstring_id name_id;

	struct cproperty *properties;
	struct cfunction *functions;
} cobject;

typedef struct console_parser {
	const char *content;
	u32 position;
	u32 current_line;
	u32 current_col;

	// darray
	ctoken *tokens;
} console_parser;

typedef union cproperty_value {
	b8 b;
	i64 i;
	f32 f;
	const char *s;
	cobject o;
} cproperty_value;

typedef struct cproperty {
	cvar_type type;
	kstring_id name;
#if KOHI_DEBUG
	const char *name_str;
#endif
} cproperty;

typedef enum ctokenize_mode {
	CTOKENIZE_MODE_UNKNOWN,
	CTOKENIZE_MODE_DEFINING_IDENTIFIER,
	CTOKENIZE_MODE_WHITESPACE,
	CTOKENIZE_MODE_STRING_LITERAL,
	CTOKENIZE_MODE_NUMERIC_LITERAL,
	CTOKENIZE_MODE_BOOLEAN,
	CTOKENIZE_MODE_OPERATOR,
} ctokenize_mode;

// Resets both the current token type and the tokenize mode to unknown.
static void reset_current_token_and_mode (ctoken *current_token, ctokenize_mode *mode) {
	current_token->type = CTOKEN_TYPE_UNKNOWN;
	current_token->start = 0;
	current_token->end = 0;
#if KOHI_DEBUG
	current_token->content = 0;
#endif

	*mode = CTOKENIZE_MODE_UNKNOWN;
}

#if KOHI_DEBUG
static void _populate_token_content (ctoken *t, const char *source) {
	KASSERT_MSG(t->start <= t->end, "Token start comes after token end, ya dingus!");
	char buffer[512] = {0};
	KASSERT_MSG((t->end - t->start) < 512, "token won't fit in buffer.");
	string_mid(buffer, source, t->start, t->end - t->start);
	t->content = string_duplicate(buffer);
}
#	define POPULATE_TOKEN_CONTENT(t, source) _populate_token_content(t, source)
#else
// No-op
#	define POPULATE_TOKEN_CONTENT(t, source)
#endif

// Pushes the current token, if not of unknown type.
static void push_token (ctoken *t, console_parser *parser) {
	if (t->type != CTOKEN_TYPE_UNKNOWN && (t->end - t->start > 0)) {
		POPULATE_TOKEN_CONTENT(t, parser->content);
		darray_push(parser->tokens, t);
	}
}

static void report_warning (const console_parser *parser, const char *message) {
	KWARN("%s at position %u. (line=%u, char=%u).", parser->position, parser->current_line, parser->current_col);
}

static void report_error (const console_parser *parser, const char *message) {
	KERROR("%s at position %u. (line=%u, char=%u).", parser->position, parser->current_line, parser->current_col);
}

static b8 tokenize_exec (console_parser *parser, const char *source) {
	b8 success = false;
	parser->content = string_duplicate(source);

	u32 char_length = string_length(source);

	ctokenize_mode mode = CTOKENIZE_MODE_DEFINING_IDENTIFIER;
	ctoken current_token = {0};

	i32 prev_codepoint = 0;
	i32 prev_codepoint2 = 0;

	b8 eof_reached = false;

	// Take the length in chars and get the correct codepoint from it.
	i32 codepoint = 0;
	for (parser->position = 0; parser->position < char_length; prev_codepoint2 = prev_codepoint, prev_codepoint = codepoint) {
		if (eof_reached) {
			break;
		}

		codepoint = source[parser->position];
		// How many bytes to advance.
		u8 advance = 1;
		// NOTE: UTF-8 codepoint handling.
		if (!bytes_to_codepoint(source, parser->position, &codepoint, &advance)) {
			KWARN("Invalid UTF-8 found in string, using unknown codepoint of -1");
			codepoint = -1;
		}

		if (mode == CTOKENIZE_MODE_STRING_LITERAL) {
			// Handle string literal parsing.

			// If a newline is encountered, line splits within strings are not supported. Error.
			if (codepoint == '\n' || codepoint == '\r') {
				report_error(parser, "Unexpected newline in string");
				goto tokenize_cleanup;
			}

			// TODO: May need to handle other escape sequences read in here, like \0 etc.

			// End the string if only if the previous codepoint was not a backslash OR the codepoint
			// previous codepoint was a backslash AND the one before that was also a backslash. I.e.
			// it needs to be confirmed that the backslash is not already escaped and that the quote is
			// also not escaped.
			if (codepoint == '"' && (prev_codepoint != '\\' || prev_codepoint2 == '\\')) {
				// Terminate the string, push the token onto the array, and revert modes.
				push_token(&current_token, parser);
				reset_current_token_and_mode(&current_token, &mode);
			} else {
				// Handle other characters as part of the string.
				current_token.end += advance;
			}

			// At this point, this codepoint has been handles so continue early.
			parser->position += advance;
			parser->current_col += advance;
			continue;
		}

		// Not part of a string, identifier, numeric, etc., so try to figure out what to do next.
		switch (codepoint) {
		case '\n': {
			push_token(&current_token, parser);

			// Just create a new token and insert it.
			ctoken newline_token = {CTOKEN_TYPE_NEWLINE, parser->position, parser->position + advance};

			parser->current_line++;
			parser->current_col = 0;

			push_token(&newline_token, parser);

			reset_current_token_and_mode(&current_token, &mode);

			// Now advance c without advancing the column.
			parser->position += advance;
			continue;
		} break;
		case '\t':
		case '\r':
		case ' ': {
			if (mode == CTOKENIZE_MODE_WHITESPACE) {
				// Tack it onto the whitespace.
				current_token.end++;
			} else {
				// Before switching to whitespace mode, push the current token.
				push_token(&current_token, parser);
				mode = CTOKENIZE_MODE_WHITESPACE;
				current_token.type = CTOKEN_TYPE_WHITESPACE;
				current_token.start = parser->position;
				current_token.end = parser->position + advance;
			}
		} break;
		case ';': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken statement_term_token = {CTOKEN_TYPE_STATEMENT_TERMINATOR, parser->position, parser->position + advance};
			push_token(&statement_term_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '{': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken open_brace_token = {CTOKEN_TYPE_CURLY_BRACE_OPEN, parser->position, parser->position + advance};
			push_token(&open_brace_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '}': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken close_brace_token = {CTOKEN_TYPE_CURLY_BRACE_CLOSE, parser->position, parser->position + advance};
			push_token(&close_brace_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '[': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken open_bracket_token = {CTOKEN_TYPE_BRACKET_OPEN, parser->position, parser->position + advance};
			push_token(&open_bracket_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case ']': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken close_bracket_token = {CTOKEN_TYPE_BRACKET_CLOSE, parser->position, parser->position + advance};
			push_token(&close_bracket_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '(': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken open_bracket_token = {CTOKEN_TYPE_PAREN_OPEN, parser->position, parser->position + advance};
			push_token(&open_bracket_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case ')': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken close_bracket_token = {CTOKEN_TYPE_PAREN_CLOSE, parser->position, parser->position + advance};
			push_token(&close_bracket_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '"': {
			push_token(&current_token, parser);

			reset_current_token_and_mode(&current_token, &mode);

			// Change to string parsing mode.
			mode = CTOKENIZE_MODE_STRING_LITERAL;
			current_token.type = CTOKEN_TYPE_STRING_LITERAL;
			current_token.start = parser->position + advance;
			current_token.end = parser->position + advance;
		} break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9': {
			if (mode == CTOKENIZE_MODE_NUMERIC_LITERAL) {
				current_token.end++;
			} else {
				// Push the existing token.
				push_token(&current_token, parser);

				// Switch to numeric parsing mode.
				mode = CTOKENIZE_MODE_NUMERIC_LITERAL;
				current_token.type = CTOKEN_TYPE_NUMERIC_LITERAL;
				current_token.start = parser->position;
				current_token.end = parser->position + advance;
			}
		} break;
		case '-': {
			// NOTE: Always treat the minus as a minus operator regardless of how it is used (except in
			// the string case above, which is already covered). It's then up to the grammar rules as to
			// whether this then gets used to negate a numeric literal or if it is used for subtraction, etc.

			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken minus_token = {CTOKEN_TYPE_OPERATOR_MINUS, parser->position, parser->position + advance};
			push_token(&minus_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '+': {
			// NOTE: Always treat the plus as a plus operator regardless of how it is used (except in
			// the string case above, which is already covered). It's then up to the grammar rules as to
			// whether this then gets used to ensure positivity of a numeric literal or if it is used for addition, etc.

			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken plus_token = {CTOKEN_TYPE_OPERATOR_PLUS, parser->position, parser->position + advance};
			push_token(&plus_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '/': {
			push_token(&current_token, parser);
			reset_current_token_and_mode(&current_token, &mode);

			// Look ahead and see if another slash follows. If so, the rest of the
			// line is a comment. Skip forward until a newline is found.
			if (source[parser->position + 1] == '/') {
				u32 start = parser->position;
				i32 cm = parser->position + 2;
				char ch = source[cm];
				while (ch != '\n' && ch != '\0') {
					cm++;
					ch = source[cm];
				}
				if (cm > 0) {
					// Skip to one char before the newline so the newline gets processed.
					// This is done because the comment shouldn't be tokenized, but should
					// instead be ignored.
					parser->current_col += (u32)(cm - parser->position);
					parser->position = cm;
				}
				// Push a comment token, which represents the entire area of text from the // to the end.
				ctoken comment_token = {CTOKEN_TYPE_COMMENT, start, parser->position};
				push_token(&comment_token, parser);
				continue;
			} else if (source[parser->position + 1] == '*') {
				// Start of a block comment.
				// Push a start block comment token, which represents the entire area of text from the // to the end.
				ctoken start_block_comment_token = {CTOKEN_TYPE_BLOCK_COMMENT_BEGIN, parser->position, parser->position + 1};
				push_token(&start_block_comment_token, parser);

				parser->position += 2;
				u32 start = parser->position;
				u32 end = parser->position;
				char ch = source[parser->position];
				char ch2 = source[parser->position + 1];
				while (true) {
					ch = source[parser->position];
					ch2 = source[parser->position + 1];

					if (ch == '\0') {
						break;
					} else if (ch == '\n') {
						// Make sure to handle newlines
						parser->current_col = 0;
						parser->current_line++;
						continue;
					} else if (ch == '*' && ch2 == '/') {
						// End of the block.
						end = parser->position - 1;

						// Push a comment token, which represents the entire area of text between the /* and */
						ctoken comment_token = {CTOKEN_TYPE_COMMENT, start, end};
						push_token(&comment_token, parser);

						// Push a block end token as well.
						ctoken end_block_comment_token = {CTOKEN_TYPE_BLOCK_COMMENT_END, parser->position, parser->position + 1};
						push_token(&end_block_comment_token, parser);
						parser->position += 2;
						parser->current_col += 2;
						break;
					}

					parser->position++;
					parser->current_col++;
				}
				continue;
			} else {
				// Otherwise it should be treated as a slash operator.
				// Create and push a new token for this.
				ctoken slash_token = {CTOKEN_TYPE_OPERATOR_SLASH, parser->position, parser->position + advance};
				push_token(&slash_token, parser);
			}

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '*': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken asterisk_token = {CTOKEN_TYPE_OPERATOR_ASTERISK, parser->position, parser->position + advance};
			push_token(&asterisk_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '=': {
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken equal_token = {CTOKEN_TYPE_OPERATOR_EQUAL, parser->position, parser->position + advance};
			push_token(&equal_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '.': {
			// NOTE: Always treat this as a dot token, regardless of use. It's up to the grammar
			// rules in the parser as to whether or not it's to be used as part of a numeric literal
			// or something else.

			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken dot_token = {CTOKEN_TYPE_OPERATOR_DOT, parser->position, parser->position + advance};
			push_token(&dot_token, parser);

			reset_current_token_and_mode(&current_token, &mode);
		} break;
		case '\0': {
			// Reached the end of the file.
			push_token(&current_token, parser);

			// Create and push a new token for this.
			ctoken eof_token = {CTOKEN_TYPE_EOF, parser->position, parser->position + advance};
			push_token(&eof_token, parser);

			reset_current_token_and_mode(&current_token, &mode);

			eof_reached = true;
		} break;

		default: {
			// Identifiers may be made up of upper/lowercase a-z, underscores and numbers (although
			// a number cannot be the first character of an identifier). Note that the number cases
			// are handled above as numeric literals, and can/will be combined into identifiers
			// if there are identifiers without whitespace next to numerics.
			if ((codepoint >= 'A' && codepoint <= 'z') || codepoint == '_') {
				if (mode == CTOKENIZE_MODE_DEFINING_IDENTIFIER) {
					// Start a new identifier token.
					if (current_token.type == CTOKEN_TYPE_UNKNOWN) {
						current_token.type = CTOKEN_TYPE_IDENTIFIER;
						current_token.start = parser->position;
						current_token.end = parser->position;
					}
					// Tack onto the existing identifier.
					current_token.end += advance;
				} else {
					// Check first to see if it's possibly a boolean definition.
					const char *str = source + parser->position;
					u8 bool_advance = 0;
					if (strings_nequali(str, "true", 4)) {
						bool_advance = 4;
					} else if (strings_nequali(str, "false", 5)) {
						bool_advance = 5;
					}

					if (bool_advance) {
						push_token(&current_token, parser);

						// Create and push boolean token.
						ctoken bool_token = {CTOKEN_TYPE_BOOLEAN, parser->position, parser->position + bool_advance};
						push_token(&bool_token, parser);

						reset_current_token_and_mode(&current_token, &mode);

						// Move forward by the size of the token.
						advance = bool_advance;
					} else {
						// Treat as the start of an identifier definition.
						// Push the existing token.
						push_token(&current_token, parser);

						// Switch to identifier parsing mode.
						mode = CTOKENIZE_MODE_DEFINING_IDENTIFIER;
						current_token.type = CTOKEN_TYPE_IDENTIFIER;
						current_token.start = parser->position;
						current_token.end = parser->position + advance;
					}
				}
			} else {
				// If any other character is come across here that isn't part of a string, it's unknown
				// what should happen here. So, throw an error regarding this and boot if this is the
				// case.
				report_error(parser, "Tokenization failed: Unexpected character '%c'");
				// Clear the tokens array, as there is nothing that can be done with them in this case.
				darray_clear(parser->tokens);
				goto tokenize_cleanup;
			}

		} break;
		}

		// Now advance c
		parser->position += advance;
		// Advance the column as well.
		parser->current_col += advance;
	}

	push_token(&current_token, parser);
	// Create and push a new token for this.
	ctoken eof_token = {CTOKEN_TYPE_EOF, char_length, char_length + 1};
	push_token(&eof_token, parser);

	success = true;
tokenize_cleanup:
	if (parser->content) {
		string_free(parser->content);
	}

	return success;
}

static b8 parse_exec (console_state *state, console_parser *parser) {
	// TODO: Parse into a collection of statements. Functions, recursions, etc
	// will have to come later. In other words, an AST.
	return true;
}

static b8 on_exec (console_state *state, const char *exec_text) {
	console_parser parser = {
		.content = 0,
		.position = 0,
		.tokens = darray_create(ctoken)};

	KTRACE("Executing code: '%s'...", exec_text);

	// Tokenize the text.
	if (!tokenize_exec(&parser, exec_text)) {
		KERROR("Failed to tokenize exec source.");
		return false;
	}

	// TODO: parse it. This should also resolve any named references along the way.

	// TODO: execute it.

	return true;
}
