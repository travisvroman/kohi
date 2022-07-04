#include "platform.h"

// Linux platform layer.
#if KPLATFORM_LINUX

#include "core/logger.h"
#include "core/event.h"
#include "core/input.h"

#include "containers/darray.h"

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xkb.h>
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

#define MAX_KEY_LOOKUP 232

static const u32 key_lookup_table[MAX_KEY_LOOKUP] = {
        0xff08, KEY_BACKSPACE,
        0xff0d, KEY_ENTER,
        0xff09, KEY_TAB,
        0xff13, KEY_PAUSE,
        0xffe5, KEY_CAPITAL,
        0xff1b, KEY_ESCAPE,
        0xff7e, KEY_MODECHANGE,
        0x0020, KEY_SPACE,
        0xff55, KEY_PRIOR,
        0xff56, KEY_NEXT,
        0xff57, KEY_END,
        0xff50, KEY_HOME,
        0xff51, KEY_LEFT,
        0xff52, KEY_UP,
        0xff53, KEY_RIGHT,
        0xff54, KEY_DOWN,
        0xff60, KEY_SELECT,
        0xff61, KEY_PRINT,
        0xff62, KEY_EXECUTE,
        0xff63, KEY_INSERT,
        0xffff, KEY_DELETE,
        0xff6a, KEY_HELP,

        0xffeb, KEY_LWIN,  // TODO: not sure this is right
        0xffec, KEY_RWIN,
        0xff9e, KEY_NUMPAD0,
        0xff9c, KEY_NUMPAD1,
        0xff99, KEY_NUMPAD2,
        0xff9b, KEY_NUMPAD3,
        0xff96, KEY_NUMPAD4,
        0xff9d, KEY_NUMPAD5,
        0xff98, KEY_NUMPAD6,
        0xff95, KEY_NUMPAD7,
        0xff97, KEY_NUMPAD8,
        0xff9a, KEY_NUMPAD9,
        0xffaa, KEY_MULTIPLY,
        0xffab, KEY_ADD,
        0xffac, KEY_SEPARATOR,
        0xffad, KEY_SUBTRACT,
        0xff9f, KEY_DECIMAL,
        0xffaf, KEY_DIVIDE,
        0xffbe, KEY_F1,
        0xffbf, KEY_F2,
        0xffc0, KEY_F3,
        0xffc1, KEY_F4,
        0xffc2, KEY_F5,
        0xffc3, KEY_F6,
        0xffc4, KEY_F7,
        0xffc5, KEY_F8,
        0xffc6, KEY_F9,
        0xffc7, KEY_F10,
        0xffc8, KEY_F11,
        0xffc9, KEY_F12,
        0xffca, KEY_F13,
        0xffcb, KEY_F14,
        0xffcc, KEY_F15,
        0xffcd, KEY_F16,
        0xffce, KEY_F17,
        0xffcf, KEY_F18,
        0xffd0, KEY_F19,
        0xffd1, KEY_F20,
        0xffd2, KEY_F21,
        0xffd3, KEY_F22,
        0xffd4, KEY_F23,
        0xffd5, KEY_F24,

        0xff7f, KEY_NUMLOCK,
        0xff14, KEY_SCROLL,

        0xffbd, KEY_NUMPAD_EQUAL,

        0xffe1, KEY_LSHIFT,
        0xffe2, KEY_RSHIFT,
        0xffe3, KEY_LCONTROL,
        0xffe4, KEY_RCONTROL,
        0xffe9, KEY_LALT,
        0xfe03, KEY_RALT,

        0x003b, KEY_SEMICOLON,
        0x002b, KEY_PLUS,
        0x002c, KEY_COMMA,
        0x002d, KEY_MINUS,
        0x002e, KEY_PERIOD,
        0x002f, KEY_SLASH,
        0x0060, KEY_GRAVE,

        0x0030, KEY_0,
        0x0031, KEY_1,
        0x0032, KEY_2,
        0x0033, KEY_3,
        0x0034, KEY_4,
        0x0035, KEY_5,
        0x0036, KEY_6,
        0x0037, KEY_7,
        0x0038, KEY_8,
        0x0039, KEY_9,

        0x0041, KEY_A,
        0x0042, KEY_B,
        0x0043, KEY_C,
        0x0044, KEY_D,
        0x0045, KEY_E,
        0x0046, KEY_F,
        0x0047, KEY_G,
        0x0048, KEY_H,
        0x0049, KEY_I,
        0x004a, KEY_J,
        0x004b, KEY_K,
        0x004c, KEY_L,
        0x004d, KEY_M,
        0x004e, KEY_N,
        0x004f, KEY_O,
        0x0050, KEY_P,
        0x0051, KEY_Q,
        0x0052, KEY_R,
        0x0053, KEY_S,
        0x0054, KEY_T,
        0x0055, KEY_U,
        0x0056, KEY_V,
        0x0057, KEY_W,
        0x0058, KEY_X,
        0x0059, KEY_Y,
        0x005a, KEY_Z
};

typedef struct platform_state {
    xcb_connection_t* connection;
    xcb_window_t window;
    xcb_screen_t* screen;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_win;
    xcb_key_symbols_t  *syms;
    VkSurfaceKHR surface;
} platform_state;

static platform_state* state_ptr;

