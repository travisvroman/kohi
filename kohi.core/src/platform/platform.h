/**
 * @file platform.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains the platform layer, or at least the interface to it.
 * Each platform should provide its own implementation of this in a .c file, and
 * should be compiled exclusively of the rest.
 * @version 2.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2024
 *
 */

#pragma once

#include "defines.h"
#include "input_types.h"
#include "logger.h"

typedef struct platform_system_config {
    /** @brief application_name The name of the application. */
    const char* application_name;
} platform_system_config;

typedef struct dynamic_library_function {
    const char* name;
    void* pfn;
} dynamic_library_function;

typedef struct dynamic_library {
    const char* name;
    const char* filename;
    u64 internal_data_size;
    void* internal_data;
    u32 watch_id;

    // darray
    dynamic_library_function* functions;
} dynamic_library;

typedef enum platform_error_code {
    PLATFORM_ERROR_SUCCESS = 0,
    PLATFORM_ERROR_UNKNOWN = 1,
    PLATFORM_ERROR_FILE_NOT_FOUND = 2,
    PLATFORM_ERROR_FILE_LOCKED = 3,
    PLATFORM_ERROR_FILE_EXISTS = 4
} platform_error_code;

struct platform_state;

/**
 * @brief A configuration structure used to create new windows.
 */
typedef struct kwindow_config {
    i32 position_x;
    i32 position_y;
    u32 width;
    u32 height;
    const char* title;
    const char* name;
} kwindow_config;

struct kwindow_platform_state;
struct kwindow_renderer_state;

/**
 * @brief Represents a window in the application.
 */
typedef struct kwindow {
    /** @brief The internal name of the window. */
    const char* name;
    /** @brief The title of the window. */
    const char* title;

    u16 width;
    u16 height;
    b8 resizing;
    u16 frames_since_resize;

    /** @brief Holds platform-specific data. */
    struct kwindow_platform_state* platform_state;

    /** @brief Holds renderer-specific data. */
    struct kwindow_renderer_state* renderer_state;
} kwindow;

typedef void (*platform_filewatcher_file_deleted_callback)(u32 watcher_id);
typedef void (*platform_filewatcher_file_written_callback)(u32 watcher_id);
typedef void (*platform_window_closed_callback)(const struct kwindow* window);
typedef void (*platform_window_resized_callback)(const struct kwindow* window);
typedef void (*platform_process_key)(keys key, b8 pressed);
typedef void (*platform_process_mouse_button)(mouse_buttons button, b8 pressed);
typedef void (*platform_process_mouse_move)(i16 x, i16 y);
typedef void (*platform_process_mouse_wheel)(i8 z_delta);

/**
 * @brief Performs startup routines within the platform layer. Should be called twice,
 * once to obtain the memory requirement (with state=0), then a second time passing
 * an allocated block of memory to state.
 *
 * @param memory_requirement A pointer to hold the memory requirement in bytes.
 * @param state A pointer to a block of memory to hold state. If obtaining memory requirement only, pass 0.
 * @param config A pointer to a configuration platform_system_config structure required by this system.
 * @return True on success; otherwise false.
 */
KAPI b8 platform_system_startup(u64* memory_requirement, struct platform_state* state, platform_system_config* config);

/**
 * @brief Shuts down the platform layer.
 *
 * @param state A pointer to the platform layer state.
 */
KAPI void platform_system_shutdown(struct platform_state* state);

/**
 * @brief Creates a new window from the given config and optionally opens it immediately.
 *
 * @param config A constant pointer to the configuration to be used for creating the window.
 * @param window A pointer to hold the newly-created window.
 * @param show_immediately Indicates whether the window should open immediately upon creation.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 platform_window_create(const kwindow_config* config, struct kwindow* window, b8 show_immediately);

/**
 * @brief Destroys the given window.
 *
 * @param window A pointer to the window to be destroyed.
 */
KAPI void platform_window_destroy(struct kwindow* window);

/**
 * @brief Shows the given window. Has no effect if the window is already shown.
 *
 * @param window A pointer to the window to be shown.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 platform_window_show(struct kwindow* window);

/**
 * @brief Hides the given window. Has no effect if the window is already hidden.
 * Note that this does not destroy the window.
 *
 * @param window A pointer to the window to be hidden.
 * @return b8 True on success; otherwise false.
 */
KAPI b8 platform_window_hide(struct kwindow* window);

/**
 * @brief Obtains the window title from the given window, if it exists.
 * NOTE: Returns a constant copy of the string, which must be freed by the caller.
 *
 * @param window A pointer to the window whose title to get.
 * @return A constant copy of the window's title, or 0 if there is no title or the window doesn't exist.
 */
KAPI const char* platform_window_title_get(const struct kwindow* window);

/**
 * @brief Sets the given window's title, if it exists.
 *
 * @param window A pointer to the window whose title is to be set.
 * @param title The title to be set.
 * @return KAPI True on success; otherwise false.
 */
KAPI b8 platform_window_title_set(struct kwindow* window, const char* title);

/**
 * @brief Performs any platform-specific message pumping that is required
 * for windowing, etc.
 *
 * @return True on success; otherwise false.
 */
KAPI b8 platform_pump_messages(void);

/**
 * @brief Performs platform-specific memory allocation of the given size.
 *
 * @param size The size of the allocation in bytes.
 * @param aligned Indicates if the allocation should be aligned.
 * @return A pointer to a block of allocated memory.
 */
KAPI void* platform_allocate(u64 size, b8 aligned);

/**
 * @brief Frees the given block of memory.
 *
 * @param block The block to be freed.
 * @param aligned Indicates if the block of memory is aligned.
 */
