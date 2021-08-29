#include <platform/platform.h>

#if KPLATFORM_APPLE

#include "containers/darray.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kstring.h"
#include "core/logger.h"
#include "renderer/vulkan/vulkan_types.inl"  // For surface creation.

// Include Vulkan before GLFW.
#include <vulkan/vulkan.h>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct internal_state {
    GLFWwindow* glfw_window;
} internal_state;

static f64 start_time = 0;

static void platform_console_write_file(FILE* file, const char* message, u8 colour);

static void platform_error_callback(int error, const char* description);
static void platform_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void platform_mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
static void platform_cursor_position_callback(GLFWwindow* window, f64 xpos, f64 ypos);
static void platform_scroll_callback(GLFWwindow* window, f64 xoffset, f64 yoffset);
static void platform_framebuffer_size_callback(GLFWwindow* window, int width, int height);

static keys translate_key(int key);

b8 platform_startup(
    platform_state* platform_state,
    const char* application_name,
    i32 x,
    i32 y,
    i32 width,
    i32 height) {
    platform_state->internal_state = malloc(sizeof(internal_state));
    internal_state* state = (internal_state*)platform_state->internal_state;

    glfwSetErrorCallback(platform_error_callback);
    if (!glfwInit()) {
        KFATAL("Failed to initialise GLFW");
        return FALSE;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // Required for Vulkan.

    state->glfw_window = glfwCreateWindow(width, height, application_name, 0, 0);
    if (!state->glfw_window) {
        KFATAL("Failed to create a window");
        glfwTerminate();
        return FALSE;
    }

    glfwSetKeyCallback(state->glfw_window, platform_key_callback);
    glfwSetMouseButtonCallback(state->glfw_window, platform_mouse_button_callback);
    glfwSetCursorPosCallback(state->glfw_window, platform_cursor_position_callback);
    glfwSetScrollCallback(state->glfw_window, platform_scroll_callback);
    glfwSetFramebufferSizeCallback(state->glfw_window, platform_framebuffer_size_callback);

    glfwSetWindowPos(state->glfw_window, x, y);
    glfwShowWindow(state->glfw_window);
    start_time = glfwGetTime();

    return TRUE;
}

void platform_shutdown(platform_state* platform_state) {
    internal_state* state = (internal_state*)platform_state->internal_state;

    glfwDestroyWindow(state->glfw_window);
    state->glfw_window = 0;

    glfwSetErrorCallback(0);
    glfwTerminate();
}

b8 platform_pump_messages(platform_state* plat_state) {
    internal_state* state = (internal_state*)plat_state->internal_state;

    glfwPollEvents();
    b8 should_continue = !glfwWindowShouldClose(state->glfw_window);
    return should_continue;
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
    platform_console_write_file(stdout, message, colour);
}

void platform_console_write_error(const char* message, u8 colour) {
    platform_console_write_file(stderr, message, colour);
}

static void platform_console_write_file(FILE* file, const char* message, u8 colour) {
    // Colours: FATAL, ERROR, WARN, INFO, DEBUG, TRACE.
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    fprintf(file, "\033[%sm%s\033[0m", colour_strings[colour], message);
}

f64 platform_get_absolute_time(void) {
    f64 time = glfwGetTime();
    return time;
}

void platform_sleep(u64 ms) {
    struct timespec ts = {0};
    ts.tv_sec = (long)((f64)ms * 0.001);
    ts.tv_nsec = ((long)ms % 1000) * 1000 * 1000;
    nanosleep(&ts, 0);
}

void platform_get_required_extension_names(const char*** names_darray) {
    u32 count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    for (u32 i = 0; i < count; ++i) {
        if (strings_equal(extensions[i], "VK_KHR_surface")) {
            // We already include "VK_KHR_surface", so skip this.
            continue;
        }
        darray_push(*names_darray, extensions[i]);
    }
}

b8 platform_create_vulkan_surface(platform_state* plat_state, vulkan_context* context) {
    internal_state* state = (internal_state*)plat_state->internal_state;

    VkResult result = glfwCreateWindowSurface(context->instance, state->glfw_window, 0, &context->surface);
    if (result != VK_SUCCESS) {
        KFATAL("Vulkan surface creation failed.");
        return FALSE;
    }

    return TRUE;
}

static void platform_error_callback(int error, const char* description) {
    platform_console_write_error(description, 0);
}

static void platform_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    keys our_key = translate_key(key);
    if (our_key != KEYS_MAX_KEYS) {
        b8 pressed = action == GLFW_PRESS || action == GLFW_REPEAT;
        input_process_key(our_key, pressed);
    }
}

