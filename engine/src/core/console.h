/**
 * @file console.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Contains the console system, which disperses all logging
 * to registered consumers as well as handles registered command
 * inputs.
 * @version 1.0
 * @date 2023-01-18
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2023
 *
 */
#pragma once

#include "defines.h"
#include "logger.h"

/**
 * @brief Typedef for a console consumer write function, which
 * is invoked every time a logging event occurs. Consumers must
 * implement this and handle the input thusly.
 */
typedef b8 (*PFN_console_consumer_write)(void* inst, log_level level, const char* message);

/**
 * @brief Represents a single console command argument's value.
 * Always represented as a string, it is up to the console command
 * function to interpret and convert it to the required type during
 * processing.
 */
typedef struct console_command_argument {
    /** @brief The argument's value. */
    const char* value;
} console_command_argument;

/**
 * @brief Context to be passed along with an executing console
 * command (i.e. arguments to the command).
 */
typedef struct console_command_context {
    /** @brief The number of arguments passed.*/
    u8 argument_count;
    /** @brief The arguments array. */
    console_command_argument* arguments;
} console_command_context;

/**
 * @brief A typedef for a function pointer which represents a registered
 * console command, and is called when triggered by some means of console
 * input (typically a debug console).
 */
typedef void (*PFN_console_command)(console_command_context context);

/**
 * @brief Initializes the console system. As with other systems, must be
 * called twice; once to get the memory requirement (where memory = 0), and
 * a second time passing allocated memory.
 *
 * @param memory_requirement A pointer to hold the memory requirement.
 * @param memory The allocated memory for this system's state.
 * @param config Ignored.
 * @return True on success; otherwise false.
 */
b8 console_initialize(u64* memory_requirement, void* memory, void* config);
/**
 * @brief Shuts down the console system.
 *
 * @param state A pointer to the console system state.
 */
void console_shutdown(void* state);

/**
 * @brief Registers a console consumer with the console system.
 *
 * @param inst Instance information to pass along with the consumer.
 * @param callback The callback to be made on console write.
 */
KAPI void console_register_consumer(void* inst, PFN_console_consumer_write callback, u8* out_consumer_id);

/**
 * @brief Updates the instance and callback for the consumer with the given identifier.
 *
 * @param consumer_id The identifier of the consumer to update.
 * @param inst The consumer instance.
 * @param callback The new callback function.
 */
KAPI void console_update_consumer(u8 consumer_id, void* inst, PFN_console_consumer_write callback);

/**
 * @brief Called internally by the logging system to write a new line
 * to the console.
 *
 * @param level The logging level.
 * @param message The message to be written.
 */
void console_write_line(log_level level, const char* message);

/**
 * @brief Registers a console command with the console system.
 *
 * @param command The name of the command.
 * @param arg_count The number of required arguments.
 * @param func The function pointer to be invoked.
 * @return True on success; otherwise false.
 */
KAPI b8 console_register_command(const char* command, u8 arg_count, PFN_console_command func);

/**
 * @brief Unregisters the given command.
 *
 * @param command The name of the command to be unregistered.
 * @return True on success; otherwise false.
 */
KAPI b8 console_unregister_command(const char* command);

/**
 * @brief Executes a console command.
 *
 * @param command The command, including arguments separated by spaces. ex: "kvar_int_set test_var 4"
 * @return True on success; otherwise false.
 */
KAPI b8 console_execute_command(const char* command);
