

#include "platform.h"

// Linux platform layer.
#if KPLATFORM_LINUX
// #include <X11/extensions/Xrender.h>
// #include <xcb/xproto.h>

#include "math/kmath.h"

#ifndef XRANDR_ROTATION_LEFT
#define XRANDR_ROTATION_LEFT (1 << 1)
#endif
#ifndef XRANDR_ROTATION_RIGHT
#define XRANDR_ROTATION_RIGHT (1 << 9)
#endif

#include <X11/XKBlib.h>    // sudo apt-get install libx11-dev
#include <X11/Xlib-xcb.h>  // sudo apt-get install libxkbcommon-x11-dev libx11-xcb-dev
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <bits/time.h>

// #include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>
#include <sys/time.h>
#include <xcb/xcb.h>

#include "containers/darray.h"
#include "core/asserts.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kmemory.h"
#include "core/kmutex.h"
#include "core/kstring.h"
#include "core/kthread.h"
#include "core/logger.h"

#if _POSIX_C_SOURCE >= 199309L
#include <time.h>  // nanosleep
#endif

#include <errno.h>  // For error reporting
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>  // Processor info
#include <unistd.h>

typedef struct linux_handle_info {
    xcb_connection_t* connection;
    xcb_window_t window;
} linux_handle_info;

typedef struct linux_file_watch {
    u32 id;
    const char* file_path;
    long last_write_time;
} linux_file_watch;

typedef struct platform_state {
    Display* display;
    linux_handle_info handle;
    xcb_screen_t* screen;
    xcb_atom_t wm_protocols;
    xcb_atom_t wm_delete_win;
    i32 screen_count;
    // darray
    linux_file_watch* watches;
    f32 device_pixel_ratio;
} platform_state;

static platform_state* state_ptr;

static void platform_update_watches(void);
// Key translation
static keys translate_keycode(u32 x_keycode);

