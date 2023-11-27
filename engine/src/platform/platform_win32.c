#include "platform/platform.h"

// Windows platform layer.
#if KPLATFORM_WINDOWS

#include "containers/darray.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kmemory.h"
#include "core/kmutex.h"
#include "core/kstring.h"
#include "core/kthread.h"
#include "core/logger.h"

#define WIN32_LEAN_AND_MEAN
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>  // param input extraction

typedef struct win32_handle_info {
    HINSTANCE h_instance;
    HWND hwnd;
} win32_handle_info;

typedef struct win32_file_watch {
    u32 id;
    const char *file_path;
    FILETIME last_write_time;
} win32_file_watch;

typedef struct platform_state {
    win32_handle_info handle;
    CONSOLE_SCREEN_BUFFER_INFO std_output_csbi;
    CONSOLE_SCREEN_BUFFER_INFO err_output_csbi;
    // darray
    win32_file_watch *watches;
    f32 device_pixel_ratio;
} platform_state;

static platform_state *state_ptr;

// Clock
static f64 clock_frequency;
static LARGE_INTEGER start_time;

static void platform_update_watches(void);
LRESULT CALLBACK win32_process_message(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param);

void clock_setup(void) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    clock_frequency = 1.0 / (f64)frequency.QuadPart;
    QueryPerformanceCounter(&start_time);
}

b8 platform_system_startup(u64 *memory_requirement, void *state, void *config) {
    platform_system_config *typed_config = (platform_system_config *)config;
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }
    state_ptr = state;
    state_ptr->handle.h_instance = GetModuleHandleA(0);

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &state_ptr->std_output_csbi);
    GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &state_ptr->err_output_csbi);

    // Only available in the Creators update for Windows 10+.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // NOTE: Older versions of windows might have to use this:
    // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    state_ptr->device_pixel_ratio = 1.0f;

    // Setup and register window class.
    HICON icon = LoadIcon(state_ptr->handle.h_instance, IDI_APPLICATION);
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_DBLCLKS;  // Get double-clicks
    wc.lpfnWndProc = win32_process_message;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = state_ptr->handle.h_instance;
    wc.hIcon = icon;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);  // NULL; // Manage the cursor manually
    wc.hbrBackground = NULL;                   // Transparent
    wc.lpszClassName = "kohi_window_class";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(0, "Window registration failed", "Error", MB_ICONEXCLAMATION | MB_OK);
        return false;
    }

    // Create window
    u32 client_x = typed_config->x;
    u32 client_y = typed_config->y;
    u32 client_width = typed_config->width;
    u32 client_height = typed_config->height;

    u32 window_x = client_x;
    u32 window_y = client_y;
    u32 window_width = client_width;
    u32 window_height = client_height;

    u32 window_style = WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION;
    u32 window_ex_style = WS_EX_APPWINDOW;

    window_style |= WS_MAXIMIZEBOX;
    window_style |= WS_MINIMIZEBOX;
    window_style |= WS_THICKFRAME;

    // Obtain the size of the border.
    RECT border_rect = {0, 0, 0, 0};
    AdjustWindowRectEx(&border_rect, window_style, 0, window_ex_style);

    // In this case, the border rectangle is negative.
    window_x += border_rect.left;
    window_y += border_rect.top;

    // Grow by the size of the OS border.
    window_width += border_rect.right - border_rect.left;
    window_height += border_rect.bottom - border_rect.top;

    HWND handle = CreateWindowExA(
        window_ex_style, "kohi_window_class", typed_config->application_name,
        window_style, window_x, window_y, window_width, window_height,
        0, 0, state_ptr->handle.h_instance, 0);

    if (handle == 0) {
        MessageBoxA(NULL, "Window creation failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);

        KFATAL("Window creation failed!");
        return false;
    } else {
        state_ptr->handle.hwnd = handle;
    }

    // Show the window
    b32 should_activate = 1;  // TODO: if the window should not accept input, this should be false.
    i32 show_window_command_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVATE;
    // If initially minimized, use SW_MINIMIZE : SW_SHOWMINNOACTIVE;
    // If initially maximized, use SW_SHOWMAXIMIZED : SW_MAXIMIZE
    ShowWindow(state_ptr->handle.hwnd, show_window_command_flags);

    // Clock setup
    clock_setup();

    return true;
}

