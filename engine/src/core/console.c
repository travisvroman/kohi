#include "console.h"
#include "kmemory.h"
#include "asserts.h"

#include "core/kstring.h"
#include "containers/darray.h"

typedef struct console_consumer {
    PFN_console_consumer_write callback;
    void* instance;
} console_consumer;

typedef struct console_command {
    const char* name;
    u8 arg_count;
    PFN_console_command func;
} console_command;

typedef struct console_state {
    u8 consumer_count;
    console_consumer* consumers;

    // darray of registered commands.
    console_command* registered_commands;
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

    return true;
}

void console_shutdown(void* state) {
    if (state_ptr) {
        darray_destroy(state_ptr->registered_commands);

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

b8 console_command_execute(const char* command) {
    if (!command) {
        return false;
    }
    char** parts = darray_create(char*);
    // TODO: If strings are ever used as arguments, this will split improperly.
    u32 part_count = string_split(command, ' ', &parts, true, false);
    if (part_count < 1) {
        string_cleanup_split_array(parts);
        darray_destroy(parts);
        return false;
    }

    // Write the line back out to the console for reference.
    char temp[512] = {0};
    string_format(temp, "-->%s", command);
    console_write_line(LOG_LEVEL_INFO, temp);

    // Yep, strings are slow. But it's a console. It doesn't need to be lightning fast...
    b8 has_error = false;
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

    string_cleanup_split_array(parts);
    darray_destroy(parts);

    return !has_error;
}