b8 platform_system_startup(u64* memory_requirement, void* state, void* config) {
    platform_system_config* typed_config = (platform_system_config*)config;
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }

    state_ptr = state;
    state_ptr->device_pixel_ratio = 1.0f;

    // Connect to X
    state_ptr->display = XOpenDisplay(NULL);

    // Retrieve the connection from the display.
    state_ptr->handle.connection = XGetXCBConnection(state_ptr->display);

    if (xcb_connection_has_error(state_ptr->handle.connection)) {
        KFATAL("Failed to connect to X server via XCB.");
        return false;
    }

    // Get data from the X server
    const struct xcb_setup_t* setup = xcb_get_setup(state_ptr->handle.connection);

    // Loop through screens using iterator
    xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
    for (i32 s = 0; s < state_ptr->screen_count; ++s) {
        /* f32 w_inches = it.data->width_in_millimeters * 0.0394;
        f32 h_inches = it.data->height_in_millimeters * 0.0394;
        f32 x_dpi = (f32)it.data->width_in_pixels / w_inches;

        KINFO("Monitor '%s' has a DPI of %.2f for a device pixelratio of %0.2f", it.index, x_dpi, x_dpi / 96.0f);
        // state_ptr->device_pixel_ratio = x_dpi / 96.0f;  // Default DPI is considered 96. */

        xcb_screen_next(&it);
    }

    // After screens have been looped through, assign it.
    state_ptr->screen = it.data;

    // Allocate a XID for the window to be created.
    state_ptr->handle.window = xcb_generate_id(state_ptr->handle.connection);

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
        state_ptr->handle.connection,
        XCB_COPY_FROM_PARENT,  // depth
        state_ptr->handle.window,
        state_ptr->screen->root,        // parent
        typed_config->x,                // x
        typed_config->y,                // y
        typed_config->width,            // width
        typed_config->height,           // height
        0,                              // No border
        XCB_WINDOW_CLASS_INPUT_OUTPUT,  // class
        state_ptr->screen->root_visual,
        event_mask,
        value_list);

    // NOTE: After much research and effort, it seems as though there is not a good, reliable, global solution
    // to determine device pixel ratio using X, _in particular_ when using mixed HiDPI and normal DPI monitors.
    // The commented code below _would_ work if the values reported by op_info->mm_width and op_info->mm_height
    // were actually correct (they aren't, and the "solution" is to have the user manually set this in config
    // files). To compound this issue, X treats the whole thing as one large "screen", and the DPI on _that_ isn't
    // accurate either. For example, on a setup that has one known 96 DPI monitor and one known 192 DPI monitor,
    // this reports... 144. Wrong on both accounts. This is supported on Wayland, supposedly, so if a Wayland
    // backend ever gets added it'll be supported there. It's just not worth attempting on X11.
    /*
    // Get monitor info
    XRRMonitorInfo* monitors = XRRGetMonitors(state_ptr->display, state_ptr->handle.window, true, &state_ptr->screen_count);
    for (u32 i = 0; i < state_ptr->screen_count; ++i) {
        if (monitors[i].noutput > 0) {
            // XRRScreenResources* current_resources = XRRGetScreenResourcesCurrent(state_ptr->display, state_ptr->handle.window);
            XRRScreenResources* resources = XRRGetScreenResources(state_ptr->display, state_ptr->handle.window);
            for (u32 o = 0; o < monitors[i].noutput; ++o) {
                RROutput op = monitors[i].outputs[o];
                XRROutputInfo* op_info = XRRGetOutputInfo(state_ptr->display, resources, op);
                if (op_info) {
                    XRRCrtcInfo* crtc_info = XRRGetCrtcInfo(state_ptr->display, resources, op_info->crtc);
                    if (crtc_info) {
                        // find the mode
                        for (i32 m = 0; m < resources->nmode; ++m) {
                            const XRRModeInfo* mode_info = &resources->modes[m];
                            if (mode_info->id == crtc_info->mode) {
                                //
                                XFixed scale_w = 0x10000, scale_h = 0x10000;
                                XRRCrtcTransformAttributes* attr = 0;

                                if (XRRGetCrtcTransform(state_ptr->display, op_info->crtc, &attr) && attr) {
                                    scale_w = attr->currentTransform.matrix[0][0];
                                    scale_h = attr->currentTransform.matrix[1][1];

                                    f32 scale;
                                    if (attr->currentTransform.matrix[0][0] == attr->currentTransform.matrix[1][1]) {
                                        scale = XFixedToDouble(attr->currentTransform.matrix[0][0]);
                                    } else {
                                        scale = XFixedToDouble(attr->currentTransform.matrix[0][0] + attr->currentTransform.matrix[0][0]);
                                    }

                                    // scale = 1.0f / scale;
                                    KTRACE("Scale before: %.2f", scale);
                                    scale = range_convert_f32(scale, 0.0f, 1.0f, 1.0f, 2.0f);
                                    KTRACE("Scale after: %.2f", scale);

                                    // If rotated, flip actual w/h
                                    i32 actual_w, actual_h;
                                    f32 w_inches, h_inches;
                                    if (crtc_info->rotation & (XRANDR_ROTATION_LEFT | XRANDR_ROTATION_RIGHT)) {
                                        actual_w = mode_info->height;
                                        actual_h = mode_info->width;
                                        w_inches = op_info->mm_height * 0.0394f;
                                        h_inches = op_info->mm_width * 0.0394f;
                                    } else {
                                        actual_w = mode_info->width;
                                        actual_h = mode_info->height;
                                        w_inches = op_info->mm_width * 0.0394f;
                                        h_inches = op_info->mm_height * 0.0394f;
                                    }
                                    f32 dpi_x = (f32)actual_w / w_inches;
                                    f32 dpi_y = (f32)actual_h / h_inches;

                                    KTRACE("device_pixel_ratio x/y: %.2f, %.2f", dpi_x / 96.0f, dpi_y / 96.0f);

                                    KTRACE("Scale x/y: %i/%i, actual w/h: %i/%i", scale_w >> 16, scale_h >> 16, actual_w, actual_h);
                                    XFree(attr);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    const char* resource_string = XResourceManagerString(state_ptr->display);
    XrmDatabase db;
    XrmValue value;
    char* type = 0;
    f32 dpi = 0.0f;

    XrmInitialize();
    db = XrmGetStringDatabase(resource_string);
    if (resource_string) {
        KTRACE("Entire DB: '%s'", resource_string);
        if (XrmGetResource(db, "Xft.dpi", "String", &type, &value) == true) {
            if (value.addr) {
                if (!string_to_f32(value.addr, &dpi)) {
                    KERROR("Unable to parse DPI from Xft.dpi");
                }
            }
        }
    }
    */

    // Change the title
    xcb_change_property(
        state_ptr->handle.connection,
        XCB_PROP_MODE_REPLACE,
        state_ptr->handle.window,
        XCB_ATOM_WM_NAME,
        XCB_ATOM_STRING,
        8,  // data should be viewed 8 bits at a time
        strlen(typed_config->application_name),
        typed_config->application_name);

    // Tell the server to notify when the window manager
    // attempts to destroy the window.
    xcb_intern_atom_cookie_t wm_delete_cookie = xcb_intern_atom(
        state_ptr->handle.connection,
        0,
        strlen("WM_DELETE_WINDOW"),
        "WM_DELETE_WINDOW");
    xcb_intern_atom_cookie_t wm_protocols_cookie = xcb_intern_atom(
        state_ptr->handle.connection,
        0,
        strlen("WM_PROTOCOLS"),
        "WM_PROTOCOLS");
    xcb_intern_atom_reply_t* wm_delete_reply = xcb_intern_atom_reply(
        state_ptr->handle.connection,
        wm_delete_cookie,
        NULL);
    xcb_intern_atom_reply_t* wm_protocols_reply = xcb_intern_atom_reply(
        state_ptr->handle.connection,
        wm_protocols_cookie,
        NULL);
    state_ptr->wm_delete_win = wm_delete_reply->atom;
    state_ptr->wm_protocols = wm_protocols_reply->atom;

    xcb_change_property(
        state_ptr->handle.connection,
        XCB_PROP_MODE_REPLACE,
        state_ptr->handle.window,
        wm_protocols_reply->atom,
        4,
        32,
        1,
        &wm_delete_reply->atom);

    // Map the window to the screen
    xcb_map_window(state_ptr->handle.connection, state_ptr->handle.window);

    // Flush the stream
    i32 stream_result = xcb_flush(state_ptr->handle.connection);
    if (stream_result <= 0) {
        KFATAL("An error occurred when flusing the stream: %d", stream_result);
        return false;
    }

    return true;
}

