#include "platform/platform.h"

// Windows platform layer.
#if _WIN32 // KPLATFORM_WINDOWS // FIXME: macro doesn't highlight correctly in vscode

#    include "containers/darray.h"
#    include "logger.h"
#    include "memory/kmemory.h"
#    include "strings/kstring.h"
#    include "threads/kmutex.h"
#    include "threads/ksemaphore.h"
#    include "threads/kthread.h"
#    include <input_types.h>

#    define WIN32_LEAN_AND_MEAN
#    include <stdlib.h>
#    include <windows.h>
#    include <windowsx.h> // param input extraction

typedef struct win32_handle_info {
    HINSTANCE h_instance;
} win32_handle_info;

typedef struct win32_file_watch {
    u32 id;
    const char* file_path;
    FILETIME last_write_time;
} win32_file_watch;

typedef struct kwindow_platform_state {
    HWND hwnd;
} kwindow_platform_state;

typedef struct platform_state {
    win32_handle_info handle;
    CONSOLE_SCREEN_BUFFER_INFO std_output_csbi;
    CONSOLE_SCREEN_BUFFER_INFO err_output_csbi;
    // darray
    win32_file_watch* watches;
    f32 device_pixel_ratio;

    // darray of pointers to created windows (owned by the application);
    kwindow** windows;
    platform_filewatcher_file_deleted_callback watcher_deleted_callback;
    platform_filewatcher_file_written_callback watcher_written_callback;
    platform_window_closed_callback window_closed_callback;
    platform_window_resized_callback window_resized_callback;
    platform_process_key process_key;
    platform_process_mouse_button process_mouse_button;
    platform_process_mouse_move process_mouse_move;
    platform_process_mouse_wheel process_mouse_wheel;
} platform_state;

static platform_state* state_ptr;

// Clock
static f64 clock_frequency;
static LARGE_INTEGER start_time;

static void platform_update_watches(void);
LRESULT CALLBACK win32_process_message(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param);
static LPCWSTR cstr_to_wcstr(const char* str);
static void wcstr_free(LPCWSTR wstr);
static const char* wcstr_to_cstr(LPCWSTR wstr);

void clock_setup(void) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    clock_frequency = 1.0 / (f64)frequency.QuadPart;
    QueryPerformanceCounter(&start_time);
}

b8 platform_system_startup(u64* memory_requirement, platform_state* state, platform_system_config* config) {
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }
    state_ptr = state;
    state_ptr->handle.h_instance = GetModuleHandleW(0);

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &state_ptr->std_output_csbi);
    GetConsoleScreenBufferInfo(GetStdHandle(STD_ERROR_HANDLE), &state_ptr->err_output_csbi);

    // Clock setup
    clock_setup();

    // Create the array of window pointers.
    state->windows = darray_create(kwindow*);
    state->watcher_deleted_callback = 0;
    state->watcher_written_callback = 0;
    state->window_closed_callback = 0;
    state->window_resized_callback = 0;
    state->process_key = 0;
    state->process_mouse_button = 0;
    state->process_mouse_move = 0;
    state->process_mouse_wheel = 0;

    // Only available in the Creators update for Windows 10+.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // NOTE: Older versions of windows might have to use this:
    // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
    state_ptr->device_pixel_ratio = 1.0f;

    // Setup and register window class. This can be done at platform init and be reused.
    HICON icon = LoadIconW(state_ptr->handle.h_instance, IDI_APPLICATION);
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_DBLCLKS; // Get double-clicks
    wc.lpfnWndProc = win32_process_message;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = state_ptr->handle.h_instance;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // NULL; // Manage the cursor manually
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"kohi_window_class";
    wc.lpszMenuName = 0;

    if (!RegisterClassExW(&wc)) {
        DWORD last_error = GetLastError();
        LPWSTR wmessage_buf = 0;

        // Ask Win32 to give us the string version of that message ID.
        // The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
        u64 size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&wmessage_buf, 0, NULL);
        if (size) {
            const char* err_message = wcstr_to_cstr(wmessage_buf);
            const char* message = string_format("Window creation failed with error: '%s'.", err_message);
            LocalFree(wmessage_buf); // Free the Win32's string's buffer.

            const WCHAR* wmessage = cstr_to_wcstr(message);
            MessageBoxW(NULL, wmessage, L"Error!", MB_ICONEXCLAMATION | MB_OK);
            KFATAL(message);
            string_free(message);
            wcstr_free(wmessage);
        } else {
            MessageBoxW(0, L"Window registration failed", L"Error", MB_ICONEXCLAMATION | MB_OK);
            KFATAL("Window registration failed!");
        }

        return false;
    }

    return true;
}

