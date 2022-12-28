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

void console_initialize(u64* memory_requirement, void* memory) {
    *memory_requirement = sizeof(console_state) + (sizeof(console_consumer) * MAX_CONSUMER_COUNT);

    if (!memory) {
        return;
    }

    kzero_memory(memory, *memory_requirement);
    state_ptr = memory;
    state_ptr->consumers = (console_consumer*)((u64)memory + sizeof(console_state));

    state_ptr->registered_commands = darray_create(console_command);
}

void console_shutdown(void* state) {
    if (state) {
        kzero_memory(state, sizeof(console_state) + (sizeof(console_consumer) * MAX_CONSUMER_COUNT));
    }

    state_ptr = 0;
}

void console_register_consumer(void* inst, PFN_console_consumer_write callback) {
    if (state_ptr) {
        KASSERT_MSG(state_ptr->consumer_count + 1 < MAX_CONSUMER_COUNT, "Max console consumers reached.");

        console_consumer* consumer = &state_ptr->consumers[state_ptr->consumer_count];
        consumer->instance = inst;
        consumer->callback = callback;
        state_ptr->consumer_count++;
    }
}

void console_write_line(log_level level, const char* message) {
    if (state_ptr) {
        // Notify each consumer that a line has been added.
        for (u8 i = 0; i < state_ptr->consumer_count; ++i) {
            console_consumer* consumer = &state_ptr->consumers[i];
            consumer->callback(consumer->instance, level, message);
        }
    }
}

b8 console_register_command(const char* command, u8 arg_count, PFN_console_command func) {
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

b8 console_execute_command(const char* command) {
    if (!command) {
        return false;
    }
    char** parts = darray_create(char*);
    u32 part_count = string_split(command, ' ', &parts, true, false);
    if (part_count < 1) {
        string_cleanup_split_array(parts);
        darray_destroy(parts);
        return false;
    }

    // TODO: temp
    char temp[512] = {0};
    string_format(temp, "-->%s", parts[0]);
    console_write_line(LOG_LEVEL_INFO, temp);

    // Yep, strings are slow. But it's a console. It doesn't need to be lightning fast...
    b8 has_error = false;
    u32 command_count = darray_length(state_ptr->registered_commands);
    for (u32 i = 0; i < command_count; ++i) {
        console_command* cmd = &state_ptr->registered_commands[i];
        if (strings_equali(cmd->name, command)) {
            u8 arg_count = part_count - 1;
            if (state_ptr->registered_commands[i].arg_count != arg_count) {
                KERROR("The console command '%s' requires %u arguments but %u were provided.", cmd->name, cmd->arg_count, arg_count);
                has_error = true;
            } else {
                // Execute it
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

    string_cleanup_split_array(parts);
    darray_destroy(parts);

    return !has_error;
}