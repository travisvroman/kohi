/**
 * @file logger.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains structures and logic pertaining to the logging system.
 * @version 2.0
 * @date 2024-04-02
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"

/** @brief Indicates if warning level logging is enabled. */
#define LOG_WARN_ENABLED 1
/** @brief Indicates if info level logging is enabled. */
#define LOG_INFO_ENABLED 1

// Disable debug and trace logging for release builds.
#if KRELEASE == 1
#    define LOG_DEBUG_ENABLED 0
#    define LOG_TRACE_ENABLED 0
#else
/** @brief Indicates if debug level logging is enabled. */
#    define LOG_DEBUG_ENABLED 1
/** @brief Indicates if trace level logging is enabled. */
#    define LOG_TRACE_ENABLED 1
#endif

/** @brief Represents levels of logging */
typedef enum log_level {
    /** @brief Fatal log level, should be used to stop the application when hit. */
    LOG_LEVEL_FATAL = 0,
    /** @brief Error log level, should be used to indicate critical runtime problems that cause the application to run improperly or not at all. */
    LOG_LEVEL_ERROR = 1,
    /** @brief Warning log level, should be used to indicate non-critial problems with the application that cause it to run suboptimally. */
    LOG_LEVEL_WARN = 2,
    /** @brief Info log level, should be used for non-erronuous informational purposes. */
    LOG_LEVEL_INFO = 3,
    /** @brief Debug log level, should be used for debugging purposes. */
    LOG_LEVEL_DEBUG = 4,
    /** @brief Trace log level, should be used for verbose debugging purposes. */
    LOG_LEVEL_TRACE = 5
} log_level;

// A function pointer for a console to hook into the logger.
typedef void (*PFN_console_write)(log_level level, const char* message);

/**
 * @brief Provides a hook to a console (perhaps from Kohi Runtime or elsewhere) that the
 * logging system can forward messages to. If not set, logs go straight to the platform
 * layer. If set, messages go to the hook _instead_, so it would be responsible for passing
 * messages to the platform layer.
 * NOTE: May only be set once - additional calls will overwrite.
 *
 * @param hook A function pointer from the console hook.
 */
KAPI void logger_console_write_hook_set(PFN_console_write hook);

/**
 * @brief Outputs logging at the given level. NOTE: This should not be called directly.
 * @param level The log level to use.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
KAPI void _log_output(log_level level, const char* message, ...);

/**
 * @brief Logs a fatal-level message. Should be used to stop the application when hit.
 * @param message The message to be logged. Can be a format string for additional parameters.
 * @param ... Additional parameters to be logged.
 */
#define KFATAL(message, ...) _log_output(LOG_LEVEL_FATAL, message, ##__VA_ARGS__);

#ifndef KERROR
/**
 * @brief Logs an error-level message. Should be used to indicate critical runtime problems
 * that cause the application to run improperly or not at all.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KERROR(message, ...) _log_output(LOG_LEVEL_ERROR, message, ##__VA_ARGS__);
#endif

#if LOG_WARN_ENABLED == 1
/**
 * @brief Logs a warning-level message. Should be used to indicate non-critial problems with
 * the application that cause it to run suboptimally.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KWARN(message, ...) _log_output(LOG_LEVEL_WARN, message, ##__VA_ARGS__);
#else
/**
 * @brief Logs a warning-level message. Should be used to indicate non-critial problems with
 * the application that cause it to run suboptimally. Does nothing when LOG_WARN_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KWARN(message, ...)
#endif

#if LOG_INFO_ENABLED == 1
/**
 * @brief Logs an info-level message. Should be used for non-erronuous informational purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KINFO(message, ...) _log_output(LOG_LEVEL_INFO, message, ##__VA_ARGS__);
#else
/**
 * @brief Logs an info-level message. Should be used for non-erronuous informational purposes.
 * Does nothing when LOG_INFO_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KINFO(message, ...)
#endif

#if LOG_DEBUG_ENABLED == 1
/**
 * @brief Logs a debug-level message. Should be used for debugging purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KDEBUG(message, ...) _log_output(LOG_LEVEL_DEBUG, message, ##__VA_ARGS__);
#else
/**
 * @brief Logs a debug-level message. Should be used for debugging purposes.
 * Does nothing when LOG_DEBUG_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KDEBUG(message, ...)
#endif

#if LOG_TRACE_ENABLED == 1
/**
 * @brief Logs a trace-level message. Should be used for verbose debugging purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KTRACE(message, ...) _log_output(LOG_LEVEL_TRACE, message, ##__VA_ARGS__);
#else
/**
 * @brief Logs a trace-level message. Should be used for verbose debugging purposes.
 * Does nothing when LOG_TRACE_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#    define KTRACE(message, ...)
#endif
