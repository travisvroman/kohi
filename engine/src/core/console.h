#pragma once

#include "defines.h"
#include "logger.h"

typedef b8 (*PFN_console_consumer_write)(void* inst, log_level level, const char* message);

void console_initialize(u64* memory_requirement, void* memory);
void console_shutdown(void* state);

KAPI void console_register_consumer(void* inst, PFN_console_consumer_write callback);

void console_write_line(log_level level, const char* message);
