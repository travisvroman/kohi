/**
 * @file event.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief This file contains structures and functions specific to the
 * event system.
 * Events are a mechenism that allows the developer to send and recieve
 * data at critical points in the execution of the application in a non-
 * coupled way. For now, this follows a simple pub-sub model of event
 * transmission.
 * @version 1.0
 * @date 2022-01-10
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 *
 */

#pragma once

#include "defines.h"

/**
 * @brief Represents event contextual data to be sent along with an
 * event code when an event is fired.
 * It is a union that is 128 bytes in size, meaning data can be mixed
 * and matched as required by the developer.
 * */
typedef struct event_context {
    // 128 bytes
    union {
        /** @brief An array of 2 64-bit signed integers. */
        i64 i64[2];
        /** @brief An array of 2 64-bit unsigned integers. */
        u64 u64[2];

        /** @brief An array of 2 64-bit floating-point numbers. */
        f64 f64[2];

        /** @brief An array of 4 32-bit signed integers. */
        i32 i32[4];
        /** @brief An array of 4 32-bit unsigned integers. */
        u32 u32[4];
        /** @brief An array of 4 32-bit floating-point numbers. */
        f32 f32[4];

        /** @brief An array of 8 16-bit signed integers. */
        i16 i16[8];

        /** @brief An array of 8 16-bit unsigned integers. */
        u16 u16[8];

        /** @brief An array of 16 8-bit signed integers. */
        i8 i8[16];
        /** @brief An array of 16 8-bit unsigned integers. */
        u8 u8[16];

        /**
         * @brief Allows a pointer to arbitrary data to be passed. Also includes size info.
         * NOTE: If used, should be freed by the sender or listener.
         */
        union {
            // The size of the data pointed to.
            u64 size;
            // A pointer to a memory block of data to be included with the event.
            void* data;
        } custom_data;

        /** @brief A free-form string. If used, should be freed by sender or listener. */
        const char* s;
    } data;
} event_context;

/**
 * @brief A function pointer typedef which is used for event subscriptions by the subscriber.
 * @param code The event code to be sent.
 * @param sender A pointer to the sender of the event. Can be 0.
 * @param listener_inst A pointer to the listener of the event. Can be 0.
 * @param data The event context to be passed with the fired event.
 * @returns True if the message should be considered handled, which means that it will not
 * be sent to any other consumers; otherwise false.
 */
typedef b8 (*PFN_on_event)(u16 code, void* sender, void* listener_inst, event_context data);

/**
 * @brief Initializes the event system.
 */
b8 event_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts the event system down.
 */
void event_system_shutdown(void* state);

/**
 * @brief Register to listen for when events are sent with the provided code. Events with duplicate
 * listener/callback combos will not be registered again and will cause this to return false.
 * @param code The event code to listen for.
 * @param listener A pointer to a listener instance. Can be 0/NULL.
 * @param on_event The callback function pointer to be invoked when the event code is fired.
 * @returns True if the event is successfully registered; otherwise false.
 */
KAPI b8 event_register(u16 code, void* listener, PFN_on_event on_event);

/**
 * @brief Unregister from listening for when events are sent with the provided code. If no matching
 * registration is found, this function returns false.
 * @param code The event code to stop listening for.
 * @param listener A pointer to a listener instance. Can be 0/NULL.
 * @param on_event The callback function pointer to be unregistered.
 * @returns True if the event is successfully unregistered; otherwise false.
 */
KAPI b8 event_unregister(u16 code, void* listener, PFN_on_event on_event);

/**
 * @brief Fires an event to listeners of the given code. If an event handler returns
 * true, the event is considered handled and is not passed on to any more listeners.
 * @param code The event code to fire.
 * @param sender A pointer to the sender. Can be 0/NULL.
 * @param data The event data.
 * @returns True if handled, otherwise false.
 */
KAPI b8 event_fire(u16 code, void* sender, event_context context);

