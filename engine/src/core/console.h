#pragma once

#include "defines.h"
#include "logger.h"

typedef b8 (*PFN_console_consumer_write)(void* inst, log_level level, const char* message);

typedef struct console_command_argument {
    const char* value;
} console_command_argument;

typedef struct console_command_context {
    u8 argument_count;
    console_command_argument* arguments;
} console_command_context;

typedef void (*PFN_console_command)(console_command_context context);

b8 console_initialize(u64* memory_requirement, void* memory, void* config);
void console_shutdown(void* state);

KAPI void console_register_consumer(void* inst, PFN_console_consumer_write callback);

void console_write_line(log_level level, const char* message);

KAPI b8 console_register_command(const char* command, u8 arg_count, PFN_console_command func);

KAPI b8 console_execute_command(const char* command);