void platform_system_shutdown(struct platform_state* state) {
    if (state && state->windows) {
        u32 len = darray_length(state->windows);
        for (u32 i = 0; i < len; ++i) {
            if (state->windows[i]) {
                platform_window_destroy(state->windows[i]);
                state->windows[i] = 0;
            }
        }
        darray_destroy(state->windows);
        state->windows = 0;
    }
}

b8 platform_window_create(const kwindow_config* config, struct kwindow* window, b8 show_immediately) {
    if (!window) {
        return false;
    }

    // Create window
    i32 client_x = config->position_x;
    i32 client_y = config->position_y;
    u32 client_width = config->width;
    u32 client_height = config->height;

    i32 window_x = client_x;
    i32 window_y = client_y;
    i32 window_width = client_width;
    i32 window_height = client_height;

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

    if (config->title) {
        window->title = string_duplicate(config->title);
    } else {
        window->title = string_duplicate("Kohi Game Engine Window");
    }

    window->width = client_width;
    window->height = client_height;

    window->platform_state = kallocate(sizeof(kwindow_platform_state), MEMORY_TAG_UNKNOWN);

    // Convert to wide character string first.
    // LPCWSTR wtitle = cstr_to_wcstr(window->title);
    // FIXME: For some reason using the above causes renderdoc to fail to open the window,
    // but using the below does not...
    WCHAR wtitle[256];
    MultiByteToWideChar(CP_UTF8, 0, window->title, -1, wtitle, 256);
    window->platform_state->hwnd = CreateWindowExW(
        window_ex_style, L"kohi_window_class", wtitle,
        window_style, window_x, window_y, window_width, window_height,
        0, 0, state_ptr->handle.h_instance, 0);
    // wcstr_free(wtitle);

    if (window->platform_state->hwnd == 0) {
        DWORD last_error = GetLastError();
        LPWSTR wmessage_buf = 0;

        // Ask Win32 to give us the string version of that message ID.
        // The parameters we pass in, tell Win32 to create the buffer that holds the message for us
        // (because we don't yet know how long the message string will be).
        u64 size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&wmessage_buf, 0, NULL);
        if (size) {
            const char* err_message = wcstr_to_cstr(wmessage_buf);
            const char* message = string_format("Window creation failed with error: '%s'.", err_message);
            LocalFree(wmessage_buf); // Free the Win32's string's buffer.

            const WCHAR* wmessage = cstr_to_wcstr(message);
            MessageBoxW(NULL, wmessage, L"Error!", MB_ICONEXCLAMATION | MB_OK);
            KFATAL(message);
            string_free(message);
            wcstr_free(wmessage);
        } else {
            MessageBoxW(NULL, L"Window creation failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
            KFATAL("Window creation failed!");
        }

        return false;
    }

    // Register the window internally.
    darray_push(state_ptr->windows, window);

    if (show_immediately) {
        platform_window_show(window);
    }

    return true;
}

void platform_window_destroy(struct kwindow* window) {
    if (window) {
        u32 len = darray_length(state_ptr->windows);
        for (u32 i = 0; i < len; ++i) {
            if (state_ptr->windows[i] == window) {
                KTRACE("Destroying window...");
                DestroyWindow(window->platform_state->hwnd);
                window->platform_state->hwnd = 0;
                state_ptr->windows[i] = 0;
                return;
            }
        }
        KERROR("Destroying a window that was somehow not registered with the platform layer.");
        DestroyWindow(window->platform_state->hwnd);
        window->platform_state->hwnd = 0;
    }
}

b8 platform_window_show(struct kwindow* window) {
    if (!window) {
        return false;
    }
    // Show the window
    b32 should_activate = 1; // TODO: if the window should not accept input, this should be false.
    i32 show_window_command_flags = should_activate ? SW_SHOW : SW_SHOWNOACTIVATE;
    // If initially minimized, use SW_MINIMIZE : SW_SHOWMINNOACTIVE;
    // If initially maximized, use SW_SHOWMAXIMIZED : SW_MAXIMIZE
    ShowWindow(window->platform_state->hwnd, show_window_command_flags);

    return true;
}