/** @brief System internal event codes. Application should use codes beyond 255. */
typedef enum system_event_code {
    /** @brief Shuts the application down on the next frame. */
    EVENT_CODE_APPLICATION_QUIT = 0x01,

    /** @brief Keyboard key pressed.
     * Context usage:
     * u16 key_code = data.data.u16[0];
     * u16 repeat_count = data.data.u16[1];
     */
    EVENT_CODE_KEY_PRESSED = 0x02,

    /** @brief Keyboard key released.
     * Context usage:
     * u16 key_code = data.data.u16[0];
     * u16 repeat_count = data.data.u16[1];
     */
    EVENT_CODE_KEY_RELEASED = 0x03,

    /** @brief Mouse button pressed.
     * Context usage:
     * u16 button = data.data.u16[0];
     * u16 x = data.data.i16[1];
     * u16 y = data.data.i16[2];
     */
    EVENT_CODE_BUTTON_PRESSED = 0x04,

    /** @brief Mouse button released.
     * Context usage:
     * u16 button = data.data.u16[0];
     * u16 x = data.data.i16[1];
     * u16 y = data.data.i16[2];
     */
    EVENT_CODE_BUTTON_RELEASED = 0x05,

    /** @brief Mouse button pressed then released.
     * Context usage:
     * u16 button = data.data.u16[0];
     * u16 x = data.data.i16[1];
     * u16 y = data.data.i16[2];
     */
    EVENT_CODE_BUTTON_CLICKED = 0x06,

    /** @brief Mouse moved.
     * Context usage:
     * u16 x = data.data.i16[0];
     * u16 y = data.data.i16[1];
     */
    EVENT_CODE_MOUSE_MOVED = 0x07,

    /** @brief Mouse moved.
     * Context usage:
     * ui z_delta = data.data.i8[0];
     */
    EVENT_CODE_MOUSE_WHEEL = 0x08,

    /** @brief Resized/resolution of a window changed from the OS.
     * Context usage:
     * u16 width = data.data.u16[0];
     * u16 height = data.data.u16[1];
     * Sender is the window itself.
     */
    EVENT_CODE_WINDOW_RESIZED = 0x09,

    // Change the render mode for debugging purposes.
    /* Context usage:
     * i32 mode = context.data.i32[0];
     */
    EVENT_CODE_SET_RENDER_MODE = 0x0A,

    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG0 = 0x10,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG1 = 0x11,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG2 = 0x12,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG3 = 0x13,
    /** @brief Special-purpose debugging event. Context will vary over time. */
    EVENT_CODE_DEBUG4 = 0x14,

    EVENT_CODE_DEBUG5 = 0x15,
    EVENT_CODE_DEBUG6 = 0x16,
    EVENT_CODE_DEBUG7 = 0x17,
    EVENT_CODE_DEBUG8 = 0x18,
    EVENT_CODE_DEBUG9 = 0x19,
    EVENT_CODE_DEBUG10 = 0x1A,
    EVENT_CODE_DEBUG11 = 0x1B,
    EVENT_CODE_DEBUG12 = 0x1C,
    EVENT_CODE_DEBUG13 = 0x1D,
    EVENT_CODE_DEBUG14 = 0x1E,
    EVENT_CODE_DEBUG15 = 0x1F,

    /** @brief The hovered-over object id, if there is one.
     * Context usage:
     * i32 id = context.data.u32[0]; - will be INVALID ID if nothing is hovered over.
     */
    EVENT_CODE_OBJECT_HOVER_ID_CHANGED = 0x20,

    /**
     * @brief An event fired by the renderer backend to indicate when any render targets
     * associated with the default window resources need to be refreshed (i.e. a window resize)
     */
    EVENT_CODE_DEFAULT_RENDERTARGET_REFRESH_REQUIRED = 0x21,

    /**
     * @brief An event fired by the kvar system when a kvar has been updated.
     * Context usage:
     * kvar_change* change = context.data.custom_data.data;
     */
    EVENT_CODE_KVAR_CHANGED = 0x22,

#if KOHI_HOT_RELOAD
    /**
     * @brief An event fired from the asset system when a watched asset file has been written to.
     * Context usage:
     * u32 watch_id = context.data.u32[0];
     * kasset* = sender;
     */
    EVENT_CODE_ASSET_HOT_RELOADED = 0x23,

    /**
     * @brief An event fired when a watched file has written to disk.
     * Context usage:
     * u32 watch_id = context.data.u32[0];
     * vfs_asset_data* = sender
     */
    EVENT_CODE_VFS_FILE_WRITTEN_TO_DISK = 0x24,

    /**
     * @brief An event fired when a watched file has been removed from disk.
     * No asset data is included (obviously)
     * Context usage:
     * u32 watch_id = context.data.u32[0];
     */
    EVENT_CODE_VFS_FILE_DELETED_FROM_DISK = 0x25,

#endif

    /**
     * @brief An event fired while a button is being held down and the
     * mouse is moved.
     *
     * Context usage:
     * i16 x = context.data.i16[0]
     * i16 y = context.data.i16[1]
     * u16 button = context.data.u16[2]
     */
    EVENT_CODE_MOUSE_DRAGGED = 0x30,

    /**
     * @brief An event fired when a button is pressed and a mouse movement
     * is done while it is pressed.
     *
     * Context usage:
     * i16 x = context.data.i16[0]
     * i16 y = context.data.i16[1]
     * u16 button = context.data.u16[2]
     */
    EVENT_CODE_MOUSE_DRAG_BEGIN = 0x31,

    /**
     * @brief An event fired when a button is released was previously dragging.
     *
     * Context usage:
     * i16 x = context.data.i16[0]
     * i16 y = context.data.i16[1]
     * u16 button = context.data.u16[2]
     */
    EVENT_CODE_MOUSE_DRAG_END = 0x32,

    /** @brief The maximum event code that can be used internally. */
    MAX_EVENT_CODE = 0xFF
} system_event_code;