void platform_system_shutdown(void *plat_state) {
    if (state_ptr && state_ptr->handle.hwnd) {
        DestroyWindow(state_ptr->handle.hwnd);
        state_ptr->handle.hwnd = 0;
    }
}

b8 platform_pump_messages(void) {
    if (state_ptr) {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    platform_update_watches();
    return true;
}

void *platform_allocate(u64 size, b8 aligned) {
    // return malloc(size);
    return (void *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void platform_free(void *block, b8 aligned) {
    // free(block);
    HeapFree(GetProcessHeap(), 0, block);
}

void *platform_zero_memory(void *block, u64 size) {
    return memset(block, 0, size);
}

void *platform_copy_memory(void *dest, const void *source, u64 size) {
    return memcpy(dest, source, size);
}

void *platform_set_memory(void *dest, i32 value, u64 size) {
    return memset(dest, value, size);
}

void platform_console_write(const char *message, u8 colour) {
    HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (state_ptr) {
        csbi = state_ptr->std_output_csbi;
    } else {
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    }

    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    static u8 levels[6] = {64, 4, 6, 2, 1, 8};
    SetConsoleTextAttribute(console_handle, levels[colour]);
    OutputDebugStringA(message);
    u64 length = strlen(message);
    DWORD number_written = 0;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), message, (DWORD)length, &number_written, 0);

    SetConsoleTextAttribute(console_handle, csbi.wAttributes);
}

void platform_console_write_error(const char *message, u8 colour) {
    HANDLE console_handle = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (state_ptr) {
        csbi = state_ptr->err_output_csbi;
    } else {
        GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &csbi);
    }

    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    static u8 levels[6] = {64, 4, 6, 2, 1, 8};
    SetConsoleTextAttribute(console_handle, levels[colour]);
    OutputDebugStringA(message);
    u64 length = strlen(message);
    DWORD number_written = 0;
    WriteConsoleA(GetStdHandle(STD_ERROR_HANDLE), message, (DWORD)length, &number_written, 0);

    SetConsoleTextAttribute(console_handle, csbi.wAttributes);
}

f64 platform_get_absolute_time(void) {
    if (!clock_frequency) {
        clock_setup();
    }

    LARGE_INTEGER now_time;
    QueryPerformanceCounter(&now_time);
    return (f64)now_time.QuadPart * clock_frequency;
}

void platform_sleep(u64 ms) {
    Sleep(ms);
}

i32 platform_get_processor_count(void) {
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    KINFO("%i processor cores detected.", sysinfo.dwNumberOfProcessors);
    return sysinfo.dwNumberOfProcessors;
}

void platform_get_handle_info(u64 *out_size, void *memory) {
    *out_size = sizeof(win32_handle_info);
    if (!memory) {
        return;
    }

    kcopy_memory(memory, &state_ptr->handle, *out_size);
}

f32 platform_device_pixel_ratio(void) {
    return state_ptr->device_pixel_ratio;
}

// NOTE: Begin threads
b8 kthread_create(pfn_thread_start start_function_ptr, void *params, b8 auto_detach, kthread *out_thread) {
    if (!start_function_ptr) {
        return false;
    }

    out_thread->internal_data = CreateThread(
        0,
        0,                                           // Default stack size
        (LPTHREAD_START_ROUTINE)start_function_ptr,  // function ptr
        params,                                      // param to pass to thread
        0,
        (DWORD *)&out_thread->thread_id);
    KDEBUG("Starting process on thread id: %#x", out_thread->thread_id);
    if (!out_thread->internal_data) {
        return false;
    }
    if (auto_detach) {
        CloseHandle(out_thread->internal_data);
    }
    return true;
}