b8 platform_window_hide(struct kwindow* window) {
    if (!window) {
        return false;
    }

    // Yep... it's the same function with a flag passed to hide...
    i32 show_window_command_flags = SW_HIDE;
    ShowWindow(window->platform_state->hwnd, show_window_command_flags);

    return true;
}

const char* platform_window_title_get(const struct kwindow* window) {
    if (window && window->title) {
        return string_duplicate(window->title);
    }
    return 0;
}

b8 platform_window_title_set(struct kwindow* window, const char* title) {
    if (!window) {
        return false;
    }

    LPCWSTR wtitle = cstr_to_wcstr(window->title);

    // If the function succeeds, the return value is nonzero.
    b8 result = (SetWindowText(window->platform_state->hwnd, wtitle) != 0);
    wcstr_free(wtitle);
    return result;
}

b8 platform_pump_messages(void) {
    if (state_ptr) {
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    platform_update_watches();
    return true;
}

void* platform_allocate(u64 size, b8 aligned) {
    // return malloc(size);
    return (void*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void platform_free(void* block, b8 aligned) {
    // free(block);
    HeapFree(GetProcessHeap(), 0, block);
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

void platform_console_write(struct platform_state* platform, log_level level, const char* message) {
    b8 is_error = (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_FATAL);
    HANDLE console_handle = GetStdHandle(is_error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (state_ptr) {
        csbi = is_error ? state_ptr->err_output_csbi : state_ptr->std_output_csbi;
    } else {
        GetConsoleScreenBufferInfo(console_handle, &csbi);
    }

    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    static u8 levels[6] = {64, 4, 6, 2, 1, 8};
    SetConsoleTextAttribute(console_handle, levels[level]);
    LPCWSTR wmessage = cstr_to_wcstr(message);
    OutputDebugStringW(wmessage);
    u64 length = lstrlen(wmessage); // strlen(message);
    DWORD number_written = 0;
    WriteConsoleW(console_handle, wmessage, (DWORD)length, &number_written, 0);
    wcstr_free(wmessage);

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

void platform_get_handle_info(u64* out_size, void* memory) {
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
b8 kthread_create(pfn_thread_start start_function_ptr, void* params, b8 auto_detach, kthread* out_thread) {
    if (!start_function_ptr) {
        return false;
    }

    out_thread->internal_data = CreateThread(
        0,
        0,                                          // Default stack size
        (LPTHREAD_START_ROUTINE)start_function_ptr, // function ptr
        params,                                     // param to pass to thread
        0,
        (DWORD*)&out_thread->thread_id);
    KDEBUG("Starting process on thread id: %#x", out_thread->thread_id);
    if (!out_thread->internal_data) {
        return false;
    }
    if (auto_detach) {
        CloseHandle(out_thread->internal_data);
    }
    return true;
}

void kthread_destroy(kthread* thread) {
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

void kthread_detach(kthread* thread) {
    if (thread && thread->internal_data) {
        CloseHandle(thread->internal_data);
        thread->internal_data = 0;
    }
}

void kthread_cancel(kthread* thread) {
    if (thread && thread->internal_data) {
        TerminateThread(thread->internal_data, 0);
        thread->internal_data = 0;
    }
}

b8 kthread_wait(kthread* thread) {
    if (thread && thread->internal_data) {
        DWORD exit_code = WaitForSingleObject(thread->internal_data, INFINITE);
        if (exit_code == WAIT_OBJECT_0) {
            return true;
        }
    }
    return false;
}

b8 kthread_wait_timeout(kthread* thread, u64 wait_ms) {
    if (thread && thread->internal_data) {
        DWORD exit_code = WaitForSingleObject(thread->internal_data, wait_ms);
        if (exit_code == WAIT_OBJECT_0) {
            return true;
        } else if (exit_code == WAIT_TIMEOUT) {
            return false;
        }
    }
    return false;
}

b8 kthread_is_active(kthread* thread) {
    if (thread && thread->internal_data) {
        DWORD exit_code = WaitForSingleObject(thread->internal_data, 0);
        if (exit_code == WAIT_TIMEOUT) {
            return true;
        }
    }
    return false;
}

void kthread_sleep(kthread* thread, u64 ms) {
    platform_sleep(ms);
}

u64 platform_current_thread_id(void) {
    return (u64)GetCurrentThreadId();
}

// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create(kmutex* out_mutex) {
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

void kmutex_destroy(kmutex* mutex) {
    if (mutex && mutex->internal_data) {
        CloseHandle(mutex->internal_data);
        // KTRACE("Destroyed mutex.");
        mutex->internal_data = 0;
    }
}

b8 kmutex_lock(kmutex* mutex) {
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

b8 kmutex_unlock(kmutex* mutex) {
    if (!mutex || !mutex->internal_data) {
        return false;
    }
    i32 result = ReleaseMutex(mutex->internal_data);
    // KTRACE("Mutex unlocked.");
    return result != 0; // 0 is a failure
}

// NOTE: End mutexes.

b8 ksemaphore_create(ksemaphore* out_semaphore, u32 max_count, u32 start_count) {
    if (!out_semaphore) {
        return false;
    }

    out_semaphore->internal_data = CreateSemaphore(0, start_count, max_count, 0);

    return true;
}

void ksemaphore_destroy(ksemaphore* semaphore) {
    if (semaphore && semaphore->internal_data) {
        CloseHandle(semaphore->internal_data);
        KTRACE("Destroyed semaphore handle.");
        semaphore->internal_data = 0;
    }
}

b8 ksemaphore_signal(ksemaphore* semaphore) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }
    // W: release/Increment
    LONG previous_count = 0;
    // NOTE: release 1 at a time.
    if (!ReleaseSemaphore(semaphore->internal_data, 1, &previous_count)) {
        KERROR("Failed to release semaphore.");
        return false;
    }
    return true;
    // L: post/Increment
}

b8 ksemaphore_wait(ksemaphore* semaphore, u64 timeout_ms) {
    if (!semaphore || !semaphore->internal_data) {
        return false;
    }

    DWORD result = WaitForSingleObject(semaphore->internal_data, timeout_ms);
    switch (result) {
    case WAIT_ABANDONED:
        KERROR("The specified object is a mutex object that was not released by the thread that owned the mutex object before the owning thread terminated. Ownership of the mutex object is granted to the calling thread and the mutex state is set to nonsignaled. If the mutex was protecting persistent state information, you should check it for consistency.");
        return false;
    case WAIT_OBJECT_0:
        // The state is signaled.
        return true;
    case WAIT_TIMEOUT:
        KERROR("Semaphore wait timeout occurred.");
        return false;
    case WAIT_FAILED:
        KERROR("WaitForSingleObject failed.");
        // TODO: GetLastError and print message.
        return false;
    default:
        KERROR("An unknown error occurred while waiting on a semaphore.");
        // TODO: GetLastError and print message.
        return false;
    }
    // W: wait/decrement, blocks when 0
    // L: wait/decrement, blocks when 0
    return true;
}

b8 platform_dynamic_library_load(const char* name, dynamic_library* out_library) {
    if (!out_library) {
        return false;
    }
    kzero_memory(out_library, sizeof(dynamic_library));
    if (!name) {
        return false;
    }

    char filename[MAX_PATH];
    kzero_memory(filename, sizeof(char) * MAX_PATH);
    string_format_unsafe(filename, "%s.dll", name);

    LPCWSTR wfilename = cstr_to_wcstr(filename);
    HMODULE library = LoadLibraryW(wfilename);
    wcstr_free(wfilename);
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

b8 platform_dynamic_library_unload(dynamic_library* library) {
    if (!library) {
        return false;
    }

    HMODULE internal_module = (HMODULE)library->internal_data;
    if (!internal_module) {
        return false;
    }

    if (library->name) {
        u64 length = string_length(library->name);
        kfree((void*)library->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->filename) {
        u64 length = string_length(library->filename);
        kfree((void*)library->filename, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
    }

    if (library->functions) {
        u32 count = darray_length(library->functions);
        for (u32 i = 0; i < count; ++i) {
            dynamic_library_function* f = &library->functions[i];
            if (f->name) {
                u64 length = string_length(f->name);
                kfree((void*)f->name, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
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

void* platform_dynamic_library_load_function(const char* name, dynamic_library* library) {
    if (!name || !library) {
        return 0;
    }

    if (!library->internal_data) {
        return 0;
    }

    FARPROC f_addr = GetProcAddress((HMODULE)library->internal_data, name);
    if (!f_addr) {
        return 0;
    }

    dynamic_library_function f = {0};
    f.pfn = f_addr;
    f.name = string_duplicate(name);
    darray_push(library->functions, f);

    return (void*)f_addr;
}

const char* platform_dynamic_library_extension(void) {
    return ".dll";
}

const char* platform_dynamic_library_prefix(void) {
    return "";
}

void platform_register_watcher_deleted_callback(platform_filewatcher_file_deleted_callback callback) {
    state_ptr->watcher_deleted_callback = callback;
}

void platform_register_watcher_written_callback(platform_filewatcher_file_written_callback callback) {
    state_ptr->watcher_written_callback = callback;
}

void platform_register_window_closed_callback(platform_window_closed_callback callback) {
    state_ptr->window_closed_callback = callback;
}

void platform_register_window_resized_callback(platform_window_resized_callback callback) {
    state_ptr->window_resized_callback = callback;
}

void platform_register_process_key(platform_process_key callback) {
    state_ptr->process_key = callback;
}

void platform_register_process_mouse_button_callback(platform_process_mouse_button callback) {
    state_ptr->process_mouse_button = callback;
}

void platform_register_process_mouse_move_callback(platform_process_mouse_move callback) {
    state_ptr->process_mouse_move = callback;
}

void platform_register_process_mouse_wheel_callback(platform_process_mouse_wheel callback) {
    state_ptr->process_mouse_wheel = callback;
}

platform_error_code platform_copy_file(const char* source, const char* dest, b8 overwrite_if_exists) {
    LPCWSTR wsource = cstr_to_wcstr(source);
    LPCWSTR wdest = cstr_to_wcstr(dest);
    BOOL result = CopyFileW(wsource, wdest, !overwrite_if_exists);
    wcstr_free(wsource);
    wcstr_free(wdest);
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

static b8 register_watch(const char* file_path, u32* out_watch_id) {
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

    WIN32_FIND_DATAW data;
    LPCWSTR wfile_path = cstr_to_wcstr(file_path);
    HANDLE file_handle = FindFirstFileW(wfile_path, &data);
    wcstr_free(wfile_path);
    if (file_handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    BOOL result = FindClose(file_handle);
    if (result == 0) {
        return false;
    }

    u32 count = darray_length(state_ptr->watches);
    for (u32 i = 0; i < count; ++i) {
        win32_file_watch* w = &state_ptr->watches[i];
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

    win32_file_watch* w = &state_ptr->watches[watch_id];
    w->id = INVALID_ID;
    u32 len = string_length(w->file_path);
    kfree((void*)w->file_path, sizeof(char) * (len + 1), MEMORY_TAG_STRING);
    w->file_path = 0;
    kzero_memory(&w->last_write_time, sizeof(FILETIME));

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
        win32_file_watch* f = &state_ptr->watches[i];
        if (f->id != INVALID_ID) {
            WIN32_FIND_DATAW data;

            LPCWSTR wfile_path = cstr_to_wcstr(f->file_path);
            HANDLE file_handle = FindFirstFileW(wfile_path, &data);
            wcstr_free(wfile_path);
            if (file_handle == INVALID_HANDLE_VALUE) {
                // This means the file has been deleted, remove from watch.
                if (state_ptr->watcher_deleted_callback) {
                    state_ptr->watcher_deleted_callback(f->id);
                } else {
                    KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_deleted_callback()");
                }
                // event_context context = {0};
                // context.data.u32[0] = f->id;
                // event_fire(EVENT_CODE_WATCHED_FILE_DELETED, 0, context);
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
                // event_context context = {0};
                // context.data.u32[0] = f->id;
                // event_fire(EVENT_CODE_WATCHED_FILE_WRITTEN, 0, context);
                if (state_ptr->watcher_written_callback) {
                    state_ptr->watcher_written_callback(f->id);
                } else {
                    KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_written_callback()");
                }
            }
        }
    }
}

static kwindow* window_from_handle(HWND hwnd) {
    u32 len = darray_length(state_ptr->windows);
    for (u32 i = 0; i < len; ++i) {
        kwindow* w = state_ptr->windows[i];
        if (w && w->platform_state->hwnd == hwnd) {
            return state_ptr->windows[i];
        }
    }
    return 0;
}

LRESULT CALLBACK win32_process_message(HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param) {
    switch (msg) {
    case WM_ERASEBKGND:
        // Notify the OS that erasing will be handled by the application to prevent flicker.
        return 1;
    case WM_CLOSE: {
        if (state_ptr->window_closed_callback) {
            kwindow* w = window_from_handle(hwnd);
            if (!w) {
                KERROR("Recieved a window close event for a non-registered window!");
                return 0;
            }
            state_ptr->window_closed_callback(w);
        }
        return 0;
    }
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
            if (!GetMonitorInfoW(monitor, &monitor_info)) {
                KWARN("Failed to get monitor info. ");
            }

            KINFO("monitor: %u", monitor_info.rcMonitor.left);
        }

        // Fire the event. The application layer should pick this up, but not handle it
        // as it shouldn't be visible to other parts of the application.
        kwindow* w = window_from_handle(hwnd);
        if (!w) {
            KERROR("Recieved a window resize event for a non-registered window!");
            return 0;
        }

        // Check if different. If so, trigger a resize event.
        if (width != w->width || height != w->height) {
            // Flag as resizing and store the change, but wait to regenerate.
            w->resizing = true;
            // Also reset the frame count since the last  resize operation.
            w->frames_since_resize = 0;
            // Update dimensions
            w->width = width;
            w->height = height;

            // Only trigger the callback if there was an actual change.
            state_ptr->window_resized_callback(w);
        }
    } break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        if (state_ptr->process_key) {
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

            // HACK: This is gross windows keybind crap.
            if (key == VK_OEM_1) {
                key = KEY_SEMICOLON;
            }

            // Pass to the input subsystem for processing.
            state_ptr->process_key(key, pressed);

            // Return 0 to prevent default window behaviour for some keypresses, such as alt.
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (state_ptr->process_mouse_move) {
            // Mouse move
            i32 x_position = GET_X_LPARAM(l_param);
            i32 y_position = GET_Y_LPARAM(l_param);

            // Pass over to the input subsystem.
            state_ptr->process_mouse_move(x_position, y_position);
        }
    } break;
    case WM_MOUSEWHEEL: {
        if (state_ptr->process_mouse_wheel) {
            i32 z_delta = GET_WHEEL_DELTA_WPARAM(w_param);
            if (z_delta != 0) {
                // Flatten the input to an OS-independent (-1, 1)
                z_delta = (z_delta < 0) ? -1 : 1;
                state_ptr->process_mouse_wheel(z_delta);
            }
        }
    } break;
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP: {
        if (state_ptr->process_mouse_button) {
            b8 pressed = msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN;
            mouse_buttons mouse_button = MOUSE_BUTTON_MAX;
            switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                mouse_button = MOUSE_BUTTON_LEFT;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                mouse_button = MOUSE_BUTTON_MIDDLE;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                mouse_button = MOUSE_BUTTON_RIGHT;
                break;
            }

            // Pass over to the input subsystem.
            if (mouse_button != MOUSE_BUTTON_MAX) {
                state_ptr->process_mouse_button(mouse_button, pressed);
            }
        }
    } break;
    case WM_SHOWWINDOW: {
        // NOTE: Prevent the white flash by working around the way Windows shows the window.
        // This makes the window visible, but transparent, then paints the black background brush,
        // and finally makes the window opaque again.
        if (!GetLayeredWindowAttributes(hwnd, NULL, NULL, NULL)) {
            SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            DefWindowProc(hwnd, WM_ERASEBKGND, (WPARAM)GetDC(hwnd), l_param);
            SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            AnimateWindow(hwnd, 200, AW_ACTIVATE | AW_BLEND);
            return 0;
        }
    } break;
    }

    return DefWindowProcW(hwnd, msg, w_param, l_param);
}

static LPCWSTR cstr_to_wcstr(const char* str) {
    if (!str) {
        return 0;
    }

    i32 len = MultiByteToWideChar(CP_UTF8, 0, str, -1, 0, 0);
    if (len == 0) {
        return 0;
    }
    wchar_t* wstr = kallocate(sizeof(wchar_t) * (len), MEMORY_TAG_STRING);
    if (!wstr) {
        return 0;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len) == 0) {
        kfree((wchar_t*)wstr, sizeof(wchar_t) * (len), MEMORY_TAG_STRING);
        return 0;
    }
    return wstr;
}

static void wcstr_free(LPCWSTR wstr) {
    if (wstr) {
        u32 len = lstrlen(wstr);
        kfree((WCHAR*)wstr, sizeof(WCHAR) * len, MEMORY_TAG_STRING);
    }
}

static const char* wcstr_to_cstr(LPCWSTR wstr) {
    if (!wstr) {
        return 0;
    }

    i32 length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (length == 0) {
        return 0;
    }
    char* str = kallocate(sizeof(WCHAR) * length, MEMORY_TAG_STRING);
    if (!str) {
        return 0;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, length, NULL, NULL) == 0) {
        kfree((char*)str, sizeof(char) * (length + 1), MEMORY_TAG_STRING);
        return 0;
    }

    return str;
}

#endif // KPLATFORM_WINDOWS
