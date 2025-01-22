#include "console.h"

#include "containers/darray.h"
#include "containers/stack.h"
#include "debug/kassert.h"
#include "defines.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

typedef struct console_consumer {
    PFN_console_consumer_write callback;
    void* instance;
} console_consumer;

typedef struct console_command {
    const char* name;
    u8 arg_count;
    PFN_console_command func;
} console_command;

typedef struct console_object {
    const char* name;
    console_object_type type;
    void* block;
    // darray
    struct console_object* properties;
} console_object;

typedef struct console_state {
    u8 consumer_count;
    console_consumer* consumers;

    // darray of registered commands.
    console_command* registered_commands;

    // darray of registered console objects.
    console_object* registered_objects;
} console_state;

const u32 MAX_CONSUMER_COUNT = 10;

static console_state* state_ptr;

b8 console_initialize(u64* memory_requirement, struct console_state* memory, void* config) {
    *memory_requirement = sizeof(console_state) + (sizeof(console_consumer) * MAX_CONSUMER_COUNT);

    if (!memory) {
        return true;
    }

    kzero_memory(memory, *memory_requirement);
    state_ptr = memory;
    state_ptr->consumers = (console_consumer*)((u64)memory + sizeof(console_state));

    state_ptr->registered_commands = darray_create(console_command);
    state_ptr->registered_objects = darray_create(console_object);

    // Tell the logger about the console.
    logger_console_write_hook_set(console_write);

    return true;
}

void console_shutdown(struct console_state* state) {
    if (state_ptr) {
        darray_destroy(state_ptr->registered_commands);
        darray_destroy(state_ptr->registered_objects);

        kzero_memory(state, sizeof(console_state) + (sizeof(console_consumer) * MAX_CONSUMER_COUNT));
    }

    state_ptr = 0;
}

void console_consumer_register(void* inst, PFN_console_consumer_write callback, u8* out_consumer_id) {
    if (state_ptr) {
        KASSERT_MSG(state_ptr->consumer_count + 1 < MAX_CONSUMER_COUNT, "Max console consumers reached.");

        console_consumer* consumer = &state_ptr->consumers[state_ptr->consumer_count];
        consumer->instance = inst;
        consumer->callback = callback;
        *out_consumer_id = state_ptr->consumer_count;
        state_ptr->consumer_count++;
    }
}

void console_consumer_update(u8 consumer_id, void* inst, PFN_console_consumer_write callback) {
    if (state_ptr) {
        KASSERT_MSG(consumer_id < state_ptr->consumer_count, "Consumer id is invalid.");

        console_consumer* consumer = &state_ptr->consumers[consumer_id];
        consumer->instance = inst;
        consumer->callback = callback;
    }
}

void console_write(log_level level, const char* message) {
    if (state_ptr) {
        // Notify each consumer that a line has been added.
        for (u8 i = 0; i < state_ptr->consumer_count; ++i) {
            console_consumer* consumer = &state_ptr->consumers[i];
            if (consumer->callback) {
                consumer->callback(consumer->instance, level, message);
            }
        }
    }
}

b8 console_command_register(const char* command, u8 arg_count, PFN_console_command func) {
    KASSERT_MSG(state_ptr && command, "console_register_command requires state and valid command");

    // Make sure it doesn't already exist.
    u32 command_count = darray_length(state_ptr->registered_commands);
    for (u32 i = 0; i < command_count; ++i) {
        if (strings_equali(state_ptr->registered_commands[i].name, command)) {
            KERROR("Command already registered: %s", command);
            return false;
        }
    }

    console_command new_command = {};
    new_command.arg_count = arg_count;
    new_command.func = func;
    new_command.name = string_duplicate(command);
    darray_push(state_ptr->registered_commands, new_command);

    return true;
}

b8 console_command_unregister(const char* command) {
    KASSERT_MSG(state_ptr && command, "console_update_command requires state and valid command");

    // Make sure it doesn't already exist.
    u32 command_count = darray_length(state_ptr->registered_commands);
    for (u32 i = 0; i < command_count; ++i) {
        if (strings_equali(state_ptr->registered_commands[i].name, command)) {
            // Command found, remove it.
            console_command popped_command;
            darray_pop_at(state_ptr->registered_commands, i, &popped_command);
            return true;
        }
    }

    return false;
}

