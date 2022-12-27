#include "console.h"
#include "kmemory.h"
#include "asserts.h"

#include "core/kstring.h"
#include "containers/darray.h"

typedef struct console_consumer {
    PFN_console_consumer_write callback;
    void* instance;
} console_consumer;

typedef struct console_state {
    u8 consumer_count;
    console_consumer* consumers;
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

b8 console_register_command(const char* command, u8 arg_count) {
    return false;
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

    string_cleanup_split_array(parts);
    darray_destroy(parts);

    return true;
}