#include "platform.h"

// Linux platform layer.
#if KPLATFORM_LINUX

#include "core/logger.h"
#include "core/event.h"
#include "core/input.h"

#include "containers/darray.h"

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h> // sudo apt install libxcb-keysym1-dev
#include <xcb/xkb.h>       // sudo apt install libxcb-xkb-dev
#include <sys/time.h>

#if _POSIX_C_SOURCE >= 199309L
#include <time.h>  // nanosleep
#else
#include <unistd.h>  // usleep
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// For surface creation
#define VK_USE_PLATFORM_XCB_KHR
#include <vulkan/vulkan.h>
#include "renderer/vulkan/vulkan_types.inl"

typedef struct platform_state {
    xcb_connection_t* connection;
    xcb_window_t window;
    xcb_screen_t* screen;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_win;
    xcb_key_symbols_t *syms;
    VkSurfaceKHR surface;
} platform_state;

static platform_state* state_ptr;

// Key translation
keys translate_keycode(u32 x_keycode);

b8 platform_system_startup(
    u64* memory_requirement,
    void* state,
    const char* application_name,
    i32 x,
    i32 y,
    i32 width,
    i32 height) {
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }

    state_ptr = state;

    // we get the connection through xcb
    int screenp = 0;
    state_ptr->connection = xcb_connect(NULL, &screenp);
    if (xcb_connection_has_error(state_ptr->connection)) {
        KFATAL("Failed to connect to X server via XCB.");
        return false;
    }

    // unlike most reply_t this one must not be freed.
    const xcb_query_extension_reply_t *ext_reply  = xcb_get_extension_data(state_ptr->connection, &xcb_xkb_id);
    if (!ext_reply) {
        KFATAL("XKB extension not available on host X11 server.");
        return false;
    }

    // we can now load xcb's extensions (xkb)
    xcb_generic_error_t *error;
    xcb_xkb_use_extension_cookie_t use_ext_cookie;
    xcb_xkb_use_extension_reply_t *use_ext_reply = NULL;
    use_ext_cookie = xcb_xkb_use_extension(state_ptr->connection, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
    use_ext_reply = xcb_xkb_use_extension_reply(state_ptr->connection, use_ext_cookie, &error);
    if (!use_ext_reply) {
        KFATAL("Couldn't load the xcb-xkb extension");
        free(use_ext_reply);
        return false;
    }
    if (!use_ext_reply->supported) {
        KFATAL("The XKB extension is not supported on this X server.");
        free(use_ext_reply);
        return false;
    }
    free(use_ext_reply);

    // we can now deactivate repeat for this app only
    xcb_xkb_per_client_flags_cookie_t pcf_cookie;
    xcb_xkb_per_client_flags_reply_t *pcf_reply;

    pcf_cookie = xcb_xkb_per_client_flags(state_ptr->connection, XCB_XKB_ID_USE_CORE_KBD, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 0, 0, 0);
    pcf_reply = xcb_xkb_per_client_flags_reply(state_ptr->connection, pcf_cookie, &error);
    free(pcf_reply);
    if (error) {
        KERROR("failed to set XCB per-client flags, not using detectable repeat. error code : %u", error->major_code);
        return false;
    }

    // Now let's grab the keysyms we will use later
    state_ptr->syms = xcb_key_symbols_alloc(state_ptr->connection);

    // Get data from the X server
    const xcb_setup_t* setup = xcb_get_setup(state_ptr->connection);

    // Loop through screens
    state_ptr->screen = xcb_setup_roots_iterator(setup).data;

    // Allocate a XID for the window to be created.
    state_ptr->window = xcb_generate_id(state_ptr->connection);

    // Register event types.
    // XCB_CW_BACK_PIXEL = filling then window bg with a single colour
    // XCB_CW_EVENT_MASK is required.
    u32 event_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

    // Listen for keyboard and mouse buttons
    u32 event_values = XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                       XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                       XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_POINTER_MOTION |
                       XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    // Values to be sent over XCB (bg colour, events)
    u32 value_list[] = {state_ptr->screen->black_pixel, event_values};

    // Create the window
    xcb_void_cookie_t cookie = xcb_create_window(
        state_ptr->connection,
        XCB_COPY_FROM_PARENT,  // depth
        state_ptr->window,
        state_ptr->screen->root,        // parent
        x,                              //x
        y,                              //y
        width,                          //width
        height,                         //height
        0,                              // No border
        XCB_WINDOW_CLASS_INPUT_OUTPUT,  //class
        state_ptr->screen->root_visual,
        event_mask,
        value_list);

    // Change the title
    xcb_change_property(
        state_ptr->connection,
        XCB_PROP_MODE_REPLACE,
        state_ptr->window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,  // data should be viewed 8 bits at a time
        strlen(application_name),
        application_name);

    // Tell the server to notify when the window manager
    // attempts to destroy the window.
    xcb_intern_atom_cookie_t wm_delete_cookie = xcb_intern_atom(
        state_ptr->connection,
        0,
        strlen("WM_DELETE_WINDOW"),
        "WM_DELETE_WINDOW");
    xcb_intern_atom_cookie_t wm_protocols_cookie = xcb_intern_atom(
        state_ptr->connection,
        0,
        strlen("WM_PROTOCOLS"),
        "WM_PROTOCOLS");
    xcb_intern_atom_reply_t* wm_delete_reply = xcb_intern_atom_reply(
        state_ptr->connection,
        wm_delete_cookie,
        NULL);
    xcb_intern_atom_reply_t* wm_protocols_reply = xcb_intern_atom_reply(
        state_ptr->connection,
        wm_protocols_cookie,
        NULL);
    state_ptr->wm_delete_win = wm_delete_reply->atom;
    state_ptr->wm_protocols = wm_protocols_reply->atom;

    xcb_change_property(
        state_ptr->connection,
        XCB_PROP_MODE_REPLACE,
        state_ptr->window,
        wm_protocols_reply->atom,
        4,
        32,
        1,
        &wm_delete_reply->atom);

    // Map the window to the screen
    xcb_map_window(state_ptr->connection, state_ptr->window);

    // Flush the stream
    i32 stream_result = xcb_flush(state_ptr->connection);
    if (stream_result <= 0) {
        KFATAL("An error occurred when flusing the stream: %d", stream_result);
        return false;
    }

    return true;
}