KAPI void platform_free(void* block, b8 aligned);

/**
 * @brief Performs platform-specific zeroing out of the given block of memory.
 *
 * @param block The block to be zeroed out.
 * @param size The size of data to zero out.
 * @return A pointer to the zeroed out block of memory.
 */
KAPI void* platform_zero_memory(void* block, u64 size);

/**
 * @brief Copies the bytes of memory in source to dest, of the given size.
 *
 * @param dest The destination memory block.
 * @param source The source memory block.
 * @param size The size of data to be copied.
 * @return A pointer to the destination block of memory.
 */
KAPI void* platform_copy_memory(void* dest, const void* source, u64 size);

/**
 * @brief Sets the bytes of memory to the given value.
 *
 * @param dest The destination block of memory.
 * @param value The value to be set.
 * @param size The size of data to set.
 * @return A pointer to the set block of memory.
 */
KAPI void* platform_set_memory(void* dest, i32 value, u64 size);

/**
 * @brief Performs platform-specific printing to the console of the given
 * message and log level.
 *
 * @param platform A pointer to the platform state.
 * @param message The message to be printed.
 * @param colour The colour to print the text in (if supported).
 */
KAPI void platform_console_write(struct platform_state* platform, log_level level, const char* message);

/**
 * @brief Gets the absolute time since the application started.
 *
 * @return The absolute time since the application started.
 */
KAPI f64 platform_get_absolute_time(void);

/**
 * @brief Sleep on the thread for the provided milliseconds. This blocks the main thread.
 * Should only be used for giving time back to the OS for unused update power.
 * Therefore it is not exported. Times are approximate.
 *
 * @param ms The number of milliseconds to sleep for.
 */
KAPI void platform_sleep(u64 ms);

/**
 * @brief Obtains the number of logical processor cores.
 *
 * @return The number of logical processor cores.
 */
KAPI i32 platform_get_processor_count(void);

/**
 * @brief Obtains the required memory amount for platform-specific handle data,
 * and optionally obtains a copy of that data. Call twice, once with memory=0
 * to obtain size, then a second time where memory = allocated block.
 *
 * @param out_size A pointer to hold the memory requirement.
 * @param memory Allocated block of memory.
 */
KAPI void platform_get_handle_info(u64* out_size, void* memory);

/**
 * @brief Returns the device pixel ratio of the main window.
 */
KAPI f32 platform_device_pixel_ratio(void);

/**
 * @brief Loads a dynamic library.
 *
 * @param name The name of the library file, *excluding* the extension. Required.
 * @param out_library A pointer to hold the loaded library. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 platform_dynamic_library_load(const char* name, dynamic_library* out_library);

/**
 * @brief Unloads the given dynamic library.
 *
 * @param library A pointer to the loaded library. Required.
 * @return True on success; otherwise false.
 */
KAPI b8 platform_dynamic_library_unload(dynamic_library* library);

/**
 * @brief Loads an exported function of the given name from the provided loaded library.
 *
 * @param name The function name to be loaded.
 * @param library A pointer to the library to load the function from.
 * @return A pointer to the loaded function if it exists; otherwise 0/null.
 */
KAPI void* platform_dynamic_library_load_function(const char* name, dynamic_library* library);

/**
 * @brief Returns the file extension for the current platform.
 */
KAPI const char* platform_dynamic_library_extension(void);

/**
 * @brief Returns a file prefix for libraries for the current platform.
 */
KAPI const char* platform_dynamic_library_prefix(void);

/**
 * @brief Copies file at source to destination, optionally overwriting.
 *
 * @param source The source file path.
 * @param dest The destination file path.
 * @param overwrite_if_exists Indicates if the file should be overwritten if it exists.
 * @return An error code indicating success or failure.
 */
KAPI platform_error_code platform_copy_file(const char* source, const char* dest, b8 overwrite_if_exists);

/**
 * @brief Registers the system-level handler for filewatcher "file deleted" events. This can be hooked
 * up by the engine or application.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_watcher_deleted_callback(platform_filewatcher_file_deleted_callback callback);

/**
 * @brief Registers the system-level handler for filewatcher "file written" events. This can be hooked
 * up by the engine or application.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_watcher_written_callback(platform_filewatcher_file_written_callback callback);

/**
 * @brief Registers the system-level handler for a window being closed.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_window_closed_callback(platform_window_closed_callback callback);

/**
 * @brief Registers the system-level handler for a window being resized.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_window_resized_callback(platform_window_resized_callback callback);

/**
 * @brief Registers the system-level handler for a keyboard key being pressed.
 *
 * @param callback A pointer to the handler function.
 * @return KAPI
 */
KAPI void platform_register_process_key(platform_process_key callback);

/**
 * @brief Registers the system-level handler for a mouse button being pressed.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_process_mouse_button_callback(platform_process_mouse_button callback);

/**
 * @brief Registers the system-level handler for a mouse being moved.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_process_mouse_move_callback(platform_process_mouse_move callback);

/**
 * @brief Registers the system-level handler for a mouse wheel being scrolled.
 *
 * @param callback A pointer to the handler function.
 */
KAPI void platform_register_process_mouse_wheel_callback(platform_process_mouse_wheel callback);

/**
 * @brief Watch a file at the given path.
 *
 * @param file_path The file path. Required.
 * @param out_watch_id A pointer to hold the watch identifier.
 * @return True on success; otherwise false.
 */
KAPI b8 platform_watch_file(const char* file_path, u32* out_watch_id);

/**
 * @brief Stops watching the file with the given watch identifier.
 *
 * @param watch_id The watch identifier
 * @return True on success; otherwise false.
 */
KAPI b8 platform_unwatch_file(u32 watch_id);