static void platform_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    buttons mouse_button = BUTTON_MAX_BUTTONS;

    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            mouse_button = BUTTON_LEFT;
            break;
        case GLFW_MOUSE_BUTTON_MIDDLE:
            mouse_button = BUTTON_MIDDLE;
            break;
        case GLFW_MOUSE_BUTTON_RIGHT:
            mouse_button = BUTTON_RIGHT;
            break;
        default:
            mouse_button = BUTTON_MAX_BUTTONS;
    }

    if (mouse_button != BUTTON_MAX_BUTTONS) {
        b8 pressed = action == GLFW_PRESS;
        input_process_button(mouse_button, pressed);
    }
}

static void platform_cursor_position_callback(GLFWwindow* window, f64 xpos, f64 ypos) {
    input_process_mouse_move((i16)xpos, (i16)ypos);
}

static void platform_scroll_callback(GLFWwindow* window, f64 xoffset, f64 yoffset) {
    // We ignore horizontal scroll and also flatten to OS-independent values (-1, +1).
    i8 z_delta = (i8)yoffset;
    if (z_delta != 0) {
        z_delta = (z_delta < 0) ? -1 : 1;
    }
    input_process_mouse_wheel(z_delta);
}

static void platform_framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    event_context context = {.data.u16[0] = (u16)width, .data.u16[1] = (u16)height};
    event_fire(EVENT_CODE_RESIZED, 0, context);
}