void platform_system_shutdown(void* plat_state) {
    if (state_ptr) {
        xcb_destroy_window(state_ptr->connection, state_ptr->window);
        xcb_key_symbols_free(state_ptr->syms);
        xcb_disconnect(state_ptr->connection);
    }
}

b8 internal_pool_for_event(xcb_generic_event_t **event);

b8 platform_pump_messages() {
    if (state_ptr) {
        xcb_generic_event_t* event;
        xcb_client_message_event_t* cm;

        b8 quit_flagged = false;

        // Poll for events until null is returned.
        while (internal_pool_for_event(&event)) {
            // Input events
            switch (event->response_type & ~0x80) {
                // while we could group PRESS and RELEASE together it's clearer to separate them
                case XCB_KEY_PRESS: {
                    xcb_key_press_event_t *key_event = (xcb_key_press_event_t*)event;
                    xcb_keysym_t key_sym = xcb_key_symbols_get_keysym(state_ptr->syms, key_event->detail,0);
                    input_process_key(translate_keycode(key_sym), true);
                } break;
                case XCB_KEY_RELEASE: {
                    xcb_key_release_event_t *key_event = (xcb_key_release_event_t *)event;
                    xcb_keysym_t key_sym = xcb_key_symbols_get_keysym(state_ptr->syms, key_event->detail,0);
                    input_process_key(translate_keycode(key_sym), false);
                } break;
                // we need to separate PRESS and RELEASE to handle the wheel event
                case XCB_BUTTON_PRESS: {
                    xcb_button_press_event_t *button_event = (xcb_button_press_event_t*)event;
                    // the wheel event is mapped to button 4 and 5
                    // 4 is down, while 5 is up
                    if (button_event->detail > 3) {
                        input_process_mouse_wheel(button_event->detail == 4 ? -1 : 1 );
                    } else {
                        input_process_button(button_event->detail, true);
                    }
                } break;
                case XCB_BUTTON_RELEASE: {
                    xcb_button_release_event_t *button_event = (xcb_button_release_event_t*)event;
                    input_process_button(button_event->detail, false);
                } break;
                case XCB_MOTION_NOTIFY: {
                    // Mouse move
                    xcb_motion_notify_event_t* move_event = (xcb_motion_notify_event_t*)event;

                    // Pass over to the input subsystem.
                    input_process_mouse_move(move_event->event_x, move_event->event_y);
                } break;
                case XCB_CONFIGURE_NOTIFY: {
                    // Resizing - note that this is also triggered by moving the window, but should be
                    // passed anyway since a change in the x/y could mean an upper-left resize.
                    // The application layer can decide what to do with this.
                    xcb_configure_notify_event_t* configure_event = (xcb_configure_notify_event_t*)event;

                    // Fire the event. The application layer should pick this up, but not handle it
                    // as it shouldn be visible to other parts of the application.
                    event_context context;
                    context.data.u16[0] = configure_event->width;
                    context.data.u16[1] = configure_event->height;
                    event_fire(EVENT_CODE_RESIZED, 0, context);

                } break;

                case XCB_CLIENT_MESSAGE: {
                    cm = (xcb_client_message_event_t*)event;

                    // Window close
                    if (cm->data.data32[0] == state_ptr->wm_delete_win) {
                        quit_flagged = true;
                    }
                } break;
                case XCB_MAP_NOTIFY: {
                    // The window just got raised
                    event_context context;
                    context.data.u16[0] = 1;
                    context.data.u16[1] = 1;
                    event_fire(EVENT_CODE_RAISED, 0, context);
                } break;
                case XCB_UNMAP_NOTIFY: {
                    // The window just got minimized
                    event_context context;
                    context.data.u16[0] = 0;
                    context.data.u16[1] = 0;
                    event_fire(EVENT_CODE_MINIZED, 0, context);
                } break;
                default:
                    // Something else
                    break;
            }

            free(event);
        }
        return !quit_flagged;
    }
    return true;
}