void platform_system_shutdown(void* plat_state) {
    if (state_ptr) {
        xcb_destroy_window(state_ptr->handle.connection, state_ptr->handle.window);
    }
}

b8 platform_pump_messages(void) {
    if (state_ptr) {
        xcb_generic_event_t* event;
        xcb_client_message_event_t* cm;

        b8 quit_flagged = false;

        // Poll for events until null is returned.
        while ((event = xcb_poll_for_event(state_ptr->handle.connection))) {
            // Input events
            switch (event->response_type & ~0x80) {
                case XCB_KEY_PRESS:
                case XCB_KEY_RELEASE: {
                    // Key press event - xcb_key_press_event_t and xcb_key_release_event_t are the same
                    xcb_key_press_event_t* kb_event = (xcb_key_press_event_t*)event;
                    b8 pressed = event->response_type == XCB_KEY_PRESS;
                    xcb_keycode_t code = kb_event->detail;
                    KeySym key_sym = XkbKeycodeToKeysym(
                        state_ptr->display,
                        (KeyCode)code,  // event.xkey.keycode,
                        0,
                        0 /*code & ShiftMask ? 1 : 0*/);

                    keys key = translate_keycode(key_sym);

                    // Pass to the input subsystem for processing.
                    input_process_key(key, pressed);
                } break;
                case XCB_BUTTON_PRESS:
                case XCB_BUTTON_RELEASE: {
                    xcb_button_press_event_t* mouse_event = (xcb_button_press_event_t*)event;
                    b8 pressed = event->response_type == XCB_BUTTON_PRESS;
                    buttons mouse_button = BUTTON_MAX_BUTTONS;
                    switch (mouse_event->detail) {
                        case XCB_BUTTON_INDEX_1:
                            mouse_button = BUTTON_LEFT;
                            break;
                        case XCB_BUTTON_INDEX_2:
                            mouse_button = BUTTON_MIDDLE;
                            break;
                        case XCB_BUTTON_INDEX_3:
                            mouse_button = BUTTON_RIGHT;
                            break;
                    }

                    // Pass over to the input subsystem.
                    if (mouse_button != BUTTON_MAX_BUTTONS) {
                        input_process_button(mouse_button, pressed);
                    }
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

        // Update watches.
        platform_update_watches();

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
    fprintf(stderr, "\033[%sm%s\033[0m", colour_strings[colour], message);
}

f64 platform_get_absolute_time(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
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

i32 platform_get_processor_count(void) {
    // Load processor info.
    i32 processor_count = get_nprocs_conf();
    i32 processors_available = get_nprocs();
    KINFO("%i processor cores detected, %i cores available.", processor_count, processors_available);
    return processors_available;
}

void platform_get_handle_info(u64* out_size, void* memory) {
    *out_size = sizeof(linux_handle_info);
    if (!memory) {
        return;
    }

    kcopy_memory(memory, &state_ptr->handle, *out_size);
}

// NOTE: Begin threads.

b8 kthread_create(pfn_thread_start start_function_ptr, void* params, b8 auto_detach, kthread* out_thread) {
    if (!start_function_ptr) {
        return false;
    }

    // pthread_create uses a function pointer that returns void*, so cold-cast to this type.
    i32 result = pthread_create((pthread_t*)&out_thread->thread_id, 0, (void* (*)(void*))start_function_ptr, params);
    if (result != 0) {
        switch (result) {
            case EAGAIN:
                KERROR("Failed to create thread: insufficient resources to create another thread.");
                return false;
            case EINVAL:
                KERROR("Failed to create thread: invalid settings were passed in attributes..");
                return false;
            default:
                KERROR("Failed to create thread: an unhandled error has occurred. errno=%i", result);
                return false;
        }
    }
    KDEBUG("Starting process on thread id: %#x", out_thread->thread_id);

    // Only save off the handle if not auto-detaching.
    if (!auto_detach) {
        out_thread->internal_data = platform_allocate(sizeof(u64), false);
        *(u64*)out_thread->internal_data = out_thread->thread_id;
    } else {
        // If immediately detaching, make sure the operation is a success.
        result = pthread_detach(out_thread->thread_id);
        if (result != 0) {
            switch (result) {
                case EINVAL:
                    KERROR("Failed to detach newly-created thread: thread is not a joinable thread.");
                    return false;
                case ESRCH:
                    KERROR("Failed to detach newly-created thread: no thread with the id %#x could be found.", out_thread->thread_id);
                    return false;
                default:
                    KERROR("Failed to detach newly-created thread: an unknown error has occurred. errno=%i", result);
                    return false;
            }
        }
    }

    return true;
}

void kthread_destroy(kthread* thread) {
    kthread_cancel(thread);
}

void kthread_detach(kthread* thread) {
    if (thread->internal_data) {
        i32 result = pthread_detach(*(pthread_t*)thread->internal_data);
        if (result != 0) {
            switch (result) {
                case EINVAL:
                    KERROR("Failed to detach thread: thread is not a joinable thread.");
                    break;
                case ESRCH:
                    KERROR("Failed to detach thread: no thread with the id %#x could be found.", thread->thread_id);
                    break;
                default:
                    KERROR("Failed to detach thread: an unknown error has occurred. errno=%i", result);
                    break;
            }
        }
        platform_free(thread->internal_data, false);
        thread->internal_data = 0;
    }
}

void kthread_cancel(kthread* thread) {
    if (thread->internal_data) {
        i32 result = pthread_cancel(*(pthread_t*)thread->internal_data);
        if (result != 0) {
            switch (result) {
                case ESRCH:
                    KERROR("Failed to cancel thread: no thread with the id %#x could be found.", thread->thread_id);
                    break;
                default:
                    KERROR("Failed to cancel thread: an unknown error has occurred. errno=%i", result);
                    break;
            }
        }
        platform_free(thread->internal_data, false);
        thread->internal_data = 0;
        thread->thread_id = 0;
    }
}

b8 kthread_is_active(kthread* thread) {
    // TODO: Find a better way to verify this.
    return thread->internal_data != 0;
}

void kthread_sleep(kthread* thread, u64 ms) {
    platform_sleep(ms);
}

b8 kthread_wait(kthread* thread) {
    if (thread && thread->internal_data) {
        i32 result = pthread_join(*(pthread_t*)thread->internal_data, 0);
        if (result == 0) {
            return true;
        }
    }
    return false;
}

b8 kthread_wait_timeout(kthread* thread, u64 wait_ms) {
    if (thread && thread->internal_data) {
        KWARN("kthread_wait_timeout - timeout not supported on this platform.");
        // LEFTOFF: Need a wait/notify loop to support timeout.
        i32 result = pthread_join(*(pthread_t*)thread->internal_data, 0);
        if (result == 0) {
            return true;
        }
    }
    return false;
}

u64 platform_current_thread_id(void) {
    return (u64)pthread_self();
}
// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create(kmutex* out_mutex) {
    if (!out_mutex) {
        return false;
    }

    // Initialize
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_t mutex;
    i32 result = pthread_mutex_init(&mutex, &mutex_attr);
    if (result != 0) {
        KERROR("Mutex creation failure!");
        return false;
    }

    // Save off the mutex handle.
    out_mutex->internal_data = platform_allocate(sizeof(pthread_mutex_t), false);
    *(pthread_mutex_t*)out_mutex->internal_data = mutex;

    return true;
}

void kmutex_destroy(kmutex* mutex) {
    if (mutex) {
        i32 result = pthread_mutex_destroy((pthread_mutex_t*)mutex->internal_data);
        switch (result) {
            case 0:
                // KTRACE("Mutex destroyed.");
                break;
            case EBUSY:
                KERROR("Unable to destroy mutex: mutex is locked or referenced.");
                break;
            case EINVAL:
                KERROR("Unable to destroy mutex: the value specified by mutex is invalid.");
                break;
            default:
                KERROR("An handled error has occurred while destroy a mutex: errno=%i", result);
                break;
        }

        platform_free(mutex->internal_data, false);
        mutex->internal_data = 0;
    }
}

b8 kmutex_lock(kmutex* mutex) {
    if (!mutex) {
        return false;
    }
    // Lock
    i32 result = pthread_mutex_lock((pthread_mutex_t*)mutex->internal_data);
    switch (result) {
        case 0:
            // Success, everything else is a failure.
            // KTRACE("Obtained mutex lock.");
            return true;
        case EOWNERDEAD:
            KERROR("Owning thread terminated while mutex still active.");
            return false;
        case EAGAIN:
            KERROR("Unable to obtain mutex lock: the maximum number of recursive mutex locks has been reached.");
            return false;
        case EBUSY:
            KERROR("Unable to obtain mutex lock: a mutex lock already exists.");
            return false;
        case EDEADLK:
            KERROR("Unable to obtain mutex lock: a mutex deadlock was detected.");
            return false;
        default:
            KERROR("An handled error has occurred while obtaining a mutex lock: errno=%i", result);
            return false;
    }
}

b8 kmutex_unlock(kmutex* mutex) {
    if (!mutex) {
        return false;
    }
    if (mutex->internal_data) {
        i32 result = pthread_mutex_unlock((pthread_mutex_t*)mutex->internal_data);
        switch (result) {
            case 0:
                // KTRACE("Freed mutex lock.");
                return true;
            case EOWNERDEAD:
                KERROR("Unable to unlock mutex: owning thread terminated while mutex still active.");
                return false;
            case EPERM:
                KERROR("Unable to unlock mutex: mutex not owned by current thread.");
                return false;
            default:
                KERROR("An handled error has occurred while unlocking a mutex lock: errno=%i", result);
                return false;
        }
    }

    return false;
}
// NOTE: End mutexes

const char* platform_dynamic_library_extension(void) {
    return ".so";
}

const char* platform_dynamic_library_prefix(void) {
    return "./lib";
}

platform_error_code platform_copy_file(const char* source, const char* dest, b8 overwrite_if_exists) {
    platform_error_code ret_code = PLATFORM_ERROR_SUCCESS;
    i32 source_fd = -1;
    i32 dest_fd = -1;

    // Obtain a file descriptor for the source file.
    source_fd = open(source, O_RDONLY);
    if (source_fd == -1) {
        if (errno == ENOENT) {
            KERROR("Source file does not exist: %s", source);
        }
        return PLATFORM_ERROR_FILE_NOT_FOUND;
    }

    // Stat the file to obtain it's attributes (e.g. size).
    struct stat source_stat;
    i32 result = fstat(source_fd, &source_stat);
    if (result != 0) {
        if (errno == ENOENT) {
            KERROR("Source file does not exist: %s", source);
        }
        ret_code = PLATFORM_ERROR_FILE_NOT_FOUND;
        goto close_handles;
    }

    u64 size = (u64)source_stat.st_size;

    // Obtain a file descriptor for the source file.
    dest_fd = open(dest, O_WRONLY | O_CREAT);
    if (dest_fd == -1) {
        if (errno == ENOENT) {
            KERROR("Destination file could not be created: %s", dest);
        }

        ret_code = PLATFORM_ERROR_FILE_LOCKED;
        goto close_handles;
    }

    // Copy the data. Iterate to handle large files, since Linux has a limit
    // on the amount that can be copied at once.
    while (size > 0) {
        ssize_t sent = sendfile(dest_fd, source_fd, NULL, (size >= SSIZE_MAX ? SSIZE_MAX : (size_t)size));
        if (sent < 0) {
            if (errno != EINVAL && errno != ENOSYS) {
                ret_code = PLATFORM_ERROR_UNKNOWN;
                goto close_handles;
            } else {
                break;
            }
        } else {
            KASSERT((size_t)sent <= size);
            size -= (size_t)sent;
        }
    }

    // Copy file times. Stat the source file again to make sure it's up to date.
    result = fstat(source_fd, &source_stat);
    if (result != 0) {
        ret_code = PLATFORM_ERROR_FILE_NOT_FOUND;
        goto close_handles;
    } else {
        struct timeval dest_times[2];
        // Update last access time.
        dest_times[0].tv_sec = source_stat.st_atime;
        dest_times[0].tv_usec = source_stat.st_atim.tv_nsec / 1000;
        // Update last modify time.
        dest_times[1].tv_sec = source_stat.st_mtime;
        dest_times[1].tv_usec = source_stat.st_mtim.tv_nsec / 1000;
        result = futimes(dest_fd, dest_times);
        // If an error is returned, treat as the destination file being locked.
        if (result != 0) {
            ret_code = PLATFORM_ERROR_FILE_LOCKED;
            goto close_handles;
        }
    }

    // Copy permissions.
    result = fchmod(dest_fd, source_stat.st_mode & (S_IRWXU | S_IRWXG | S_IRWXO));
    // If an error is returned, treat as the destination file being locked.
    if (result != 0) {
        ret_code = PLATFORM_ERROR_FILE_LOCKED;
        goto close_handles;
    }

close_handles:
    if (source_fd != -1) {
        result = close(source_fd);
        if (result != 0) {
            KERROR("Error closing source file: %s", source);
        }
    }
    if (dest_fd != -1) {
        result = close(dest_fd);
        if (result != 0) {
            KERROR("Error closing destination file: %s", source);
        }
    }

    return ret_code;
}

static b8 register_watch(const char* file_path, u32* out_watch_id) {
    if (!state_ptr || !file_path || !out_watch_id) {
        if (out_watch_id) {
            *out_watch_id = INVALID_ID;
        }
        return false;
    }
    *out_watch_id = INVALID_ID;

    if (!state_ptr->watches) {
        state_ptr->watches = darray_create(linux_file_watch);
    }

    struct stat info;
    int result = stat(file_path, &info);
    if (result != 0) {
        if (errno == ENOENT) {
            // File doesn't exist. TODO: report?
        }
        return false;
    }

    u32 count = darray_length(state_ptr->watches);
    for (u32 i = 0; i < count; ++i) {
        linux_file_watch* w = &state_ptr->watches[i];
        if (w->id == INVALID_ID) {
            // Found a free slot to use.
            w->id = i;
            w->file_path = string_duplicate(file_path);
            w->last_write_time = info.st_mtime;
            *out_watch_id = i;
            return true;
        }
    }

    // If no empty slot is available, create and push a new entry.
    linux_file_watch w = {0};
    w.id = count;
    w.file_path = string_duplicate(file_path);
    w.last_write_time = info.st_mtime;
    *out_watch_id = count;
    darray_push(state_ptr->watches, w);

    return true;
}

static b8 unregister_watch(u32 watch_id) {
    if (!state_ptr || !state_ptr->watches) {
        return false;
    }

    u32 count = darray_length(state_ptr->watches);
    if (count == 0 || watch_id > (count - 1)) {
        return false;
    }

    linux_file_watch* w = &state_ptr->watches[watch_id];
    w->id = INVALID_ID;
    u32 len = string_length(w->file_path);
    kfree((void*)w->file_path, sizeof(char) * (len + 1), MEMORY_TAG_STRING);
    w->file_path = 0;
    kzero_memory(&w->last_write_time, sizeof(long));

    return true;
}

b8 platform_watch_file(const char* file_path, u32* out_watch_id) {
    return register_watch(file_path, out_watch_id);
}

b8 platform_unwatch_file(u32 watch_id) {
    return unregister_watch(watch_id);
}

static void platform_update_watches(void) {
    if (!state_ptr || !state_ptr->watches) {
        return;
    }

    u32 count = darray_length(state_ptr->watches);
    for (u32 i = 0; i < count; ++i) {
        linux_file_watch* f = &state_ptr->watches[i];
        if (f->id != INVALID_ID) {
            struct stat info;
            int result = stat(f->file_path, &info);
            if (result != 0) {
                if (errno == ENOENT) {
                    // File doesn't exist. Which means it was deleted. Remove the watch.
                    event_context context = {0};
                    context.data.u32[0] = f->id;
                    event_fire(EVENT_CODE_WATCHED_FILE_DELETED, 0, context);
                    KINFO("File watch id %d has been removed.", f->id);
                    unregister_watch(f->id);
                    continue;
                } else {
                    KWARN("Some other error occurred on file watch id %d", f->id);
                }
                // NOTE: some other error has occurred. TODO: Handle?
                continue;
            }

            // Check the file time to see if it has been changed and update/notify if so.
            if (info.st_mtime - f->last_write_time != 0) {
                KTRACE("File update found.");
                f->last_write_time = info.st_mtime;
                // Notify listeners.
                event_context context = {0};
                context.data.u32[0] = f->id;
                event_fire(EVENT_CODE_WATCHED_FILE_WRITTEN, 0, context);
            }
        }
    }
}

// Key translation
static keys translate_keycode(u32 x_keycode) {
    switch (x_keycode) {
        case XK_BackSpace:
            return KEY_BACKSPACE;
        case XK_Return:
            return KEY_ENTER;
        case XK_Tab:
            return KEY_TAB;
            // case XK_Shift: return KEY_SHIFT;
            // case XK_Control: return KEY_CONTROL;

        case XK_Pause:
            return KEY_PAUSE;
        case XK_Caps_Lock:
            return KEY_CAPITAL;

        case XK_Escape:
            return KEY_ESCAPE;

            // Not supported
            // case : return KEY_CONVERT;
            // case : return KEY_NONCONVERT;
            // case : return KEY_ACCEPT;

        case XK_Mode_switch:
            return KEY_MODECHANGE;

        case XK_space:
            return KEY_SPACE;
        case XK_Prior:
            return KEY_PAGEUP;
        case XK_Next:
            return KEY_PAGEDOWN;
        case XK_End:
            return KEY_END;
        case XK_Home:
            return KEY_HOME;
        case XK_Left:
            return KEY_LEFT;
        case XK_Up:
            return KEY_UP;
        case XK_Right:
            return KEY_RIGHT;
        case XK_Down:
            return KEY_DOWN;
        case XK_Select:
            return KEY_SELECT;
        case XK_Print:
            return KEY_PRINT;
        case XK_Execute:
            return KEY_EXECUTE;
        // case XK_snapshot: return KEY_SNAPSHOT; // not supported
        case XK_Insert:
            return KEY_INSERT;
        case XK_Delete:
            return KEY_DELETE;
        case XK_Help:
            return KEY_HELP;

        case XK_Meta_L:
            return KEY_LSUPER;  // TODO: not sure this is right
        case XK_Meta_R:
            return KEY_RSUPER;
            // case XK_apps: return KEY_APPS; // not supported

            // case XK_sleep: return KEY_SLEEP; //not supported

        case XK_KP_0:
            return KEY_NUMPAD0;
        case XK_KP_1:
            return KEY_NUMPAD1;
        case XK_KP_2:
            return KEY_NUMPAD2;
        case XK_KP_3:
            return KEY_NUMPAD3;
        case XK_KP_4:
            return KEY_NUMPAD4;
        case XK_KP_5:
            return KEY_NUMPAD5;
        case XK_KP_6:
            return KEY_NUMPAD6;
        case XK_KP_7:
            return KEY_NUMPAD7;
        case XK_KP_8:
            return KEY_NUMPAD8;
        case XK_KP_9:
            return KEY_NUMPAD9;
        case XK_multiply:
            return KEY_MULTIPLY;
        case XK_KP_Add:
            return KEY_ADD;
        case XK_KP_Separator:
            return KEY_SEPARATOR;
        case XK_KP_Subtract:
            return KEY_SUBTRACT;
        case XK_KP_Decimal:
            return KEY_DECIMAL;
        case XK_KP_Divide:
            return KEY_DIVIDE;
        case XK_F1:
            return KEY_F1;
        case XK_F2:
            return KEY_F2;
        case XK_F3:
            return KEY_F3;
        case XK_F4:
            return KEY_F4;
        case XK_F5:
            return KEY_F5;
        case XK_F6:
            return KEY_F6;
        case XK_F7:
            return KEY_F7;
        case XK_F8:
            return KEY_F8;
        case XK_F9:
            return KEY_F9;
        case XK_F10:
            return KEY_F10;
        case XK_F11:
            return KEY_F11;
        case XK_F12:
            return KEY_F12;
        case XK_F13:
            return KEY_F13;
        case XK_F14:
            return KEY_F14;
        case XK_F15:
            return KEY_F15;
        case XK_F16:
            return KEY_F16;
        case XK_F17:
            return KEY_F17;
        case XK_F18:
            return KEY_F18;
        case XK_F19:
            return KEY_F19;
        case XK_F20:
            return KEY_F20;
        case XK_F21:
            return KEY_F21;
        case XK_F22:
            return KEY_F22;
        case XK_F23:
            return KEY_F23;
        case XK_F24:
            return KEY_F24;

        case XK_Num_Lock:
            return KEY_NUMLOCK;
        case XK_Scroll_Lock:
            return KEY_SCROLL;

        case XK_KP_Equal:
            return KEY_NUMPAD_EQUAL;

        case XK_Shift_L:
            return KEY_LSHIFT;
        case XK_Shift_R:
            return KEY_RSHIFT;
        case XK_Control_L:
            return KEY_LCONTROL;
        case XK_Control_R:
            return KEY_RCONTROL;
        case XK_Alt_L:
            return KEY_LALT;
        case XK_Alt_R:
            return KEY_RALT;

        case XK_semicolon:
            return KEY_SEMICOLON;
        case XK_plus:
            return KEY_EQUAL;
        case XK_comma:
            return KEY_COMMA;
        case XK_minus:
            return KEY_MINUS;
        case XK_period:
            return KEY_PERIOD;
        case XK_slash:
            return KEY_SLASH;
        case XK_grave:
            return KEY_GRAVE;

        case XK_0:
            return KEY_0;
        case XK_1:
            return KEY_1;
        case XK_2:
            return KEY_2;
        case XK_3:
            return KEY_3;
        case XK_4:
            return KEY_4;
        case XK_5:
            return KEY_5;
        case XK_6:
            return KEY_6;
        case XK_7:
            return KEY_7;
        case XK_8:
            return KEY_8;
        case XK_9:
            return KEY_9;

        case XK_a:
        case XK_A:
            return KEY_A;
        case XK_b:
        case XK_B:
            return KEY_B;
        case XK_c:
        case XK_C:
            return KEY_C;
        case XK_d:
        case XK_D:
            return KEY_D;
        case XK_e:
        case XK_E:
            return KEY_E;
        case XK_f:
        case XK_F:
            return KEY_F;
        case XK_g:
        case XK_G:
            return KEY_G;
        case XK_h:
        case XK_H:
            return KEY_H;
        case XK_i:
        case XK_I:
            return KEY_I;
        case XK_j:
        case XK_J:
            return KEY_J;
        case XK_k:
        case XK_K:
            return KEY_K;
        case XK_l:
        case XK_L:
            return KEY_L;
        case XK_m:
        case XK_M:
            return KEY_M;
        case XK_n:
        case XK_N:
            return KEY_N;
        case XK_o:
        case XK_O:
            return KEY_O;
        case XK_p:
        case XK_P:
            return KEY_P;
        case XK_q:
        case XK_Q:
            return KEY_Q;
        case XK_r:
        case XK_R:
            return KEY_R;
        case XK_s:
        case XK_S:
            return KEY_S;
        case XK_t:
        case XK_T:
            return KEY_T;
        case XK_u:
        case XK_U:
            return KEY_U;
        case XK_v:
        case XK_V:
            return KEY_V;
        case XK_w:
        case XK_W:
            return KEY_W;
        case XK_x:
        case XK_X:
            return KEY_X;
        case XK_y:
        case XK_Y:
            return KEY_Y;
        case XK_z:
        case XK_Z:
            return KEY_Z;

        default:
            return 0;
    }
}

#endif