void kthread_destroy(kthread *thread) {
    if (thread && thread->internal_data) {
        DWORD exit_code;
        GetExitCodeThread(thread->internal_data, &exit_code);
        // if (exit_code == STILL_ACTIVE) {
        //     TerminateThread(thread->internal_data, 0);  // 0 = failure
        // }
        CloseHandle((HANDLE)thread->internal_data);
        thread->internal_data = 0;
        thread->thread_id = 0;
    }
}

void kthread_detach(kthread *thread) {
    if (thread && thread->internal_data) {
        CloseHandle(thread->internal_data);
        thread->internal_data = 0;
    }
}

void kthread_cancel(kthread *thread) {
    if (thread && thread->internal_data) {
        TerminateThread(thread->internal_data, 0);
        thread->internal_data = 0;
    }
}

b8 kthread_is_active(kthread *thread) {
    if (thread && thread->internal_data) {
        DWORD exit_code = WaitForSingleObject(thread->internal_data, 0);
        if (exit_code == WAIT_TIMEOUT) {
            return true;
        }
    }
    return false;
}

void kthread_sleep(kthread *thread, u64 ms) {
    platform_sleep(ms);
}

u64 platform_current_thread_id(void) {
    return (u64)GetCurrentThreadId();
}

// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create(kmutex *out_mutex) {
    if (!out_mutex) {
        return false;
    }

    out_mutex->internal_data = CreateMutex(0, 0, 0);
    if (!out_mutex->internal_data) {
        KERROR("Unable to create mutex.");
        return false;
    }
    // KTRACE("Created mutex.");
    return true;
}

void kmutex_destroy(kmutex *mutex) {
    if (mutex && mutex->internal_data) {
        CloseHandle(mutex->internal_data);
        // KTRACE("Destroyed mutex.");
        mutex->internal_data = 0;
    }
}

b8 kmutex_lock(kmutex *mutex) {
    if (!mutex) {
        return false;
    }

    DWORD result = WaitForSingleObject(mutex->internal_data, INFINITE);
    switch (result) {
        // The thread got ownership of the mutex
        case WAIT_OBJECT_0:
            // KTRACE("Mutex locked.");
            return true;

            // The thread got ownership of an abandoned mutex.
        case WAIT_ABANDONED:
            KERROR("Mutex lock failed.");
            return false;
    }
    // KTRACE("Mutex locked.");
    return true;
}

b8 kmutex_unlock(kmutex *mutex) {
    if (!mutex || !mutex->internal_data) {
        return false;
    }
    i32 result = ReleaseMutex(mutex->internal_data);
    // KTRACE("Mutex unlocked.");
    return result != 0;  // 0 is a failure
}

// NOTE: End mutexes.

b8 platform_dynamic_library_load(const char *name, dynamic_library *out_library) {
    if (!out_library) {
        return false;
    }
    kzero_memory(out_library, sizeof(dynamic_library));
    if (!name) {
        return false;
    }

    char filename[MAX_PATH];
    kzero_memory(filename, sizeof(char) * MAX_PATH);
    string_format(filename, "%s.dll", name);

    HMODULE library = LoadLibraryA(filename);
    if (!library) {
        return false;
    }

    out_library->name = string_duplicate(name);
    out_library->filename = string_duplicate(filename);

    out_library->internal_data_size = sizeof(HMODULE);
    out_library->internal_data = library;

    out_library->functions = darray_create(dynamic_library_function);

    return true;
}