void* platform_allocate(u64 size, b8 aligned) {
    return malloc(size);
}
void platform_free(void* block, b8 aligned) {
    free(block);
}
void* platform_zero_memory(void* block, u64 size) {
    return memset(block, 0, size);
}
void* platform_copy_memory(void* dest, const void* source, u64 size) {
    return memcpy(dest, source, size);
}
void* platform_set_memory(void* dest, i32 value, u64 size) {
    return memset(dest, value, size);
}

void platform_console_write(const char* message, u8 colour) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}
void platform_console_write_error(const char* message, u8 colour) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}

f64 platform_get_absolute_time() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec * 0.000000001;
}

void platform_sleep(u64 ms) {
#if _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000 * 1000;
    nanosleep(&ts, 0);
#else
    if (ms >= 1000) {
        sleep(ms / 1000);
    }
    usleep((ms % 1000) * 1000);
#endif
}

void platform_get_required_extension_names(const char*** names_darray) {
    darray_push(*names_darray, &"VK_KHR_xcb_surface");  // VK_KHR_xlib_surface?
}

// Surface creation for Vulkan
b8 platform_create_vulkan_surface(vulkan_context* context) {
    if(!state_ptr) {
        return false;
    }

    VkXcbSurfaceCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR};
    create_info.connection = state_ptr->connection;
    create_info.window = state_ptr->window;

    VkResult result = vkCreateXcbSurfaceKHR(
        context->instance,
        &create_info,
        context->allocator,
        &state_ptr->surface);
    if (result != VK_SUCCESS) {
        KFATAL("Vulkan surface creation failed.");
        return false;
    }

    context->surface = state_ptr->surface;
    return true;
}

b8 internal_pool_for_event(xcb_generic_event_t **event) {
    if (state_ptr) {
        *event = xcb_poll_for_event(state_ptr->connection);
    }
    return (*event != NULL);
}

keys translate_keycode(u32 x_keycode) {
    xcb_keysym_t upper = x_keycode;
    if ((x_keycode >> 8) == 0) {
        if (x_keycode >= 0x0061 && x_keycode <= 0x007a) {
            upper -= (0x0061 - 0x0041);
        }
    }

    return upper;
}

#endif