static console_object* console_object_get(console_object* parent, const char* name) {
    if (parent) {
        u32 property_count = darray_length(parent->properties);
        for (u32 i = 0; i < property_count; ++i) {
            console_object* obj = &parent->properties[i];
            if (strings_equali(obj->name, name)) {
                return obj;
            }
        }
    } else {
        u32 registered_object_len = darray_length(state_ptr->registered_objects);
        for (u32 i = 0; i < registered_object_len; ++i) {
            console_object* obj = &state_ptr->registered_objects[i];
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

static void console_object_print(u8 indent, console_object* obj) {
    char indent_buffer[513] = {0};
    u16 idx = 0;
    for (; idx < (indent * 2); idx += 2) {
        indent_buffer[idx + 0] = ' ';
        indent_buffer[idx + 1] = ' ';
    }
    indent_buffer[idx] = 0;

    switch (obj->type) {
    case CONSOLE_OBJECT_TYPE_INT32:
        KINFO("%s%i", indent_buffer, *((i32*)obj->block));
        break;
    case CONSOLE_OBJECT_TYPE_UINT32:
        KINFO("%s%u", indent_buffer, *((u32*)obj->block));
        break;
    case CONSOLE_OBJECT_TYPE_F32:
        KINFO("%s%f", indent_buffer, *((f32*)obj->block));
        break;
    case CONSOLE_OBJECT_TYPE_BOOL: {
        b8 val = *((b8*)obj->block);
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
/*
typedef enum console_token_type {
    CONSOLE_TOKEN_TYPE_UNKNOWN,
    CONSOLE_TOKEN_TYPE_EOF,
    CONSOLE_TOKEN_TYPE_WHITESPACE,
    CONSOLE_TOKEN_TYPE_NEWLINE,
    CONSOLE_TOKEN_TYPE_OPEN_PAREN,
    CONSOLE_TOKEN_TYPE_CLOSE_PAREN,
    CONSOLE_TOKEN_TYPE_IDENTIFIER,
    CONSOLE_TOKEN_TYPE_OPERATOR,
    CONSOLE_TOKEN_TYPE_STRING_LITERAL,
    CONSOLE_TOKEN_TYPE_NUMERIC_LITERAL,
    CONSOLE_TOKEN_TYPE_DOT,
    CONSOLE_TOKEN_TYPE_COMMA,
    CONSOLE_TOKEN_TYPE_END_STATEMENT
} console_token_type;

typedef enum console_operator_type {
    // Unrecognized operator.
    CONSOLE_OPERATOR_TYPE_UNKNOWN,
    // =
    CONSOLE_OPERATOR_TYPE_ASSIGN,
    // ==
    CONSOLE_OPERATOR_TYPE_EQUALITY,
    // !=
    CONSOLE_OPERATOR_TYPE_INEQUALITY,
    // !
    CONSOLE_OPERATOR_TYPE_NOT,
    // <
    CONSOLE_OPERATOR_TYPE_LESSTHAN,
    // <=
    CONSOLE_OPERATOR_TYPE_LESSTHAN_OR_EQUAL,
    // >
    CONSOLE_OPERATOR_TYPE_GREATERTHAN,
    // >=
    CONSOLE_OPERATOR_TYPE_GREATERTHAN_OR_EQUAL,
    // +
    CONSOLE_OPERATOR_TYPE_ADD,
    // +=
    CONSOLE_OPERATOR_TYPE_ADD_ASSIGN,
    // -
    CONSOLE_OPERATOR_TYPE_SUBTRACT,
    // -=
    CONSOLE_OPERATOR_TYPE_SUBTRACT_ASSIGN,
    // *
    CONSOLE_OPERATOR_TYPE_MULTIPLY,
    // *=
    CONSOLE_OPERATOR_TYPE_MULTIPLY_ASSIGN,
    // /
    CONSOLE_OPERATOR_TYPE_DIVIDE,
    // /=
    CONSOLE_OPERATOR_TYPE_DIVIDE_ASSIGN,
    // %
    CONSOLE_OPERATOR_TYPE_MODULUS,
    // %=
    CONSOLE_OPERATOR_TYPE_MODULUS_ASSIGN,
    // ++
    CONSOLE_OPERATOR_TYPE_INCREMENT,
    // --
    CONSOLE_OPERATOR_TYPE_DECREMENT,
} console_operator_type;

typedef struct console_token {
    console_token_type type;
    u32 start_index;
    u32 length;
} console_token;

static b8 console_operator_type_from_token(const console_token token, const char* source, console_operator_type* out_type) {
    if (!source || token.type != CONSOLE_TOKEN_TYPE_OPERATOR) {
        KERROR("console_operator_type_from_token requires a valid pointer to source and an operator type token");
        return false;
    }

    // NOTE: Ensure token length here so it doesn't have to be verified anywhere below.
    if (token.length < 1 || token.length > 2) {
        KERROR("operators are always either 1 or 2 characters.");
        return false;
    }

    char c = source[token.start_index];
    char next = source[token.start_index + 1];
    *out_type = CONSOLE_OPERATOR_TYPE_UNKNOWN;
    u8 error_offset = 0;
    switch (c) {
        case '=':
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_ASSIGN;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_EQUALITY;
            } else {
                error_offset = 1;
            }
            break;
        case '!': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_NOT;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_INEQUALITY;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '<': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_LESSTHAN;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_LESSTHAN_OR_EQUAL;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '>': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_GREATERTHAN;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_GREATERTHAN_OR_EQUAL;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '+': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_ADD;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_ADD_ASSIGN;
            } else if (next == '+') {
                *out_type = CONSOLE_OPERATOR_TYPE_INCREMENT;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '-': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_SUBTRACT;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_SUBTRACT_ASSIGN;
            } else if (next == '-') {
                *out_type = CONSOLE_OPERATOR_TYPE_DECREMENT;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '*': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_MULTIPLY;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_MULTIPLY_ASSIGN;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '/': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_DIVIDE;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_DIVIDE_ASSIGN;
            } else {
                error_offset = 1;
            }
            break;
        }
        case '%': {
            if (token.length == 1) {
                *out_type = CONSOLE_OPERATOR_TYPE_MODULUS;
            } else if (next == '=') {
                *out_type = CONSOLE_OPERATOR_TYPE_MODULUS_ASSIGN;
            } else {
                error_offset = 1;
            }
            break;
        }
        default:
            error_offset = 0;
            break;
    }

    b8 result = (*out_type != CONSOLE_OPERATOR_TYPE_UNKNOWN);
    if (!result) {
        KERROR("Unexpected character '%c' at position %u.", source[token.start_index + error_offset], token.start_index + error_offset);
    }
    return result;
}

static b8 tokenize_source(const char* source, stack* out_tokens) {
    if (!source || !out_tokens) {
        KERROR("tokenize_source requires valid pointers to source and out_tokens.");
        return false;
    }

    u32 char_length = string_length(source);

    console_token current_token = {0};
    for (u32 i = 0; i < char_length; ++i) {
        i32 codepoint;
        u8 advance;
        if (!bytes_to_codepoint(source, i, &codepoint, &advance)) {
            // If this fails, don't attempt parsing any further since the characters aren't recognized,
            // it might lead to some sort of undefined behaviour.
            KERROR("Error processing expression - unable to decode codepoint at position: %u", i);
            return false;
        }

        switch (codepoint) {
            case ' ':
            case '\t': {
                if (current_token.type == CONSOLE_TOKEN_TYPE_WHITESPACE) {
                    current_token.length++;
                } else {
                    // If unknown, start a whitespace token.
                    if (current_token.type == CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        current_token.length = 1;
                        current_token.start_index = i;
                        current_token.type = CONSOLE_TOKEN_TYPE_WHITESPACE;
                    } else if (current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                        // For strings, just extend the length.
                        current_token.length++;
                    } else {
                        // Push the current token if not unknown type.
                        if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                            stack_push(out_tokens, &current_token);
                        }

                        // Then start a new whitespace token.
                        current_token.length = 1;
                        current_token.start_index = i;
                        current_token.type = CONSOLE_TOKEN_TYPE_WHITESPACE;
                    }
                }
            } break;
            case '{': {
                // Push the current token if not unknown type.
                if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                    stack_push(out_tokens, &current_token);
                }

                // Push a new open paren token.
                current_token.length = 1;
                current_token.start_index = i;
                current_token.type = CONSOLE_TOKEN_TYPE_OPEN_PAREN;
                stack_push(out_tokens, &current_token);

                // Set back to unknown
                current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                current_token.length = 0;
                current_token.start_index = 0;
            } break;
            case '}': {
                // Push the current token if not unknown type.
                if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                    stack_push(out_tokens, &current_token);
                }

                // Push a new close paren token.
                current_token.length = 1;
                current_token.start_index = i;
                current_token.type = CONSOLE_TOKEN_TYPE_CLOSE_PAREN;
                stack_push(out_tokens, &current_token);

                // Set back to unknown
                current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                current_token.length = 0;
                current_token.start_index = 0;
            } break;
            case '\n': {
                // Push the current token if not unknown type.
                if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                    stack_push(out_tokens, &current_token);
                }

                // Push a new newline token.
                current_token.length = 1;
                current_token.start_index = i;
                current_token.type = CONSOLE_TOKEN_TYPE_NEWLINE;
                stack_push(out_tokens, &current_token);

                // Set back to unknown
                current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                current_token.length = 0;
                current_token.start_index = 0;
            } break;
            case '"': {
                // TODO: handle escape sequences like '\"'.
                //
                // If not in a string, push the old token and start a new string one.
                if (current_token.type != CONSOLE_TOKEN_TYPE_STRING_LITERAL && current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                    stack_push(out_tokens, &current_token);

                    current_token.length = 1;
                    current_token.start_index = i;
                    current_token.type = CONSOLE_TOKEN_TYPE_STRING_LITERAL;
                } else if (current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                    // If currently in a string, close it and push the token.
                    current_token.length++;
                    stack_push(out_tokens, &current_token);

                    // Set back to unknown
                    current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                    current_token.length = 0;
                    current_token.start_index = 0;
                }
            } break;
            case '=':
            case '!':
            case '<':
            case '>':
            case '+':
            case '-':
            case '*':
            case '/':
            case '%': {
                // Operators.

                if (current_token.type != CONSOLE_TOKEN_TYPE_OPERATOR) {
                    // Push the current token if not unknown type.
                    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        stack_push(out_tokens, &current_token);
                    }

                    // Start a new operator token.
                    current_token.start_index = i;
                    current_token.length = 1;
                    current_token.type = CONSOLE_TOKEN_TYPE_OPERATOR;
                    // The operator will be closed and added on the next iteration if it is a different codepoint type.
                } else {
                    // Already within an operator token.
                    // Ensure the length isn't going to go over 2 - then it's invalid.
                    if (current_token.length >= 2) {
                        KERROR("Invalid character at position %u.", i);
                        return false;
                    }

                    // Only some of these are valid.
                    switch (codepoint) {
                        case '=': {
                            // Valid if the previous char was !,=,+,-,/,*, or %
                            char prev = source[current_token.start_index];
                            switch (prev) {
                                case '!':
                                case '=':
                                case '+':
                                case '-':
                                case '/':
                                case '*':
                                case '%':
                                    current_token.length++;
                                    break;
                                default:
                                    KERROR("Unexpected character at position %u", i);
                                    return false;
                            }
                        }
                        case '+':
                        case '-': {
                            // Only valid if the previous character matches.
                            char prev = source[current_token.start_index];
                            if (((char)codepoint) == prev) {
                                current_token.length++;
                            } else {
                                KERROR("Unexpected character at position %u", i);
                                return false;
                            }
                        } break;
                        case '!':
                        case '%':
                        case '/':
                        case '*':
                        case '<':
                        case '>': {
                            // invalid as a second character.
                            KERROR("Unexpected character at position %u", i);
                            return false;
                        }
                    }
                }
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
                if (current_token.type != CONSOLE_TOKEN_TYPE_NUMERIC_LITERAL) {
                    // Push the current token if not unknown type.
                    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        stack_push(out_tokens, &current_token);
                    }

                    // Start a new numeric literal token.
                    current_token.length = 1;
                    current_token.start_index = i;
                    current_token.type = CONSOLE_TOKEN_TYPE_NUMERIC_LITERAL;

                } else {
                    // Add onto the current numeric literal. Safe to move one char here.
                    current_token.length++;
                }
            } break;
            case '.': {
                if (current_token.type == CONSOLE_TOKEN_TYPE_NUMERIC_LITERAL || current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                    // Add onto the current token. Save to move one char here.
                    current_token.length++;
                } else {
                    // Push the current token if not unknown type.
                    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        stack_push(out_tokens, &current_token);
                    }

                    // Create a new dot token and push it.
                    current_token.length = 1;
                    current_token.start_index = i;
                    current_token.type = CONSOLE_TOKEN_TYPE_DOT;
                    stack_push(out_tokens, &current_token);

                    // Set back to unknown
                    current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                    current_token.length = 0;
                    current_token.start_index = 0;
                }
            } break;
            case ',': {
                if (current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                    // Add onto the current token. Save to move one char here.
                    current_token.length++;
                } else {
                    // Push the current token if not unknown type.
                    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        stack_push(out_tokens, &current_token);
                    }

                    // Create a new comma token and push it.
                    current_token.length = 1;
                    current_token.start_index = i;
                    current_token.type = CONSOLE_TOKEN_TYPE_COMMA;
                    stack_push(out_tokens, &current_token);

                    // Set back to unknown
                    current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                    current_token.length = 0;
                    current_token.start_index = 0;
                }
            } break;
            case ';': {
                if (current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                    // Add onto the current token. Save to move one char here.
                    current_token.length++;
                } else {
                    // Push the current token if not unknown type.
                    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
                        stack_push(out_tokens, &current_token);
                    }

                    // Create a new statement-end token and push it.
                    current_token.length = 1;
                    current_token.start_index = i;
                    current_token.type = CONSOLE_TOKEN_TYPE_END_STATEMENT;
                    stack_push(out_tokens, &current_token);

                    // Set back to unknown
                    current_token.type = CONSOLE_TOKEN_TYPE_UNKNOWN;
                    current_token.length = 0;
                    current_token.start_index = 0;
                }
            } break;
            default: {
                // Any other character should be treated as part of a string or an identifier, depending on
                // what kind of token we are currently in.
                // NOTE: Use the advance here to increase length to handle utf8 identifiers/strings.
                if (current_token.type == CONSOLE_TOKEN_TYPE_STRING_LITERAL) {
                    current_token.length += advance;
                } else if (current_token.type == CONSOLE_TOKEN_TYPE_IDENTIFIER) {
                    current_token.length += advance;
                } else if (current_token.type == CONSOLE_TOKEN_TYPE_UNKNOWN) {
                    // If unknown and we got here, treat it as an identifier.
                    current_token.type = CONSOLE_TOKEN_TYPE_IDENTIFIER;
                    current_token.start_index = i;
                    current_token.length = advance;
                } else {
                    // If in the middle of some other type of token, this won't be valid.
                    KERROR("Unexpected character at position %u", i);
                    return false;
                }
            } break;
        }
        // Bump the iterator forward by the advance.
        i += advance;
    }

    // If the final token is not unknown, also push it.
    if (current_token.type != CONSOLE_TOKEN_TYPE_UNKNOWN) {
        stack_push(out_tokens, &current_token);
    }

    // Finally, push a EOF token.
    current_token.type = CONSOLE_TOKEN_TYPE_EOF;
    current_token.start_index += current_token.length;
    current_token.length = 0;
    stack_push(out_tokens, &current_token);

    return true;
}

static b8 look_ahead_token(const stack* tokens, console_token* out_token, b8 skip_whitespace, b8 skip_newlines) {
    if (!tokens || !out_token) {
        return 0;
    }

    if (!stack_peek(tokens, out_token)) {
        return false;
    }

    if (skip_whitespace) {
        while (out_token->type == CONSOLE_TOKEN_TYPE_WHITESPACE) {
            if (!stack_peek(tokens, out_token)) {
                return false;
            }
        }
    }

    if (skip_newlines) {
        while (out_token->type == CONSOLE_TOKEN_TYPE_NEWLINE) {
            if (!stack_peek(tokens, out_token)) {
                return false;
            }
        }
    }

    return true;
}

typedef struct console_expression {
    console_object_type result_type;
    u32 token_count;
    console_token* tokens;
} console_expression;

typedef union console_numeric {
    i32 i;
    u32 u;
    f32 f;
} console_numeric;

static b8 numeric_token_parse(const char* source, const console_token* token, console_object_type* out_type, console_numeric* out_result) {
    if (!token || !out_type || !out_result) {
        return false;
    }

    b8 result = false;
    if (token->type == CONSOLE_TOKEN_TYPE_NUMERIC_LITERAL) {
        // Extract the value to a string to determine its type. When parsing numeric literals,
        // they are always parsed as either floats if there is a '.', otherwise an i32.
        char* value = kallocate(sizeof(char) * token->length + 1, MEMORY_TAG_STRING);
        if (string_index_of(value, '.')) {
            if (!string_to_f32(value, &out_result->f)) {
                KERROR("Failed to parse token as float at position %i.", token->start_index);
                result = false;
            }
            *out_type = CONSOLE_OBJECT_TYPE_F32;
            result = true;
        } else {
            if (!string_to_i32(value, &out_result->i)) {
                KERROR("Failed to parse token as integer at position %i.", token->start_index);
                result = false;
            }
            *out_type = CONSOLE_OBJECT_TYPE_INT32;
            result = true;
        }
        kfree(value, sizeof(char) * token->length + 1, MEMORY_TAG_STRING);

    } else if (token->type == CONSOLE_TOKEN_TYPE_IDENTIFIER) {
        // Extract the name and attempt to find the console object.
        char* name = kallocate(sizeof(char) * token->length + 1, MEMORY_TAG_STRING);
        string_mid(name, source, token->start_index, token->length);
        console_object* obj = console_object_get(0, name);
        if (!obj) {
            KERROR("Identifier '%s' is undefined.", name);
            result = false;
        } else {
            switch (obj->type) {
                case CONSOLE_OBJECT_TYPE_INT32:
                    out_result->i = console_object_to_i32(obj);
                    *out_type = obj->type;
                    result = true;
                    break;
                case CONSOLE_OBJECT_TYPE_UINT32:
                    out_result->u = console_object_to_u32(obj);
                    *out_type = obj->type;
                    result = true;
                    break;
                case CONSOLE_OBJECT_TYPE_F32:
                    out_result->f = console_object_to_f32(obj);
                    *out_type = obj->type;
                    result = true;
                    break;
                default:
                    // Wrong type of token.
                    KERROR("Expected numeric token (i32, u32 or f32) at position %i", token->start_index);
                    result = false;
                    break;
            }
        }
        kfree(name, sizeof(char) * token->length + 1, MEMORY_TAG_STRING);
    }

    return result;
}

static b8 evaluate_numerical_expression(const char* source, const console_token* a, const console_token* operator, const console_token* b, console_object_type* out_type, console_numeric* out_result) {
    console_numeric numeric_a, numeric_b;
    console_object_type type_a, type_b;
    console_operator_type operator_type;

    b8 is_unary = true;

    // Get operator type
    if (!console_operator_type_from_token(*operator, source, &operator_type)) {
        KERROR("Failed to evaluate numerical expression due to invalid operator token.");
        return false;
    }

    // Obtain token values.
    if (numeric_token_parse(source, a, &type_a, &numeric_a)) {
        KERROR("Failed to evaluate numerical expression. See logs for details.");
        return false;
    }

    // Is not unary if b exists.
    if (b) {
        if (numeric_token_parse(source, b, &type_b, &numeric_b)) {
            KERROR("Failed to evaluate numerical expression. See logs for details.");
            return false;
        }
        is_unary = false;
    }

    if (is_unary) {
        if (type_a == CONSOLE_OBJECT_TYPE_F32) {
            if (operator_type == CONSOLE_OPERATOR_TYPE_INCREMENT) {
                KWARN("Incrementing on float at position %i. May yield unexpected results.", a->start_index);
                out_result->f = numeric_a.f + 1;
            } else if (operator_type == CONSOLE_OPERATOR_TYPE_DECREMENT) {
                KWARN("Decrementing on float at position %i. May yield unexpected results.", a->start_index);
                out_result->f = numeric_a.f - 1;
            } else {
                KERROR("Unexpected unary operator at position %i", a->start_index);
                return false;
            }
        } else {
            // Treat as int.
            if (operator_type == CONSOLE_OPERATOR_TYPE_INCREMENT) {
                out_result->i = numeric_a.i + 1;
            } else if (operator_type == CONSOLE_OPERATOR_TYPE_DECREMENT) {
                out_result->i = numeric_a.i - 1;
            } else {
                KERROR("Unexpected unary operator at position %i", a->start_index);
                return false;
            }
        }
    } else {
        // Binary operator.

        // If either is a float, treat the entire thing like a float.
        if (type_a == CONSOLE_OBJECT_TYPE_F32 || type_b == CONSOLE_OBJECT_TYPE_F32) {
            *out_type = CONSOLE_OBJECT_TYPE_F32;
            // Ensure both sides are converted to floats if they aren't already.
            f32 f_a, f_b;
            if (type_a == CONSOLE_OBJECT_TYPE_F32) {
                f_a = numeric_a.f;
            } else if (type_a == CONSOLE_OBJECT_TYPE_INT32) {
                f_a = (f32)numeric_a.i;
            } else if (type_a == CONSOLE_OBJECT_TYPE_UINT32) {
                f_a = (f32)numeric_a.u;
            } else {
                KERROR("Unsupported object type at position %i.", a->start_index);
                return false;
            }
            if (type_b == CONSOLE_OBJECT_TYPE_F32) {
                f_b = numeric_b.f;
            } else if (type_b == CONSOLE_OBJECT_TYPE_INT32) {
                f_b = (f32)numeric_b.i;
            } else if (type_b == CONSOLE_OBJECT_TYPE_UINT32) {
                f_b = (f32)numeric_b.u;
            } else {
                KERROR("Unsupported object type at position %i.", b->start_index);
                return false;
            }

            switch (operator_type) {
                case CONSOLE_OPERATOR_TYPE_ADD:
                case CONSOLE_OPERATOR_TYPE_ADD_ASSIGN:
                    out_result->f = f_a + f_b;
                    break;
                case CONSOLE_OPERATOR_TYPE_SUBTRACT:
                case CONSOLE_OPERATOR_TYPE_SUBTRACT_ASSIGN:
                    out_result->f = f_a - f_b;
                    break;
                    // TODO: others
            }

        } else {
            // Otherwise, treat it as an i32.
            *out_type = CONSOLE_OBJECT_TYPE_INT32;
        }
    }

    // TODO: Pick this back up later.
    return false;
}

static b8 parse_expression(const char* source, const stack* tokens, console_object_type* out_result_type) {
    // Determine what kind of expression it is.
    // TODO: Pick this back up later.
    return false;
}

static b8 evaluate_expression(const char* source) {
    // TODO: Pick this back up later.
    return false;
}

static b8 console_source_tokens_evaluate(const char* source, stack* tokens, console_object_type* out_type, void* out_value) {
    b8 result = false;

    u32 token_count = darray_length(tokens);
    // TODO: Iterate the tokens to obtain a result.
    for (u32 i = 0; i < token_count; ++i) {
        console_token* token = &tokens[i];

        // Skip whitespace
        if (token->type == CONSOLE_TOKEN_TYPE_WHITESPACE) {
            continue;
        }
    }
    //

    return result;
}

static b8 console_expression_parse2(const char* expression_str, console_object_type* out_type, void* out_value) {
    b8 result = false;
    // Take a copy of the string and trim it.
    char* expression_copy = string_duplicate(expression_str);
    // NOTE: keep track of the original pointer address since string_trim
    // works by moving the pointer forward on the left side.
    char* expression_copy_original = expression_copy;
    string_trim(expression_copy);

    u32 char_length = string_length(expression_str);
    for (u32 i = 0; i < char_length; ++i) {
        i32 codepoint;
        u8 advance;
        if (!bytes_to_codepoint(expression_str, i, &codepoint, &advance)) {
            // If this fails, don't attempt parsing any further since the characters aren't recognized,
            // it might lead to some sort of undefined behaviour.
            KERROR("Error processing expression - unable to decode codepoint at position: %u", i);
            goto console_expression_parse_cleanup2;
        } else {
            // Bump the iterator forward by the advance.
            i += advance;
        }
    }

console_expression_parse_cleanup2:
    // Cleanup
    string_free(expression_copy_original);

    return result;
}*/

static b8 console_expression_parse(const char* expression, console_object_type* out_type, void* out_value) {
    // TODO: Disabling this for now. Need to rethink this implementation.
    /*
    b8 result = true;
    char* expression_copy = string_duplicate(expression);
    char* expression_copy_original = expression_copy;
    string_trim(expression_copy);

    // Operators supported are: =, ==, !=, /, *, +, -, %

    b8 operator_found = false;

    if (!operator_found) {
        i32 space_index = string_index_of(expression_copy, ' ');
        if (space_index != -1) {
            KERROR("Unexpected token at position %i", space_index);
            result = false;
            goto console_expression_parse_cleanup;
        }

        // Check for a dot operator which indicates a property of a struct.
        b8 identifier_found = false;
        i32 dot_index = string_index_of(expression_copy, '.');
        if (dot_index != -1) {
            // Parse each portion and figure out the struct/property hierarchy.
            char** parts = darray_create(char*);
            u32 split_count = string_split(expression_copy, '.', &parts, true, false);

            console_object* parent = console_object_get(0, parts[0]);
            for (u32 s = 1; s < split_count; ++s) {
                console_object* obj = console_object_get(parent, parts[s]);
                if (obj) {
                    parent = obj;
                }
            }
            if (parent) {
                console_object_print(0, parent);
                identifier_found = true;
                result = true;
            }
        } else {
            console_object* obj = console_object_get(0, expression_copy);
            if (obj) {
                console_object_print(0, obj);
                identifier_found = true;
                result = true;
            }
        }

        if (!identifier_found) {
            KERROR("Identifier is undefined: '%s'.", expression_copy);
            result = false;
            goto console_expression_parse_cleanup;
        }
    }

    // TODO:
    // Example expression:
    // the_thing = thing_2
    // or:
    // the_thing
    // Just entering a object name on its own would print the value of said object to the console.
    // Expressions can also just be parsed inline.

console_expression_parse_cleanup:
    // Cleanup
    string_free(expression_copy_original);

    return result;
    */
    return false;
}

b8 console_command_execute(const char* command) {
    if (!command) {
        return false;
    }
    b8 has_error = true;
    char** parts = darray_create(char*);
    // TODO: If strings are ever used as arguments, this will split improperly.
    u32 part_count = string_split(command, ' ', &parts, true, false);
    if (part_count < 1) {
        has_error = true;
        goto console_command_execute_cleanup;
    }
    // LEFTOFF: Need to refactor this to have 2 types of processing, a "process_command",
    // which takes command_name(arg1, arg2+arg3), etc. and passes each argument through
    // a "process_expression", which either retrieves the value of an object/property, or
    // retrieves the value as-is and passes it as an argument to the command.
    // Example command:
    // command(thing_1 + thing2, "arg")
    // Example expression:
    // the_thing = thing_2
    // or:
    // the_thing
    // Just entering a object name on its own would print the value of said object to the console.
    // Expressions can also just be parsed inline.
    // TODO: Add objects/properties to simple_scene during load.
    console_object_type parsed_type;
    void* block = kallocate(sizeof(void*), MEMORY_TAG_ARRAY);
    if (console_expression_parse(command, &parsed_type, block)) {
        // TODO: cast to appropriate type and use somehow..

        has_error = false;
        goto console_command_execute_cleanup;
    }

    // Write the line back out to the console for reference.
    char temp[512] = {0};
    string_format_unsafe(temp, "-->%s", command);
    console_write(LOG_LEVEL_INFO, temp);

    // Yep, strings are slow. But it's a console. It doesn't need to be lightning fast...
    b8 command_found = false;
    u32 command_count = darray_length(state_ptr->registered_commands);
    // Look through registered commands for a match.
    for (u32 i = 0; i < command_count; ++i) {
        console_command* cmd = &state_ptr->registered_commands[i];
        if (strings_equali(cmd->name, parts[0])) {
            command_found = true;
            u8 arg_count = part_count - 1;
            // Provided argument count must match expected number of arguments for the command.
            if (state_ptr->registered_commands[i].arg_count != arg_count) {
                KERROR("The console command '%s' requires %u arguments but %u were provided.", cmd->name, cmd->arg_count, arg_count);
                has_error = true;
            } else {
                // Execute it, passing along arguments if needed.
                console_command_context context = {};
                context.argument_count = cmd->arg_count;
                if (context.argument_count > 0) {
                    context.arguments = kallocate(sizeof(console_command_argument) * cmd->arg_count, MEMORY_TAG_ARRAY);
                    for (u8 j = 0; j < cmd->arg_count; ++j) {
                        context.arguments[j].value = parts[j + 1];
                    }
                }

                cmd->func(context);

                if (context.arguments) {
                    kfree(context.arguments, sizeof(console_command_argument) * cmd->arg_count, MEMORY_TAG_ARRAY);
                }
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

b8 console_object_register(const char* object_name, void* object, console_object_type type) {
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
    darray_push(state_ptr->registered_objects, new_object);

    return true;
}

b8 console_object_unregister(const char* object_name) {
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

b8 console_object_add_property(const char* object_name, const char* property_name, void* property, console_object_type type) {
    if (!property || !object_name || !property_name) {
        KERROR("console_object_add_property requires a valid pointer to property, property_name and object_name");
        return false;
    }

    // Make sure the object exists first.
    u32 object_count = darray_length(state_ptr->registered_objects);
    for (u32 i = 0; i < object_count; ++i) {
        if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
            console_object* obj = &state_ptr->registered_objects[i];
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
            darray_push(obj->properties, new_object);

            return true;
        }
    }

    KERROR("Console object not found: '%s'.", object_name);
    return false;
}

static void console_object_destroy(console_object* obj) {
    string_free((char*)obj->name);
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

b8 console_object_remove_property(const char* object_name, const char* property_name) {
    if (!object_name || !property_name) {
        KERROR("console_object_remove_property requires a valid pointer to property, property_name and object_name");
        return false;
    }

    // Make sure the object exists first.
    u32 object_count = darray_length(state_ptr->registered_objects);
    for (u32 i = 0; i < object_count; ++i) {
        if (strings_equali(state_ptr->registered_objects[i].name, object_name)) {
            console_object* obj = &state_ptr->registered_objects[i];
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
