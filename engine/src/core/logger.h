/**
 * @file logger.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains structures and logic pertaining to the logging system.
 * @version 1.0
 * @date 2022-01-10
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once

#include "defines.h"

/** @brief Indicates if warning level logging is enabled. */
#define LOG_WARN_ENABLED 1
/** @brief Indicates if info level logging is enabled. */
#define LOG_INFO_ENABLED 1
/** @brief Indicates if debug level logging is enabled. */
#define LOG_DEBUG_ENABLED 1
/** @brief Indicates if trace level logging is enabled. */
#define LOG_TRACE_ENABLED 1

// Disable debug and trace logging for release builds.
#if KRELEASE == 1
#define LOG_DEBUG_ENABLED 0
#define LOG_TRACE_ENABLED 0
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

/**
 * @brief Initializes logging system. Call twice; once with state = 0 to get required memory size,
 * then a second time passing allocated memory to state.
 * 
 * @param memory_requirement A pointer to hold the required memory size of internal state.
 * @param state 0 if just requesting memory requirement, otherwise allocated block of memory.
 * @return b8 True on success; otherwise false.
 */
b8 initialize_logging(u64* memory_requirement, void* state);

/**
 * @brief Shuts down the logging system.
 * @param state A pointer to the system state.
 */
void shutdown_logging(void* state);

/**
 * @brief Outputs logging at the given level.
 * @param level The log level to use.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
KAPI void log_output(log_level level, const char* message, ...);

/** 
 * @brief Logs a fatal-level message. Should be used to stop the application when hit.
 * @param message The message to be logged. Can be a format string for additional parameters.
 * @param ... Additional parameters to be logged.
 */
#define KFATAL(message, ...) log_output(LOG_LEVEL_FATAL, message, ##__VA_ARGS__);

#ifndef KERROR
/** 
 * @brief Logs an error-level message. Should be used to indicate critical runtime problems 
 * that cause the application to run improperly or not at all.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KERROR(message, ...) log_output(LOG_LEVEL_ERROR, message, ##__VA_ARGS__);
#endif

#if LOG_WARN_ENABLED == 1
/** 
 * @brief Logs a warning-level message. Should be used to indicate non-critial problems with 
 * the application that cause it to run suboptimally.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KWARN(message, ...) log_output(LOG_LEVEL_WARN, message, ##__VA_ARGS__);
#else
/** 
 * @brief Logs a warning-level message. Should be used to indicate non-critial problems with 
 * the application that cause it to run suboptimally. Does nothing when LOG_WARN_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KWARN(message, ...)
#endif

#if LOG_INFO_ENABLED == 1
/** 
 * @brief Logs an info-level message. Should be used for non-erronuous informational purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KINFO(message, ...) log_output(LOG_LEVEL_INFO, message, ##__VA_ARGS__);
#else
/** 
 * @brief Logs an info-level message. Should be used for non-erronuous informational purposes.
 * Does nothing when LOG_INFO_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KINFO(message, ...)
#endif

#if LOG_DEBUG_ENABLED == 1
/** 
 * @brief Logs a debug-level message. Should be used for debugging purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KDEBUG(message, ...) log_output(LOG_LEVEL_DEBUG, message, ##__VA_ARGS__);
#else
/** 
 * @brief Logs a debug-level message. Should be used for debugging purposes.
 * Does nothing when LOG_DEBUG_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KDEBUG(message, ...)
#endif

#if LOG_TRACE_ENABLED == 1
/** 
 * @brief Logs a trace-level message. Should be used for verbose debugging purposes.
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KTRACE(message, ...) log_output(LOG_LEVEL_TRACE, message, ##__VA_ARGS__);
#else
/** 
 * @brief Logs a trace-level message. Should be used for verbose debugging purposes.
 * Does nothing when LOG_TRACE_ENABLED != 1
 * @param message The message to be logged.
 * @param ... Any formatted data that should be included in the log entry.
 */
#define KTRACE(message, ...)
#endif
