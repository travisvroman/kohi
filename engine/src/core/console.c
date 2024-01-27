#include "console.h"

#include <math.h>

#include "asserts.h"
#include "containers/darray.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "kmemory.h"

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

b8 console_initialize(u64* memory_requirement, void* memory, void* config) {
    *memory_requirement = sizeof(console_state) + (sizeof(console_consumer) * MAX_CONSUMER_COUNT);

    if (!memory) {
        return true;
    }

    kzero_memory(memory, *memory_requirement);
    state_ptr = memory;
    state_ptr->consumers = (console_consumer*)((u64)memory + sizeof(console_state));

    state_ptr->registered_commands = darray_create(console_command);
    state_ptr->registered_objects = darray_create(console_object);

    return true;
}

void console_shutdown(void* state) {
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

void console_write_line(log_level level, const char* message) {
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
        case CONSOLE_OBJECT_TYPE_BOOL:
            b8 val = *((b8*)obj->block);
            KINFO("%s%s", indent_buffer, val ? "true" : "false");
            break;
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

static b8 console_expression_parse(const char* expression, console_object_type* out_type, void* out_value) {
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
    string_format(temp, "-->%s", command);
    console_write_line(LOG_LEVEL_INFO, temp);

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
    string_cleanup_split_array(parts);
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
