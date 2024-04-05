#include "logger.h"
#include "debug/kassert.h"
#include "memory/kmemory.h"
#include "platform/filesystem.h"
#include "platform/platform.h"
#include "strings/kstring.h"

//
#include <stdarg.h>

// A console hook function pointer.
static PFN_console_write console_hook = 0;

void logger_console_write_hook_set(PFN_console_write hook) {
    console_hook = hook;
}

void _log_output(log_level level, const char* message, ...) {
    const char* level_strs[6] = {"[FATAL]: ", "[ERROR]: ", "[WARN]:  ", "[INFO]:  ", "[DEBUG]: ", "[TRACE]: "};
    // Technically imposes a 32k character limit on a single log entry, but...
    // DON'T DO THAT!
    char out_message[32000] = {0};
    string_format(out_message, "%s%s\n", level_strs[level], message);

    // Format original message.
    // NOTE: Oddly enough, MS's headers override the GCC/Clang va_list type with a "typedef char* va_list" in some
    // cases, and as a result throws a strange error here. The workaround for now is to just use __builtin_va_list,
    // which is the type GCC/Clang's va_start expects.
    __builtin_va_list arg_ptr;
    va_start(arg_ptr, message);
    string_format_v(out_message, message, arg_ptr);
    va_end(arg_ptr);

    // If the console hook is defined, make sure to forward messages to it, and it will pass along to consumers.
    // Otherwise the platform layer will be used directly.
    if (console_hook) {
        console_hook(level, out_message);
    } else {
        platform_console_write(0, level, out_message);
    }

    // Trigger a "debug break" for fatal errors.
    if (level == LOG_LEVEL_FATAL) {
        kdebug_break();
    }
}

void report_assertion_failure(const char* expression, const char* message, const char* file, i32 line) {
    _log_output(LOG_LEVEL_FATAL, "Assertion Failure: %s, message: '%s', in file: %s, line: %d\n", expression, message, file, line);
}