b8 internal_poll_for_event(xcb_generic_event_t **event);
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

    // We get the connection through xcb
    int screenp = 0;
    state_ptr->connection = xcb_connect(0, &screenp);
    if (xcb_connection_has_error(state_ptr->connection)) {
        KFATAL("Failed to connect to X server via XCB.");
        return false;
    }

    // Unlike most reply_t this one must not be freed.
    const xcb_query_extension_reply_t  *ext_reply = xcb_get_extension_data(state_ptr->connection, &xcb_xkb_id);
    if (!ext_reply) {
        KFATAL("XKB extension not available on host X11 server.");
        return false;
    }

    // We can now load xcb's extensions (xkb)
    xcb_generic_error_t *error;
    xcb_xkb_use_extension_cookie_t use_ext_cookie;
    xcb_xkb_use_extension_reply_t *use_ext_reply;
    use_ext_cookie = xcb_xkb_use_extension(state_ptr->connection, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
    use_ext_reply = xcb_xkb_use_extension_reply(state_ptr->connection, use_ext_cookie, &error);
    if (!use_ext_reply) {
        KFATAL("Couldn't load the xcb-xkb extension.");
        free(use_ext_reply);
        return false;
    }
    if (!use_ext_reply->supported) {
        KFATAL("The XKB extension is not supported on this X server.");
        free(use_ext_reply);
        return false;
    }
    free(use_ext_reply);

    // We can now deactivate repeat for this app only without affecting the system
    xcb_xkb_per_client_flags_cookie_t pcf_cookie;
    xcb_xkb_per_client_flags_reply_t *pcf_reply;
    pcf_cookie = xcb_xkb_per_client_flags(
            state_ptr->connection,
            XCB_XKB_ID_USE_CORE_KBD,
            XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
            XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
            0, 0, 0);
    pcf_reply = xcb_xkb_per_client_flags_reply(state_ptr->connection, pcf_cookie, &error);
    free(pcf_reply);
    if (error) {
        KERROR("Failed to set XKB per-client flags, not using detectable repeat. error code: %u", error->major_code);
        return false;
    }

    // Now let's grab the keysyms we will use later
    state_ptr->syms = xcb_key_symbols_alloc(state_ptr->connection);

    // Get data from the X server
    const struct xcb_setup_t* setup = xcb_get_setup(state_ptr->connection);

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
    xcb_create_window(
        state_ptr->connection,
        XCB_COPY_FROM_PARENT,  // depth
        state_ptr->window,
        state_ptr->screen->root,        // parent
        (i16)x,                              //x
        (i16)y,                              //y
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
    xcb_intern_atom_cookie_t wm_cookie = xcb_intern_atom(
        state_ptr->connection,
        0,
        strlen("WM_DELETE_WINDOW"),
        "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t* wm_reply = xcb_intern_atom_reply(
        state_ptr->connection,
        wm_cookie,
        NULL);
    state_ptr->wm_delete_win = wm_reply->atom;
    free(wm_reply);
    wm_cookie = xcb_intern_atom(
        state_ptr->connection,
        0,
        strlen("WM_PROTOCOLS"),
        "WM_PROTOCOLS");
    wm_reply = xcb_intern_atom_reply(
        state_ptr->connection,
        wm_cookie,
        NULL);
    state_ptr->wm_protocols = wm_reply->atom;
    free(wm_reply);

    xcb_change_property(
        state_ptr->connection,
        XCB_PROP_MODE_REPLACE,
        state_ptr->window,
        state_ptr->wm_protocols,
        4,
        32,
        1,
        &state_ptr->wm_delete_win);

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

b8 platform_pump_messages() {
    if (state_ptr) {
        xcb_generic_event_t* event;
        xcb_client_message_event_t* cm;

        b8 quit_flagged = false;

        // Poll for events until false is returned.
        while (internal_poll_for_event(&event)) {
            // Input events
            switch (event->response_type & ~0x80) {
                case XCB_KEY_PRESS: {
                    xcb_key_press_event_t *key_event = (xcb_key_press_event_t *)event;
                    xcb_keysym_t key_sym = xcb_key_symbols_get_keysym(state_ptr->syms, key_event->detail, 0);
                    input_process_key(translate_keycode(key_sym), true);
                } break;
                case XCB_KEY_RELEASE: {
                    xcb_key_release_event_t *key_event = (xcb_key_release_event_t *)event;
                    xcb_keysym_t key_sym = xcb_key_symbols_get_keysym(state_ptr->syms, key_event->detail, 0);
                    input_process_key(translate_keycode(key_sym), false);
                } break;
                // we need to separate the PRESS and RELEASE events to handle the WHEEL event
                case XCB_BUTTON_PRESS: {
                    xcb_button_press_event_t *button_event = (xcb_button_press_event_t *)event;
                    // the wheel event is mapped to button 4 and 5
                    // 4 is down, while 5 is up
                    if (button_event->detail > 3) {
                        input_process_mouse_wheel(button_event->detail == 4 ? -1 : 1);
                    } else {
                        input_process_button(button_event->detail, true);
                    }
                }
                case XCB_BUTTON_RELEASE: {
                    xcb_button_press_event_t *button_event = (xcb_button_press_event_t *)event;
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

b8 internal_poll_for_event(xcb_generic_event_t **event) {
    if (state_ptr) {
        *event = xcb_poll_for_event(state_ptr->connection);
    }

    return (*event != NULL);
}

// Key translation
keys translate_keycode(u32 x_keycode) {
    xcb_keysym_t upper = x_keycode;
    if ((x_keycode >> 8) == 0) {
        if (x_keycode >= 0x0061 && x_keycode <= 0x007a) {
            upper -= (0x0061 - 0x0041);
        }
    }
    for (u32 i = 0; i < MAX_KEY_LOOKUP; ++i) {
        if (key_lookup_table[i] == upper) {
            return key_lookup_table[i + 1];
        }
    }

    return KEYS_MAX_KEYS;
}

#endif