static keys translate_key(int key) {
    keys our_key = KEYS_MAX_KEYS;

    switch (key) {
        case GLFW_KEY_SPACE:
            our_key = KEY_SPACE;
            break;
        case GLFW_KEY_COMMA:
            our_key = KEY_COMMA;
            break;
        case GLFW_KEY_MINUS:
            our_key = KEY_MINUS;
            break;
        case GLFW_KEY_PERIOD:
            our_key = KEY_PERIOD;
            break;
        case GLFW_KEY_SLASH:
            our_key = KEY_SLASH;
            break;
        case GLFW_KEY_0:
            our_key = KEY_NUMPAD0;
            break;
        case GLFW_KEY_1:
            our_key = KEY_NUMPAD1;
            break;
        case GLFW_KEY_2:
            our_key = KEY_NUMPAD2;
            break;
        case GLFW_KEY_3:
            our_key = KEY_NUMPAD3;
            break;
        case GLFW_KEY_4:
            our_key = KEY_NUMPAD4;
            break;
        case GLFW_KEY_5:
            our_key = KEY_NUMPAD5;
            break;
        case GLFW_KEY_6:
            our_key = KEY_NUMPAD6;
            break;
        case GLFW_KEY_7:
            our_key = KEY_NUMPAD7;
            break;
        case GLFW_KEY_8:
            our_key = KEY_NUMPAD8;
            break;
        case GLFW_KEY_9:
            our_key = KEY_NUMPAD9;
            break;
        case GLFW_KEY_SEMICOLON:
            our_key = KEY_SEMICOLON;
            break;
        case GLFW_KEY_EQUAL:
            our_key = KEY_PLUS;
            break;
        case GLFW_KEY_A:
            our_key = KEY_A;
            break;
        case GLFW_KEY_B:
            our_key = KEY_B;
            break;
        case GLFW_KEY_C:
            our_key = KEY_C;
            break;
        case GLFW_KEY_D:
            our_key = KEY_D;
            break;
        case GLFW_KEY_E:
            our_key = KEY_E;
            break;
        case GLFW_KEY_F:
            our_key = KEY_F;
            break;
        case GLFW_KEY_G:
            our_key = KEY_G;
            break;
        case GLFW_KEY_H:
            our_key = KEY_H;
            break;
        case GLFW_KEY_I:
            our_key = KEY_I;
            break;
        case GLFW_KEY_J:
            our_key = KEY_J;
            break;
        case GLFW_KEY_K:
            our_key = KEY_K;
            break;
        case GLFW_KEY_L:
            our_key = KEY_L;
            break;
        case GLFW_KEY_M:
            our_key = KEY_M;
            break;
        case GLFW_KEY_N:
            our_key = KEY_N;
            break;
        case GLFW_KEY_O:
            our_key = KEY_O;
            break;
        case GLFW_KEY_P:
            our_key = KEY_P;
            break;
        case GLFW_KEY_Q:
            our_key = KEY_Q;
            break;
        case GLFW_KEY_R:
            our_key = KEY_R;
            break;
        case GLFW_KEY_S:
            our_key = KEY_S;
            break;
        case GLFW_KEY_T:
            our_key = KEY_T;
            break;
        case GLFW_KEY_U:
            our_key = KEY_U;
            break;
        case GLFW_KEY_V:
            our_key = KEY_V;
            break;
        case GLFW_KEY_W:
            our_key = KEY_W;
            break;
        case GLFW_KEY_X:
            our_key = KEY_X;
            break;
        case GLFW_KEY_Y:
            our_key = KEY_Y;
            break;
        case GLFW_KEY_Z:
            our_key = KEY_Z;
            break;
        case GLFW_KEY_GRAVE_ACCENT:
            our_key = KEY_GRAVE;
            break;
        case GLFW_KEY_ESCAPE:
            our_key = KEY_ESCAPE;
            break;
        case GLFW_KEY_ENTER:
            our_key = KEY_ENTER;
            break;
        case GLFW_KEY_TAB:
            our_key = KEY_TAB;
            break;
        case GLFW_KEY_BACKSPACE:
            our_key = KEY_BACKSPACE;
            break;
        case GLFW_KEY_INSERT:
            our_key = KEY_INSERT;
            break;
        case GLFW_KEY_DELETE:
            our_key = KEY_DELETE;
            break;
        case GLFW_KEY_RIGHT:
            our_key = KEY_RIGHT;
            break;
        case GLFW_KEY_LEFT:
            our_key = KEY_LEFT;
            break;
        case GLFW_KEY_DOWN:
            our_key = KEY_DOWN;
            break;
        case GLFW_KEY_UP:
            our_key = KEY_UP;
            break;
        case GLFW_KEY_PAGE_UP:
            our_key = KEY_PRIOR;
            break;
        case GLFW_KEY_PAGE_DOWN:
            our_key = KEY_NEXT;
            break;
        case GLFW_KEY_HOME:
            our_key = KEY_HOME;
            break;
        case GLFW_KEY_END:
            our_key = KEY_END;
            break;
        case GLFW_KEY_CAPS_LOCK:
            our_key = KEY_CAPITAL;
            break;
        case GLFW_KEY_SCROLL_LOCK:
            our_key = KEY_SCROLL;
            break;
        case GLFW_KEY_NUM_LOCK:
            our_key = KEY_NUMLOCK;
            break;
        case GLFW_KEY_PRINT_SCREEN:
            our_key = KEY_SNAPSHOT;
            break;
        case GLFW_KEY_PAUSE:
            our_key = KEY_PAUSE;
            break;
        case GLFW_KEY_F1:
            our_key = KEY_F1;
            break;
        case GLFW_KEY_F2:
            our_key = KEY_F2;
            break;
        case GLFW_KEY_F3:
            our_key = KEY_F3;
            break;
        case GLFW_KEY_F4:
            our_key = KEY_F4;
            break;
        case GLFW_KEY_F5:
            our_key = KEY_F5;
            break;
        case GLFW_KEY_F6:
            our_key = KEY_F6;
            break;
        case GLFW_KEY_F7:
            our_key = KEY_F7;
            break;
        case GLFW_KEY_F8:
            our_key = KEY_F8;
            break;
        case GLFW_KEY_F9:
            our_key = KEY_F9;
            break;
        case GLFW_KEY_F10:
            our_key = KEY_F10;
            break;
        case GLFW_KEY_F11:
            our_key = KEY_F11;
            break;
        case GLFW_KEY_F12:
            our_key = KEY_F12;
            break;
        case GLFW_KEY_F13:
            our_key = KEY_F13;
            break;
        case GLFW_KEY_F14:
            our_key = KEY_F14;
            break;
        case GLFW_KEY_F15:
            our_key = KEY_F15;
            break;
        case GLFW_KEY_F16:
            our_key = KEY_F16;
            break;
        case GLFW_KEY_F17:
            our_key = KEY_F17;
            break;
        case GLFW_KEY_F18:
            our_key = KEY_F18;
            break;
        case GLFW_KEY_F19:
            our_key = KEY_F19;
            break;
        case GLFW_KEY_F20:
            our_key = KEY_F20;
            break;
        case GLFW_KEY_F21:
            our_key = KEY_F21;
            break;
        case GLFW_KEY_F22:
            our_key = KEY_F22;
            break;
        case GLFW_KEY_F23:
            our_key = KEY_F23;
            break;
        case GLFW_KEY_F24:
            our_key = KEY_F24;
            break;
        case GLFW_KEY_KP_0:
            our_key = KEY_NUMPAD0;
            break;
        case GLFW_KEY_KP_1:
            our_key = KEY_NUMPAD1;
            break;
        case GLFW_KEY_KP_2:
            our_key = KEY_NUMPAD2;
            break;
        case GLFW_KEY_KP_3:
            our_key = KEY_NUMPAD3;
            break;
        case GLFW_KEY_KP_4:
            our_key = KEY_NUMPAD4;
            break;
        case GLFW_KEY_KP_5:
            our_key = KEY_NUMPAD5;
            break;
        case GLFW_KEY_KP_6:
            our_key = KEY_NUMPAD6;
            break;
        case GLFW_KEY_KP_7:
            our_key = KEY_NUMPAD7;
            break;
        case GLFW_KEY_KP_8:
            our_key = KEY_NUMPAD8;
            break;
        case GLFW_KEY_KP_9:
            our_key = KEY_NUMPAD9;
            break;
        case GLFW_KEY_KP_DECIMAL:
            our_key = KEY_DECIMAL;
            break;
        case GLFW_KEY_KP_DIVIDE:
            our_key = KEY_DIVIDE;
            break;
        case GLFW_KEY_KP_MULTIPLY:
            our_key = KEY_MULTIPLY;
            break;
        case GLFW_KEY_KP_SUBTRACT:
            our_key = KEY_SUBTRACT;
            break;
        case GLFW_KEY_KP_ADD:
            our_key = KEY_ADD;
            break;
        case GLFW_KEY_KP_ENTER:
            our_key = KEY_ENTER;
            break;
        case GLFW_KEY_KP_EQUAL:
            our_key = KEY_NUMPAD_EQUAL;
            break;
        case GLFW_KEY_LEFT_SHIFT:
            our_key = KEY_LSHIFT;
            break;
        case GLFW_KEY_LEFT_CONTROL:
            our_key = KEY_LCONTROL;
            break;
        case GLFW_KEY_LEFT_ALT:
            our_key = KEY_LMENU;
            break;
        case GLFW_KEY_LEFT_SUPER:
            our_key = KEY_LWIN;
            break;
        case GLFW_KEY_RIGHT_SHIFT:
            our_key = KEY_RSHIFT;
            break;
        case GLFW_KEY_RIGHT_CONTROL:
            our_key = KEY_RCONTROL;
            break;
        case GLFW_KEY_RIGHT_ALT:
            our_key = KEY_RMENU;
            break;
        case GLFW_KEY_RIGHT_SUPER:
            our_key = KEY_RWIN;
            break;
        default:
            // GLFW_KEY_UNKNOWN
            // GLFW_KEY_LAST
            // GLFW_KEY_APOSTROPHE
            // GLFW_KEY_LEFT_BRACKET
            // GLFW_KEY_BACKSLASH
            // GLFW_KEY_RIGHT_BRACKET
            // GLFW_KEY_F25
            // GLFW_KEY_WORLD_1
            // GLFW_KEY_WORLD_2
            // GLFW_KEY_MENU
            our_key = KEYS_MAX_KEYS;
    }

    return our_key;
}

#endif  // KPLATFORM_APPLE