b8 platform_dynamic_library_unload(dynamic_library *library) {
    if (!library) {
        return false;
    }

    HMODULE internal_module = (HMODULE)library->internal_data;
    if (!internal_module) {
        return false;
    }

    if (library->name) {
        u64 length = string_length(library->name);
        kfree((void *)library->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->filename) {
        u64 length = string_length(library->filename);
        kfree((void *)library->filename, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->functions) {
        u32 count = darray_length(library->functions);
        for (u32 i = 0; i < count; ++i) {
            dynamic_library_function *f = &library->functions[i];
            if (f->name) {
                u64 length = string_length(f->name);
                kfree((void *)f->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
            }
        }

        darray_destroy(library->functions);
        library->functions = 0;
    }

    BOOL result = FreeLibrary(internal_module);
    if (result == 0) {
        return false;
    }

    kzero_memory(library, sizeof(dynamic_library));

    return true;
}

b8 platform_dynamic_library_load_function(const char *name, dynamic_library *library) {
    if (!name || !library) {
        return false;
    }

    if (!library->internal_data) {
        return false;
    }

    FARPROC f_addr = GetProcAddress((HMODULE)library->internal_data, name);
    if (!f_addr) {
        return false;
    }

    dynamic_library_function f = {0};
    f.pfn = f_addr;
    f.name = string_duplicate(name);
    darray_push(library->functions, f);

    return true;
}

const char *platform_dynamic_library_extension(void) {
    return ".dll";
}

const char *platform_dynamic_library_prefix(void) {
    return "";
}

platform_error_code platform_copy_file(const char *source, const char *dest, b8 overwrite_if_exists) {
    BOOL result = CopyFileA(source, dest, !overwrite_if_exists);
    if (!result) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return PLATFORM_ERROR_FILE_NOT_FOUND;
        } else if (err == ERROR_SHARING_VIOLATION) {
            return PLATFORM_ERROR_FILE_LOCKED;
        } else {
            return PLATFORM_ERROR_UNKNOWN;
        }
    }
    return PLATFORM_ERROR_SUCCESS;
}

static b8 register_watch(const char *file_path, u32 *out_watch_id) {
    if (!state_ptr || !file_path || !out_watch_id) {
        if (out_watch_id) {
            *out_watch_id = INVALID_ID;
        }
        return false;
    }
    *out_watch_id = INVALID_ID;

    if (!state_ptr->watches) {
        state_ptr->watches = darray_create(win32_file_watch);
    }

    WIN32_FIND_DATAA data;
    HANDLE file_handle = FindFirstFileA(file_path, &data);
    if (file_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BOOL result = FindClose(file_handle);
    if (result == 0) {
        return false;
    }

    u32 count = darray_length(state_ptr->watches);
    for (u32 i = 0; i < count; ++i) {
        win32_file_watch *w = &state_ptr->watches[i];
        if (w->id == INVALID_ID) {
            // Found a free slot to use.
            w->id = i;
            w->file_path = string_duplicate(file_path);
            w->last_write_time = data.ftLastWriteTime;
            *out_watch_id = i;
            return true;
        }
    }

    // If no empty slot is available, create and push a new entry.
    win32_file_watch w = {0};
    w.id = count;
    w.file_path = string_duplicate(file_path);
    w.last_write_time = data.ftLastWriteTime;
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

    win32_file_watch *w = &state_ptr->watches[watch_id];
    w->id = INVALID_ID;
    u32 len = string_length(w->file_path);
    kfree((void *)w->file_path, sizeof(char) * (len + 1), MEMORY_TAG_STRING);
    w->file_path = 0;
    kzero_memory(&w->last_write_time, sizeof(FILETIME));

    return true;
}

b8 platform_watch_file(const char *file_path, u32 *out_watch_id) {
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
        win32_file_watch *f = &state_ptr->watches[i];
        if (f->id != INVALID_ID) {
            WIN32_FIND_DATAA data;
            HANDLE file_handle = FindFirstFileA(f->file_path, &data);
            if (file_handle == INVALID_HANDLE_VALUE) {
                // This means the file has been deleted, remove from watch.
                event_context context = {0};
                context.data.u32[0] = f->id;
                event_fire(EVENT_CODE_WATCHED_FILE_DELETED, 0, context);
                KINFO("File watch id %d has been removed.", f->id);
                unregister_watch(f->id);
                continue;
            }
            BOOL result = FindClose(file_handle);
            if (result == 0) {
                continue;
            }

            // Check the file time to see if it has been changed and update/notify if so.
            if (CompareFileTime(&data.ftLastWriteTime, &f->last_write_time) != 0) {
                f->last_write_time = data.ftLastWriteTime;
                // Notify listeners.
                event_context context = {0};
                context.data.u32[0] = f->id;
                event_fire(EVENT_CODE_WATCHED_FILE_WRITTEN, 0, context);
            }
        }
    }
}

LRESULT CALLBACK win32_process_message(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
        case WM_ERASEBKGND:
            // Notify the OS that erasing will be handled by the application to prevent flicker.
            return 1;
        case WM_CLOSE:
            // TODO: Fire an event for the application to quit.
            event_context data = {};
            event_fire(EVENT_CODE_APPLICATION_QUIT, 0, data);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED:
            // x- and y-axis DPI are always the same, so just grab one.
            i32 x_dpi = GET_X_LPARAM(w_param);

            // Store off the device pixel ratio.
            state_ptr->device_pixel_ratio = (f32)x_dpi / USER_DEFAULT_SCREEN_DPI;
            KINFO("Display device pixel ratio is: %.2f", state_ptr->device_pixel_ratio);

            return 0;
        case WM_SIZE: {
            // Get the updated size.
            RECT r;
            GetClientRect(hwnd, &r);
            u32 width = r.right - r.left;
            u32 height = r.bottom - r.top;

            {
                HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

                MONITORINFO monitor_info = {0};
                monitor_info.cbSize = sizeof(MONITORINFO);
                if (!GetMonitorInfoA(monitor, &monitor_info)) {
                    KWARN("Failed to get monitor info. ");
                }

                KINFO("monitor: %u", monitor_info.rcMonitor.left);
            }

            // Fire the event. The application layer should pick this up, but not handle it
            // as it shouldn be visible to other parts of the application.
            event_context context;
            context.data.u16[0] = (u16)width;
            context.data.u16[1] = (u16)height;
            event_fire(EVENT_CODE_RESIZED, 0, context);
        } break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            // Key pressed/released
            b8 pressed = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
            keys key = (u16)w_param;

            // Check for extended scan code.
            b8 is_extended = (HIWORD(l_param) & KF_EXTENDED) == KF_EXTENDED;

            // Keypress only determines if _any_ alt/ctrl/shift key is pressed. Determine which one if so.
            if (w_param == VK_MENU) {
                key = is_extended ? KEY_RALT : KEY_LALT;
            } else if (w_param == VK_SHIFT) {
                // Annoyingly, KF_EXTENDED is not set for shift keys.
                u32 left_shift = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
                u32 scancode = ((l_param & (0xFF << 16)) >> 16);
                key = scancode == left_shift ? KEY_LSHIFT : KEY_RSHIFT;
            } else if (w_param == VK_CONTROL) {
                key = is_extended ? KEY_RCONTROL : KEY_LCONTROL;
            }

            // Pass to the input subsystem for processing.
            input_process_key(key, pressed);

            // Return 0 to prevent default window behaviour for some keypresses, such as alt.
            return 0;
        }
        case WM_MOUSEMOVE: {
            // Mouse move
            i32 x_position = GET_X_LPARAM(l_param);
            i32 y_position = GET_Y_LPARAM(l_param);

            // Pass over to the input subsystem.
            input_process_mouse_move(x_position, y_position);
        } break;
        case WM_MOUSEWHEEL: {
            i32 z_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (z_delta != 0) {
                // Flatten the input to an OS-independent (-1, 1)
                z_delta = (z_delta < 0) ? -1 : 1;
                input_process_mouse_wheel(z_delta);
            }
        } break;
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            b8 pressed = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            buttons mouse_button = BUTTON_MAX_BUTTONS;
            switch (msg) {
                case WM_LBUTTONDOWN:
                case WM_LBUTTONUP:
                    mouse_button = BUTTON_LEFT;
                    break;
                case WM_MBUTTONDOWN:
                case WM_MBUTTONUP:
                    mouse_button = BUTTON_MIDDLE;
                    break;
                case WM_RBUTTONDOWN:
                case WM_RBUTTONUP:
                    mouse_button = BUTTON_RIGHT;
                    break;
            }

            // Pass over to the input subsystem.
            if (mouse_button != BUTTON_MAX_BUTTONS) {
                input_process_button(mouse_button, pressed);
            }
        } break;
    }

    return DefWindowProcA(hwnd, msg, w_param, l_param);
}

#endif  // KPLATFORM_WINDOWS
