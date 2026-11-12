/**
 * @file platform_win32.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "platform/platform.h"

// Windows platform layer.
#if KPLATFORM_WINDOWS

#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	include <windowsx.h> // param input extraction
// NOTE: These must be included after above windows includes.
#	include <stdlib.h>
#	include <timeapi.h>
#	include <intrin.h>
#	include <debugapi.h>
#	include <minwindef.h>
#	include <sysinfoapi.h>
#	include <winnt.h>
#	include <winreg.h>
#	include <stdint.h>
#	include <string.h>
#	include <stringapiset.h>
#	include <wchar.h>
#	include <winbase.h>
#	include <winnls.h>
#	include <winuser.h>
#	include <commdlg.h>
#	include <fileapi.h>
#	include <handleapi.h>

// DirectInput
/* #	define _WIN32_WINNT 0x0501 */
#	define INITGUID
#	define DIRECTINPUT_VERSION 0x0800
#	include <libloaderapi.h>
#	include <Dbt.h>
#	include <RegStr.h>
#	include <dinput.h>
#	include <combaseapi.h>
#	include <objbase.h>
#	include <winerror.h>
#	define INPUT_QUEUE_SIZE 32
#	define AXIS_MIN -32768
#	define AXIS_MAX 32767

// HID devices
#	include <hidusage.h>
#	include <hidsdi.h>

#	include "containers/darray.h"
#	include "logger.h"
#	include "memory/kmemory.h"
#	include "strings/kstring.h"
#	include "threads/kmutex.h"
#	include "threads/ksemaphore.h"
#	include "threads/kthread.h"
#	include "time/kclock.h"
#	include "platform/kfeatures_runtime.h"
#	include "input_types.h"
#	include "math/kmath.h"
#	include "utils/input_utils.h"

#	define UNIX_EPOCH_TO_FILETIME_NS 116444736000000000ULL
#	define FILETIME_TICK_NS 100ULL

#	pragma comment(lib, "advapi32.lib")
#	pragma comment(lib, "kernel32.lib")
#	pragma comment(lib, "ntdll.lib")
#	pragma comment(lib, "comdlg32.lib")
#	pragma comment(lib, "hid.lib")

typedef LONG NTSTATUS;

/* typedef struct _RTL_OSVERSIONINFOW {
  ULONG dwOSVersionInfoSize;
  ULONG dwMajorVersion;
  ULONG dwMinorVersion;
  ULONG dwBuildNumber;
  ULONG dwPlatformId;
  WCHAR szCSDVersion[128];
} RTL_OSVERSIONINFOW; */

__declspec(dllimport)
NTSTATUS NTAPI
RtlGetVersion (RTL_OSVERSIONINFOW *lpVersionInformation);

typedef struct win32_handle_info {
	HINSTANCE h_instance;
} win32_handle_info;

typedef struct win32_file_watch {
	u32 id;
	const char *file_path;
	b8 is_binary;
	platform_filewatcher_file_written_callback watcher_written_callback;
	void *watcher_written_context;
	platform_filewatcher_file_deleted_callback watcher_deleted_callback;
	void *watcher_deleted_context;
	FILETIME last_write_time;
} win32_file_watch;

typedef struct kwindow_platform_state {
	HWND hwnd;
	f32 device_pixel_ratio;
} kwindow_platform_state;

#	define PLATFORM_GAMEPAD_MAX_AXES 128
#	define PLATFORM_GAMEPAD_MAX_BUTTONS 128

typedef struct diAxisInfo {
	DWORD offset;
	b8 is_pov;
	b8 is_pov_second_axis;
	gamepad_axis axis;
} diAxisInfo;

typedef HRESULT(WINAPI *PFN_DirectInput8Create)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);

typedef struct platform_gamepad_device {
	u32 device_id;
	char *description;
	i32 vendor_id;
	i32 product_id;
	u8 axis_count;
	u8 button_count;
	f32 axis_states[PLATFORM_GAMEPAD_MAX_AXES];
	u32 button_states;

	// DirectInput
	GUID guid_instance;
	IDirectInputDevice8 *device_interface;
	b8 buffered;
	u32 slider_count;
	u32 pov_count;
	diAxisInfo *axis_info;
	DWORD *button_offsets;
	gamepad_button *buttons; // maps alongside button_offsets

} platform_gamepad_device;

typedef enum platform_gamepad_state_flags {
	PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT = 0x01,
	PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT = 0x02,
	PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED = 0x04,
	PLATFORM_GAMEPAD_STATE_DIRECTINPUT_AVAILABLE = 0x08,
} platform_gamepad_state_flags;

typedef u32 platform_gamepad_state_flag_bits;

typedef struct __attribute__((aligned(8))) platform_gamepad_state {
	HRESULT co_initialized;
	HDEVNOTIFY h_notify;

	// darray
	platform_gamepad_device **devices;
	u32 next_device_id;

	platform_gamepad_state_flag_bits flags;

	HMODULE direct_input_lib;
	LPDIRECTINPUT direct_input_iface;
	PFN_DirectInput8Create kDirectInput8Create;

	PRAWINPUTDEVICELIST raw_dev_list;
	UINT raw_dev_list_count;

	f64 start_time;
} platform_gamepad_state;

typedef struct platform_state {
	WNDCLASSEXW wc;
	win32_handle_info handle;
	CONSOLE_SCREEN_BUFFER_INFO std_output_csbi;
	CONSOLE_SCREEN_BUFFER_INFO err_output_csbi;
	// darray
	win32_file_watch *watches;

	// darray of pointers to created windows (owned by the application);
	kwindow **windows;
	platform_window_closed_callback window_closed_callback;
	platform_window_resized_callback window_resized_callback;
	platform_process_key process_key;
	platform_process_mouse_button process_mouse_button;
	platform_process_mouse_move process_mouse_move;
	platform_process_mouse_wheel process_mouse_wheel;
	platform_clipboard_on_paste_callback on_paste;

	platform_gamepad_callbacks gamepad_callbacks;
	platform_gamepad_state *gamepad_state;

	// Clock
	f64 tick_duration;
	UINT min_period;
	LARGE_INTEGER start_time;
} platform_state;

static platform_state *state_ptr = KNULL;

static void platform_update_watches (void);
LRESULT CALLBACK win32_process_message (HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param);
static LPCWSTR cstr_to_wcstr (const char *str);
static const char *wcstr_to_cstr (LPCWSTR wstr);
static void detect_gamepad_devices (platform_state *state);
static void process_gamepad_events (platform_state *state);
static platform_gamepad_device *get_gamepad_device_by_id (u32 device_id);
static gamepad_axis gamepad_get_axis (u16 vendor_id, u16 product_id, u8 axis_id);
static gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u8 button_id);

static b8 register_raw_input_device (HWND hwnd) {
	RAWINPUTDEVICE *device = kallocate_aligned(sizeof(RAWINPUTDEVICE), 32, MEMORY_TAG_PLATFORM);
	device->usUsagePage = HID_USAGE_PAGE_GENERIC; // possibly HID_USAGE_PAGE_GAME ?
	device->usUsage = HID_USAGE_GENERIC_KEYBOARD;
	device->dwFlags = RIDEV_DEVNOTIFY;
	device->hwndTarget = hwnd;

	b8 result = RegisterRawInputDevices(device, 1, sizeof(*device));

	return result;
}
typedef struct win32_device_info {
	char manufacturer[128];
	char product[128];
	input_device_type type;
	u16 version;
	u16 vendor_id;
	u16 product_id;
	union {
		struct {
			u8 button_count;
			u32 sample_rate;
			b8 has_horizontal_wheel;
		} mouse;
		struct {
			u32 key_count;
			u32 fn_key_count;
			u32 mode;
		} keyboard;
		struct {
			u32 usage_page;
			u32 usage;
		} hid;
	};
} win32_device_info;

b8 get_hid_device_info (const WCHAR *device_name, win32_device_info *out_device_info) {
	if (!device_name || !out_device_info) {
		return false;
	}

	kzero_memory(out_device_info, sizeof(win32_device_info));

	HANDLE file = CreateFileW(device_name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, KNULL, OPEN_EXISTING, 0, KNULL);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	WCHAR wbuf[128];
	BOOLEAN got_prod = HidD_GetProductString(file, wbuf, sizeof(wbuf));
	if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out_device_info->product, 126, NULL, NULL) == 0) {
		return false;
	}
	kzero_memory(wbuf, sizeof(wbuf));

	BOOLEAN got_manu = HidD_GetManufacturerString(file, wbuf, sizeof(wbuf));
	if (WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out_device_info->manufacturer, 126, NULL, NULL) == 0) {
		return false;
	}

	HIDD_ATTRIBUTES attr = {0};
	BOOLEAN got_attr = HidD_GetAttributes(file, &attr);
	if (got_attr) {
		out_device_info->version = attr.VersionNumber;
		out_device_info->vendor_id = attr.VendorID;
		out_device_info->product_id = attr.ProductID;
	}

	CloseHandle(file);

	return (got_prod || got_manu || got_attr);
}

void clock_setup (void) {
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	state_ptr->tick_duration = 1.0 / (f64)frequency.QuadPart;
	QueryPerformanceCounter(&state_ptr->start_time);

	TIMECAPS tc;
	timeGetDevCaps(&tc, sizeof(tc));
	state_ptr->min_period = tc.wPeriodMin;
}

b8 platform_system_startup (u64 *memory_requirement, platform_state *state, platform_system_config *config) {
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
	state->windows = darray_create(kwindow *);
	state->process_key = 0;
	state->process_mouse_button = 0;
	state->process_mouse_move = 0;
	state->process_mouse_wheel = 0;

	// Only available in the Creators update for Windows 10+.
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	// NOTE: Older versions of windows might have to use this:
	// SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);

	// Setup and register window class. This can be done at platform init and be reused.
	HICON icon = LoadIconW(state_ptr->handle.h_instance, IDI_APPLICATION);
	kzero_memory(&state_ptr->wc, sizeof(WNDCLASSEXW));
	state->wc.cbSize = sizeof(WNDCLASSEXW);
	state->wc.style = CS_DBLCLKS; // Get double-clicks
	state->wc.lpfnWndProc = win32_process_message;
	state->wc.cbClsExtra = 0;
	state->wc.cbWndExtra = 0;
	state->wc.hInstance = state_ptr->handle.h_instance;
	state->wc.hIcon = icon;
	state->wc.hIconSm = icon;
	state->wc.hCursor = LoadCursorW(NULL, IDC_ARROW); // NULL; // Manage the cursor manually
	state->wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	state->wc.lpszClassName = L"kohi_window_class";
	state->wc.lpszMenuName = 0;

	if (!RegisterClassExW(&state->wc)) {
		DWORD last_error = GetLastError();
		LPWSTR wmessage_buf = 0;

		// Ask Win32 to give us the string version of that message ID.
		// The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
		u64 size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
								  NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&wmessage_buf, 0, NULL);
		if (size) {
			const char *err_message = wcstr_to_cstr(wmessage_buf);
			const char *message = string_format("Window creation failed with error: '%s'.", err_message);
			LocalFree(wmessage_buf); // Free the Win32's string's buffer.

			const WCHAR *wmessage = cstr_to_wcstr(message);
			MessageBoxW(NULL, wmessage, L"Error!", MB_ICONEXCLAMATION | MB_OK);
			KFATAL(message);
			string_free(message);
			kfree(wmessage);
		} else {
			MessageBoxW(0, L"Window registration failed", L"Error", MB_ICONEXCLAMATION | MB_OK);
			KFATAL("Window registration failed!");
		}

		return false;
	}

	return true;
}

void platform_system_shutdown (struct platform_state *state) {
	if (state) {
		// Gamepad shutdown.
		if (state->gamepad_state) {
			kzero_memory(&state->gamepad_callbacks, sizeof(state->gamepad_callbacks));
			if (FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
				if (state->gamepad_state->h_notify) {
					UnregisterDeviceNotification(state->gamepad_state->h_notify);
					state->gamepad_state->h_notify = KNULL;
				}
				UnregisterClass(state->wc.lpszClassName, state->wc.hInstance);
				if (state->gamepad_state->co_initialized == S_OK) {
					CoUninitialize();
				}
			}
		}

		if (state->windows) {
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
}

b8 platform_window_create (const kwindow_config *config, struct kwindow *window, b8 show_immediately) {
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
	window->device_pixel_ratio = 1.0f;

	window->platform_state = kallocate(sizeof(kwindow_platform_state), MEMORY_TAG_PLATFORM);

	// Convert to wide character string first.
	LPCWSTR wtitle = cstr_to_wcstr(window->title);
	window->platform_state->hwnd = CreateWindowExW(
		window_ex_style, L"kohi_window_class", wtitle,
		window_style, window_x, window_y, window_width, window_height,
		0, 0, state_ptr->handle.h_instance, 0);
	kfree(wtitle);

	if (window->platform_state->hwnd == 0) {
		DWORD last_error = GetLastError();
		LPWSTR wmessage_buf = 0;

		// Ask Win32 to give us the string version of that message ID.
		// The parameters we pass in, tell Win32 to create the buffer that holds the message for us
		// (because we don't yet know how long the message string will be).
		u64 size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
								  NULL, last_error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&wmessage_buf, 0, NULL);
		if (size) {
			const char *err_message = wcstr_to_cstr(wmessage_buf);
			const char *message = string_format("Window creation failed with error: '%s'.", err_message);
			LocalFree(wmessage_buf); // Free the Win32's string's buffer.

			const WCHAR *wmessage = cstr_to_wcstr(message);
			MessageBoxW(NULL, wmessage, L"Error!", MB_ICONEXCLAMATION | MB_OK);
			KFATAL(message);
			string_free(message);
			kfree(wmessage);
		} else {
			MessageBoxW(NULL, L"Window creation failed!", L"Error!", MB_ICONEXCLAMATION | MB_OK);
			KFATAL("Window creation failed!");
		}

		return false;
	}

	// Register the window internally.
	darray_push(state_ptr->windows, &window);

	if (show_immediately) {
		platform_window_show(window);
	}

	return true;
}

void platform_window_destroy (struct kwindow *window) {
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

b8 platform_window_show (struct kwindow *window) {
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

b8 platform_window_hide (struct kwindow *window) {
	if (!window) {
		return false;
	}

	// Yep... it's the same function with a flag passed to hide...
	i32 show_window_command_flags = SW_HIDE;
	ShowWindow(window->platform_state->hwnd, show_window_command_flags);

	return true;
}

const char *platform_window_title_get (const struct kwindow *window) {
	if (window && window->title) {
		return string_duplicate(window->title);
	}
	return 0;
}

b8 platform_window_title_set (struct kwindow *window, const char *title) {
	if (!window) {
		return false;
	}

	LPCWSTR wtitle = cstr_to_wcstr(title);
	// If the function succeeds, the return value is nonzero.
	b8 result = (SetWindowTextW(window->platform_state->hwnd, wtitle) != 0);
	kfree(wtitle);
	return result;
}

b8 platform_pump_messages (void) {
	if (state_ptr) {
		MSG message;
		while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
	}
	platform_update_watches();

	// Check every so often for new controllers.
	static u32 next_poll = 0;
	next_poll++;
	if (next_poll >= 30) {
		detect_gamepad_devices(state_ptr);
		next_poll = 0;
	}

	// Also process gamepad events.
	process_gamepad_events(state_ptr);
	return true;
}

void *platform_allocate (u64 size, b8 aligned) {
	return (void *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

void platform_free (const void *block, b8 aligned, u64 size) {
	HeapFree(GetProcessHeap(), 0, (void *)block);
}

void platform_console_write (struct platform_state *platform, log_level level, const char *message) {
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
	kfree(wmessage);

	SetConsoleTextAttribute(console_handle, csbi.wAttributes);
}

f64 platform_get_absolute_time (void) {
	if (!state_ptr->tick_duration) {
		clock_setup();
	}

	LARGE_INTEGER now_time;
	QueryPerformanceCounter(&now_time);
	return (f64)(now_time.QuadPart - state_ptr->start_time.QuadPart) * state_ptr->tick_duration;
}

void platform_sleep (u64 ms) {
	kclock clock;
	kclock_start(&clock);
	timeBeginPeriod(state_ptr->min_period);
	Sleep(ms - state_ptr->min_period);
	timeEndPeriod(state_ptr->min_period);

	kclock_update(&clock);
	f64 observed = clock.elapsed * 1000.0;
	f64 ms_remaining = (f64)ms - observed;

	// spin lock
	kclock_start(&clock);
	while (clock.elapsed * 1000.0 < ms_remaining) {
		_mm_pause();
		kclock_update(&clock);
	}
}

i32 platform_get_processor_count (void) {
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	KINFO("%i processor cores detected.", sysinfo.dwNumberOfProcessors);
	return sysinfo.dwNumberOfProcessors;
}

void platform_get_handle_info (u64 *out_size, void *memory) {
	*out_size = sizeof(win32_handle_info);
	if (!memory) {
		return;
	}

	kcopy_memory(memory, &state_ptr->handle, *out_size);
}

f32 platform_device_pixel_ratio (const struct kwindow *window) {
	return window->platform_state->device_pixel_ratio;
}

typedef struct kthread_work_info {
	pfn_thread_start start;
	void *params;
} kthread_work_info;

static DWORD kthread_do_work (void *params) {
	kthread_work_info *info = params;
	if (!info->start(info->params)) {
		KERROR("Failed to start kthread due to failure in user callback. Check logs for details");
		// Nonzero exit code indicates failure.
		return 1;
	}

	// Always free params at the end.
	kfree(params);

	// Zero exit code indicates success.
	return 0;
}

// NOTE: Begin threads
b8 kthread_create (pfn_thread_start start_function_ptr, void *params, b8 auto_detach, kthread *out_thread) {
	if (!start_function_ptr) {
		return false;
	}

	kthread_work_info *info = KALLOC_TYPE(kthread_work_info, MEMORY_TAG_PLATFORM);
	info->params = params,
	info->start = start_function_ptr;

	out_thread->internal_data = CreateThread(
		0,
		0,										 // Default stack size
		(LPTHREAD_START_ROUTINE)kthread_do_work, // function ptr
		info,									 // param to pass to thread
		0,
		(DWORD *)&out_thread->thread_id);
	/* KDEBUG("Starting process on thread id: %#x", out_thread->thread_id); */
	if (!out_thread->internal_data) {
		return false;
	}
	if (auto_detach) {
		CloseHandle(out_thread->internal_data);
		out_thread->internal_data = KNULL;
	}
	return true;
}

void kthread_destroy (kthread *thread) {
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

void kthread_detach (kthread *thread) {
	if (thread && thread->internal_data) {
		CloseHandle(thread->internal_data);
		thread->internal_data = 0;
	}
}

void kthread_cancel (kthread *thread) {
	if (thread && thread->internal_data) {
		TerminateThread(thread->internal_data, 0);
		thread->internal_data = 0;
	}
}

b8 kthread_wait (kthread *thread) {
	if (thread && thread->internal_data) {
		DWORD exit_code = WaitForSingleObject(thread->internal_data, INFINITE);
		if (exit_code == WAIT_OBJECT_0) {
			return true;
		}
	}
	return false;
}

b8 kthread_wait_timeout (kthread *thread, u64 wait_ms) {
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

b8 kthread_is_active (kthread *thread) {
	if (thread && thread->internal_data) {
		DWORD exit_code = WaitForSingleObject(thread->internal_data, 0);
		if (exit_code == WAIT_TIMEOUT) {
			return true;
		}
	}
	return false;
}

void kthread_sleep (kthread *thread, u64 ms) {
	platform_sleep(ms);
}

u64 platform_current_thread_id (void) {
	return (u64)GetCurrentThreadId();
}

// NOTE: End threads.

// NOTE: Begin mutexes
b8 kmutex_create (kmutex *out_mutex) {
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

void kmutex_destroy (kmutex *mutex) {
	if (mutex && mutex->internal_data) {
		WaitForSingleObject(mutex->internal_data, INFINITE);
		CloseHandle(mutex->internal_data);
		mutex->internal_data = KNULL;
		KTRACE("Destroyed mutex.");
	}
}

b8 kmutex_lock (kmutex *mutex) {
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

b8 kmutex_unlock (kmutex *mutex) {
	if (!mutex || !mutex->internal_data) {
		return false;
	}
	i32 result = ReleaseMutex(mutex->internal_data);
	// KTRACE("Mutex unlocked.");
	return result != 0; // 0 is a failure
}

// NOTE: End mutexes.

b8 ksemaphore_create (ksemaphore *out_semaphore, u32 max_count, u32 start_count) {
	if (!out_semaphore) {
		return false;
	}

	out_semaphore->internal_data = CreateSemaphore(0, start_count, max_count, 0);

	return true;
}

void ksemaphore_destroy (ksemaphore *semaphore) {
	if (semaphore && semaphore->internal_data) {
		CloseHandle(semaphore->internal_data);
		KTRACE("Destroyed semaphore handle.");
		semaphore->internal_data = 0;
	}
}

b8 ksemaphore_signal (ksemaphore *semaphore) {
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

b8 ksemaphore_wait (ksemaphore *semaphore, u64 timeout_ms) {
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

b8 platform_dynamic_library_load (const char *name, dynamic_library *out_library) {
	if (!out_library) {
		return false;
	}
	kzero_memory(out_library, sizeof(dynamic_library));
	if (!name) {
		return false;
	}

	out_library->filename = string_format("%s.dll", name);

	LPCWSTR wfilename = cstr_to_wcstr(out_library->filename);
	HMODULE library = LoadLibraryW(wfilename);
	kfree(wfilename);
	if (!library) {
		return false;
	}

	out_library->name = string_duplicate(name);

	out_library->internal_data_size = sizeof(HMODULE);
	out_library->internal_data = library;

	out_library->functions = darray_create(dynamic_library_function);

	return true;
}

b8 platform_dynamic_library_unload (dynamic_library *library) {
	if (!library) {
		return false;
	}

	HMODULE internal_module = (HMODULE)library->internal_data;
	if (!internal_module) {
		return false;
	}

	if (library->name) {
		kfree(library->name);
	}

	if (library->filename) {
		kfree(library->filename);
	}

	if (library->functions) {
		u32 count = darray_length(library->functions);
		for (u32 i = 0; i < count; ++i) {
			dynamic_library_function *f = &library->functions[i];
			if (f->name) {
				kfree(f->name);
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

void *platform_dynamic_library_load_function (const char *name, dynamic_library *library) {
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
	darray_push(library->functions, &f);

	return (void *)f_addr;
}

const char *platform_dynamic_library_extension (void) {
	return ".dll";
}

const char *platform_dynamic_library_prefix (void) {
	return "";
}

void platform_register_window_closed_callback (platform_window_closed_callback callback) {
	state_ptr->window_closed_callback = callback;
}

void platform_register_window_resized_callback (platform_window_resized_callback callback) {
	state_ptr->window_resized_callback = callback;
}

void platform_register_process_key (platform_process_key callback) {
	state_ptr->process_key = callback;
}

void platform_register_process_mouse_button_callback (platform_process_mouse_button callback) {
	state_ptr->process_mouse_button = callback;
}

void platform_register_process_mouse_move_callback (platform_process_mouse_move callback) {
	state_ptr->process_mouse_move = callback;
}

void platform_register_process_mouse_wheel_callback (platform_process_mouse_wheel callback) {
	state_ptr->process_mouse_wheel = callback;
}

void platform_register_clipboard_paste_callback (platform_clipboard_on_paste_callback callback) {
	state_ptr->on_paste = callback;
}

void platform_register_gamepad_callbacks (platform_gamepad_callbacks callbacks) {
	state_ptr->gamepad_callbacks = callbacks;
}

platform_error_code platform_copy_file (const char *source, const char *dest, b8 overwrite_if_exists) {
	LPCWSTR wsource = cstr_to_wcstr(source);
	LPCWSTR wdest = cstr_to_wcstr(dest);
	BOOL result = CopyFileW(wsource, wdest, !overwrite_if_exists);
	kfree(wsource);
	kfree(wdest);
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

static b8 register_watch (
	const char *file_path,
	b8 is_binary,
	platform_filewatcher_file_written_callback watcher_written_callback,
	void *watcher_written_context,
	platform_filewatcher_file_deleted_callback watcher_deleted_callback,
	void *watcher_deleted_context,
	u32 *out_watch_id) {

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
	kfree(wfile_path);
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
	w.watcher_written_callback = watcher_written_callback;
	w.watcher_written_context = watcher_written_context;
	w.watcher_deleted_callback = watcher_deleted_callback;
	w.watcher_deleted_context = watcher_deleted_context;
	*out_watch_id = count;
	darray_push(state_ptr->watches, &w);

	return true;
}

static b8 unregister_watch (u32 watch_id) {
	if (!state_ptr || !state_ptr->watches) {
		return false;
	}

	u32 count = darray_length(state_ptr->watches);
	if (count == 0 || watch_id > (count - 1)) {
		return false;
	}

	win32_file_watch *w = &state_ptr->watches[watch_id];
	w->id = INVALID_ID;
	kfree(w->file_path);
	w->file_path = 0;
	kzero_memory(&w->last_write_time, sizeof(FILETIME));

	return true;
}

b8 platform_watch_file (
	const char *file_path,
	b8 is_binary,
	platform_filewatcher_file_written_callback watcher_written_callback,
	void *watcher_written_context,
	platform_filewatcher_file_deleted_callback watcher_deleted_callback,
	void *watcher_deleted_context,
	u32 *out_watch_id) {
	return register_watch(
		file_path,
		is_binary,
		watcher_written_callback,
		watcher_written_context,
		watcher_deleted_callback,
		watcher_deleted_context,
		out_watch_id);
}

b8 platform_unwatch_file (u32 watch_id) {
	return unregister_watch(watch_id);
}

static void platform_update_watches (void) {
	if (!state_ptr || !state_ptr->watches) {
		return;
	}

	u32 count = darray_length(state_ptr->watches);
	for (u32 i = 0; i < count; ++i) {
		win32_file_watch *f = &state_ptr->watches[i];
		if (f->id != INVALID_ID) {
			WIN32_FIND_DATAW data;

			LPCWSTR wfile_path = cstr_to_wcstr(f->file_path);
			HANDLE file_handle = FindFirstFileW(wfile_path, &data);
			kfree(wfile_path);
			if (file_handle == INVALID_HANDLE_VALUE) {
				// This means the file has been deleted, remove from watch.
				if (f->watcher_deleted_callback) {
					f->watcher_deleted_callback(f->id, f->watcher_written_context);
				} else {
					KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_deleted_callback()");
				}
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
				if (f->watcher_written_callback) {
					f->watcher_written_callback(f->id, f->file_path, f->is_binary, f->watcher_written_context);
				} else {
					KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_written_callback()");
				}
			}
		}
	}
}

static inline kunix_time_ns
unix_time_from_filetime (FILETIME ft) {
	uint64_t ticks =
		((uint64_t)ft.dwHighDateTime << 32) |
		(uint64_t)ft.dwLowDateTime;

	uint64_t ns = ticks * FILETIME_TICK_NS;
	return ns - UNIX_EPOCH_TO_FILETIME_NS;
}

static inline FILETIME
filetime_from_unix_time (kunix_time_ns t) {
	uint64_t ns = t + UNIX_EPOCH_TO_FILETIME_NS;
	uint64_t ticks = ns / FILETIME_TICK_NS;

	FILETIME ft;
	ft.dwLowDateTime = (DWORD)(ticks & 0xFFFFFFFF);
	ft.dwHighDateTime = (DWORD)(ticks >> 32);
	return ft;
}

kunix_time_ns platform_get_file_mtime (const char *path) {
	WIN32_FILE_ATTRIBUTE_DATA data;

	LPCWSTR wfile_path = cstr_to_wcstr(path);

	kunix_time_ns ret = 0;
	if (GetFileAttributesExW(wfile_path, GetFileExInfoStandard, &data)) {
		ret = unix_time_from_filetime(data.ftLastWriteTime);
	}

	kfree(wfile_path);

	return ret;
}

static const char *windows_product_to_string (DWORD p) {
	switch (p) {
	case PRODUCT_PROFESSIONAL:
		return "Pro";
	case PRODUCT_ENTERPRISE:
		return "Enterprise";
	case PRODUCT_EDUCATION:
		return "Education";
	case PRODUCT_CORE:
		return "Home";
	case PRODUCT_CORE_SINGLELANGUAGE:
		return "Home Single Language";
	case PRODUCT_CORE_COUNTRYSPECIFIC:
		return "Home China";
	case PRODUCT_PRO_WORKSTATION:
		return "Pro for Workstations";

	default:
		return "Unknown";
	}
}

static u32 windows_get_physical_core_count (void) {
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
	DWORD returnLength = 0;
	BOOL done = FALSE;
	DWORD coreCount = 0;

	while (!done) {
		// First call to get the buffer size
		DWORD rc = GetLogicalProcessorInformation(buffer, &returnLength);

		if (rc == FALSE) {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				// Allocate memory for the buffer
				if (buffer)
					free(buffer);
				buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION)malloc(returnLength);
				if (buffer == NULL) {
					// Handle error
					break;
				}
			} else {
				// Handle other errors
				break;
			}
		} else {
			done = TRUE;
		}
	}

	if (buffer != NULL) {
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = buffer;
		DWORD byteOffset = 0;

		while (byteOffset < returnLength) {
			if (ptr->Relationship == RelationProcessorCore) {
				// Count the number of physical cores
				coreCount++;
			}
			byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
			ptr++;
		}

		free(buffer);
	}

	return coreCount;
}

static u32 windows_get_ram_speed_mhz (void) {
	DWORD size = GetSystemFirmwareTable('RSMB', 0, NULL, 0);
	if (!size) {
		return 0;
	}

	BYTE *buf = (BYTE *)kallocate(size, MEMORY_TAG_PLATFORM);
	if (!buf) {
		return 0;
	}

	if (!GetSystemFirmwareTable('RSMB', 0, buf, size)) {
		kfree(buf);
		return 0;
	}

	BYTE *p = buf;
	BYTE *end = buf + size;

	while (p + 4 <= end) {
		BYTE type = p[0];
		BYTE len = p[1];

		if (type == 17 && len >= 0x15) { // Memory Device
			u16 speed = *(u16 *)(p + 0x15);
			if (speed != 0 && speed != 0xFFFF) {
				kfree(buf);
				return speed;
			}
		}

		// Skip structure + string-set
		p += len;
		while (p + 1 < end && (p[0] || p[1])) {
			p++;
		}
		p += 2;
	}

	kfree(buf);
	return 0;
}
static u32 windows_get_cpu_base_clock_mhz (void) {
	HKEY key;
	DWORD mhz = 0;
	DWORD size = sizeof(DWORD);

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) != ERROR_SUCCESS) {
		return 0;
	}

	RegQueryValueExA(
		key,
		"~MHz",
		NULL,
		NULL,
		(LPBYTE)&mhz,
		&size);

	RegCloseKey(key);
	return mhz;
}

static void windows_query_storage (ksystem_info *s) {
	s->storage_count = 0;

	DWORD drives = GetLogicalDrives();
	if (drives == 0) {
		return;
	}

	for (char letter = 'A'; letter <= 'Z'; ++letter) {
		if (!(drives & (1 << (letter - 'A')))) {
			continue;
		}

		char root[] = "A:\\";
		root[0] = letter;

		UINT type = GetDriveTypeA(root);
		if (type == DRIVE_NO_ROOT_DIR || type == DRIVE_UNKNOWN) {
			continue;
		}

		ULARGE_INTEGER free_bytes, total_bytes, total_free;
		if (!GetDiskFreeSpaceExA(root, &free_bytes, &total_bytes, &total_free)) {
			continue;
		}

		if (s->storage_count >= KMAX_STORAGE_DEVICES) {
			break;
		}
		kstorage_info *info = &s->storage[s->storage_count++];

		info->name[0] = letter;
		info->name[1] = '\0';
		strcpy(info->mount_point, root);
		info->total_bytes = total_bytes.QuadPart;
		info->free_bytes = free_bytes.QuadPart;
		info->type = type;
	}
}

b8 platform_system_info_collect (ksystem_info *out_info) {
	// CPU
	HKEY key;
	RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key);

	DWORD size = sizeof(out_info->cpu_name);
	RegQueryValueExA(key, "ProcessorNameString", KNULL, KNULL, (LPBYTE)out_info->cpu_name, &size);
	RegCloseKey(key);

	SYSTEM_INFO info;
	GetSystemInfo(&info);
	out_info->logical_cores = info.dwNumberOfProcessors;
	out_info->physical_cores = windows_get_physical_core_count();
	out_info->cpu_mhz = windows_get_cpu_base_clock_mhz();

	// RAM
	MEMORYSTATUSEX m;
	kzero_memory(&m, sizeof(m));
	m.dwLength = sizeof(m);

	if (GlobalMemoryStatusEx(&m)) {
		out_info->ram_total_bytes = m.ullTotalPhys;
		out_info->ram_available_bytes = m.ullAvailPhys;
	}
	out_info->ram_speed_mhz = windows_get_ram_speed_mhz();

	// OS info
	OSVERSIONINFOEXA v = {sizeof(v)};
	RtlGetVersion((PRTL_OSVERSIONINFOW)&v);
	string_nformat(out_info->os_version, sizeof(out_info->os_version), "%lu.%lu", v.dwMajorVersion, v.dwMinorVersion);
	string_nformat(out_info->os_build, sizeof(out_info->os_build), "%lu", v.dwBuildNumber);
	string_nformat(out_info->kernel_version, sizeof(out_info->kernel_version), "NT %lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);

	DWORD product = 0;
	GetProductInfo(v.dwMajorVersion, v.dwMinorVersion, 0, 0, &product);

	u8 actual_ver = (v.dwBuildNumber >= 22000) ? 11 : 10;
	char *windows_formatted = string_format("Windows %u %s 64-bit", actual_ver, windows_product_to_string(product));
	string_copy(out_info->os_name, windows_formatted);
	string_free(windows_formatted);

	string_copy(out_info->cpu_arch, "x86_64");
	detect_x86_features(&out_info->features);

	// Storage
	windows_query_storage(out_info);

	// Misc
	FLAG_SET(out_info->flags, KSYSTEM_INFO_FLAGS_IS_64_BIT_BIT, true);
	FLAG_SET(out_info->flags, KSYSTEM_INFO_FLAGS_DEBUGGER_ATTACHED_BIT, IsDebuggerPresent());

	return true;
}

void platform_request_clipboard_content (kwindow *window) {
	if (!OpenClipboard(NULL)) {
		return;
	}

	HANDLE h = GetClipboardData(CF_UNICODETEXT);
	if (!h) {
		goto win32_clipboard_fire;
	}

	wchar_t *wtext = (wchar_t *)GlobalLock(h);
	if (!wtext) {
		goto win32_clipboard_fire;
	}

	i32 utf8_len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1, NULL, 0, NULL, NULL);

	char *text = kallocate(utf8_len, MEMORY_TAG_STRING);
	WideCharToMultiByte(CP_UTF8, 0, wtext, -1, text, utf8_len, NULL, NULL);

	GlobalUnlock(h);

win32_clipboard_fire:
	CloseClipboard();

	if (state_ptr->on_paste) {
		kclipboard_context ctx = {
			.content = text,
			.content_type = KCLIPBOARD_CONTENT_TYPE_STRING,
			.size = utf8_len,
			.requesting_window = window};
		state_ptr->on_paste(ctx);
	}
}
static wchar_t *utf8_to_utf16 (const char *utf8) {
	if (!utf8) {
		return KNULL;
	}

	i32 len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, KNULL, 0);

	if (len <= 0) {
		return KNULL;
	}

	wchar_t *out = (wchar_t *)malloc(len * sizeof(wchar_t));
	if (!out) {
		return KNULL;
	}
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, out, len);

	return out;
}

void platform_clipboard_content_set (kwindow *window, kclipboard_content_type type, u32 size, void *content) {
	if (!OpenClipboard(KNULL)) {
		return;
	}

	EmptyClipboard();

	u64 len;
	wchar_t *wtext = 0;
	if (type == KCLIPBOARD_CONTENT_TYPE_STRING) {
		wtext = utf8_to_utf16(content);
		len = (wcslen(wtext) + 1) * sizeof(wchar_t);
	} else {
		len = size;
	}

	HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, len);
	if (!hmem) {
		goto win32_clipboard_fire;
	}

	void *dest = GlobalLock(hmem);
	if (type == KCLIPBOARD_CONTENT_TYPE_STRING) {
		memcpy(dest, wtext, len);
	} else {
		memcpy(dest, content, len);
	}
	GlobalUnlock(hmem);

	SetClipboardData(CF_UNICODETEXT, hmem);

win32_clipboard_fire:
	CloseClipboard();
	if (wtext) {
		free(wtext);
	}
}

typedef struct file_list {
	const char **items;
	u32 count;
} file_list;

file_list parse_open_filename (const wchar_t *buffer) {
	file_list result = {0};

	if (!buffer || !buffer[0]) {
		return result;
	}

	const wchar_t *p = buffer;

	// First string
	const wchar_t *first = p;
	u64 first_len = wcslen(first);

	p += first_len + 1;

	// If next is null → single file
	if (*p == L'\0') {
		result.items = KALLOC_TYPE_CARRAY(const char *, 1);
		result.items[0] = wcstr_to_cstr(first);
		result.count = 1;
		return result;
	}

	// Otherwise: directory + multiple files
	const wchar_t *dir = first;

	// Count files
	u32 count = 0;
	const wchar_t *tmp = p;
	while (*tmp) {
		tmp += wcslen(tmp) + 1;
		count++;
	}

	result.items = KALLOC_TYPE_CARRAY(const char *, count);
	result.count = count;

	for (u32 i = 0; i < count; ++i) {
		const wchar_t *file = p;

		u64 dir_len = wcslen(dir);
		u64 file_len = wcslen(file);

		// dir + '\' + file + null
		u64 total = dir_len + 1 + file_len + 1;

		wchar_t *full = (wchar_t *)malloc(sizeof(wchar_t) * total);

		wcscpy(full, dir);
		full[dir_len] = L'\\';
		wcscpy(full + dir_len + 1, file);

		result.items[i] = wcstr_to_cstr(full);

		p += file_len + 1;
	}

	return result;
}

void free_file_list (file_list *list) {
	if (!list || !list->items) {
		return;
	}

	for (u32 i = 0; i < list->count; ++i) {
		string_free(list->items[i]);
	}

	kfree(list->items);

	list->items = NULL;
	list->count = 0;
}

static wchar_t *build_filter_string (const wchar_t *input) {
	if (!input) {
		return KNULL;
	}

	u64 len = wcslen(input);

	// Worst case: every char becomes itself, plus we add one extra null at the end
	wchar_t *out = (wchar_t *)KALLOC_TYPE_CARRAY(wchar_t, (len + 2));
	if (!out) {
		return KNULL;
	}

	u64 j = 0;

	for (u64 i = 0; i < len; ++i) {
		if (input[i] == L'|') {
			out[j++] = L'\0';
		} else {
			out[j++] = input[i];
		}
	}

	// Double null terminate
	out[j++] = L'\0';
	out[j++] = L'\0';

	return out;
}

platform_open_file_dialog_result platform_open_file_dialog_open (platform_open_file_dialog_options options) {
	platform_open_file_dialog_result result = {
		.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE,
		.file_count = 0,
		.file_paths = KNULL,
	};

	OPENFILENAMEW ofn;
	kzero_memory(&ofn, sizeof(ofn));

	wchar_t path[32768];
	path[0] = L'\0';

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = path;
	ofn.nMaxFile = 32768;

	// Filter
	ofn.lpstrFilter = options.filter ? build_filter_string(cstr_to_wcstr(options.filter)) : L"All Files (*.*)\0*.*\0";

	ofn.nFilterIndex = 1;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	if (options.allow_multiselect) {
		ofn.Flags |= OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	}
	ofn.lpstrTitle = options.title ? cstr_to_wcstr(options.title) : L"Open File";

	if (GetOpenFileNameW(&ofn) != 0) {
		file_list fl = parse_open_filename(path);
		for (u32 i = 0; i < fl.count; ++i) {
			KDEBUG("Selected: '%s'", fl.items[i]);
		}
		result.result = PLATFORM_OFD_OP_RESULT_SUCCESS;
		result.file_count = fl.count;
		result.file_paths = fl.items;
	} else {
		DWORD err = CommDlgExtendedError();
		if (err == 0) {
			// No error, was cancelled.
			result.result = PLATFORM_OFD_OP_RESULT_USER_CANCELLED;
		} else {
			result.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE;
		}
	}

	if (options.title) {
		// Free converted string if applicable.
		kfree(ofn.lpstrTitle);
	}

	return result;
}

b8 platform_initialize_input (void) {
	platform_state *state = state_ptr;

	// Register keyboard raw input device.
	register_raw_input_device(state->windows[0]->platform_state->hwnd);

	KINFO("Windows platform input initialized.");

	return true;
}

static kwindow *window_from_handle (HWND hwnd) {
	u32 len = darray_length(state_ptr->windows);
	for (u32 i = 0; i < len; ++i) {
		kwindow *w = state_ptr->windows[i];
		if (w && w->platform_state->hwnd == hwnd) {
			return state_ptr->windows[i];
		}
	}
	return 0;
}

LRESULT CALLBACK win32_process_message (HWND hwnd, u32 msg, WPARAM w_param, LPARAM l_param) {
	switch (msg) {
	case WM_CREATE:
		return 0;
	case WM_ERASEBKGND:
		// Notify the OS that erasing will be handled by the application to prevent flicker.
		return 1;
	case WM_CLOSE: {
		if (state_ptr->window_closed_callback) {
			kwindow *w = window_from_handle(hwnd);
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
		kwindow *w = window_from_handle(hwnd);

		// Store off the device pixel ratio.
		w->platform_state->device_pixel_ratio = (f32)x_dpi / USER_DEFAULT_SCREEN_DPI;
		KINFO("Display device pixel ratio is: %.2f", w->platform_state->device_pixel_ratio);

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
		kwindow *w = window_from_handle(hwnd);
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

			// Key repeats can never be on release.
			b8 is_repeat = pressed && ((l_param & (1 << 30)) != 0);

			// Keypress only determines if _any_ alt/ctrl/shift key is pressed. Determine which one if so.
			if (w_param == VK_MENU) {
				key = is_extended ? K_KEY_RALT : K_KEY_LALT;
			} else if (w_param == VK_SHIFT) {
				// NOTE: Since this method doesn't handle multiple shift keys pressed at once well, don't process it here.
				return 0;
				/* // Annoyingly, KF_EXTENDED is not set for shift keys.
				u32 left_shift = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
				u32 scancode = ((l_param & (0xFF << 16)) >> 16);
				key = scancode == left_shift ? K_KEY_LSHIFT : K_KEY_RSHIFT; */
			} else if (w_param == VK_CONTROL) {
				key = is_extended ? K_KEY_RCONTROL : K_KEY_LCONTROL;
			}

			// HACK: This is gross windows keybind crap.
			if (key == VK_OEM_1) {
				key = K_KEY_SEMICOLON;
			}

			// Pass to the input subsystem for processing.
			state_ptr->process_key(key, pressed, is_repeat);

			// Return 0 to prevent default window behaviour for some keypresses, such as alt.
		}
		return 0;
	}
	case WM_INPUT_DEVICE_CHANGE: {
		// RawInput - handle device connect/disconnect.
		HANDLE device = (HANDLE)l_param;

		if (w_param == GIDC_REMOVAL) {
			// Device disconnected.
			KINFO("Device disconnected.");
		} else if (w_param == GIDC_ARRIVAL) {
			// Device connected.
			KINFO("Device connected (0x%X).", device);

			// Get device interface name.
			u32 char_count = 0;
			i32 di_result = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, KNULL, &char_count); // should be 0
			if (di_result != 0) {
				KERROR("Failed to get device info/name length");
				return 0;
			}
			WCHAR *di_name = kallocate_aligned(sizeof(WCHAR) * char_count, 16, MEMORY_TAG_ARRAY);
			di_result = GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, di_name, &char_count); // should be bytes copied
			DWORD err2 = GetLastError();
			if (di_result < 1) {
				KERROR("Failed to get device info name string - err = %lld", err2);
				kfree(di_name);
				return 0;
			}

			// Use the device string to get the product details.
			win32_device_info di = {0};
			get_hid_device_info(di_name, &di);

			RID_DEVICE_INFO device_info = {0};
			device_info.cbSize = sizeof(RID_DEVICE_INFO);
			u32 di_size = sizeof(RID_DEVICE_INFO);

			di_result = GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &device_info, &di_size);
			char *type_str = "";
			switch (device_info.dwType) {
			case RIM_TYPEMOUSE:
				di.type = INPUT_DEVICE_TYPE_MOUSE;
				di.mouse.button_count = device_info.mouse.dwNumberOfButtons;
				di.mouse.sample_rate = device_info.mouse.dwSampleRate;
				di.mouse.has_horizontal_wheel = device_info.mouse.fHasHorizontalWheel;
				type_str = "Mouse";
				break;
			case RIM_TYPEKEYBOARD:
				di.type = INPUT_DEVICE_TYPE_KEYBOARD;
				di.keyboard.key_count = device_info.keyboard.dwNumberOfKeysTotal;
				di.keyboard.fn_key_count = device_info.keyboard.dwNumberOfFunctionKeys;
				di.keyboard.mode = device_info.keyboard.dwKeyboardMode;
				type_str = "Keyboard";
				break;
			case RIM_TYPEHID:
				di.type = INPUT_DEVICE_TYPE_HID;
				di.hid.usage_page = device_info.hid.usUsagePage;
				di.hid.usage = device_info.hid.usUsage;
				type_str = "HID device";
				break;
			default:
				// Unknown
				di.type = INPUT_DEVICE_TYPE_UNKNOWN;
				type_str = "Unknown";
				break;
			}

			//
			KINFO("Device acquired (0x%X, 0x%X): type='%s' manufacturer='%s', product='%s', version=%hu", di.vendor_id, di.product_id, type_str, di.manufacturer, di.product, di.version);

			kfree(di_name);
		}

		return 0;
	}
	case WM_INPUT: {
		RAWINPUT input = {0};
		UINT size = sizeof(input);

		GetRawInputData((HRAWINPUT)l_param, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));

		if (input.header.dwType == RIM_TYPEKEYBOARD) {
			const RAWKEYBOARD *keyboard = &input.data.keyboard;

			b8 pressed = !(keyboard->Flags & RI_KEY_BREAK);

			// For now, only making use of RawInput to handle the double shift press issue.
			keys key;
			// keyboard->MakeCode identifies the physical scan code.
			if (keyboard->MakeCode == 0x2A) {
				key = K_KEY_LSHIFT;
			} else if (keyboard->MakeCode == 0x36) {
				key = K_KEY_RSHIFT;
			} else {
				return 0;
			}
			state_ptr->process_key(key, pressed, false);
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

	case WM_DEVICECHANGE: {
		// Gamepad Support
		switch (w_param) {
		case DBT_DEVICEARRIVAL:
			if (((DEV_BROADCAST_HDR *)l_param)->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				FLAG_SET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED, true);
			}
			break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (((DEV_BROADCAST_HDR *)l_param)->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				FLAG_SET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED, true);
			}
			break;
		}
		return 0;
	}
	}

	return DefWindowProcW(hwnd, msg, w_param, l_param);
}

static LPCWSTR cstr_to_wcstr (const char *str) {
	if (!str) {
		return 0;
	}

	i32 len = MultiByteToWideChar(CP_UTF8, 0, str, -1, KNULL, 0);
	if (len == 0) {
		return KNULL;
	}

	// NOTE: The alignment here is important! Not doing this occasionally/randomly
	// causes functions (such as SetWindowTitle()) to silently fail (though report
	// success) and there's _no_ tracability of it. Aligning this allocation seems
	// to fix all of these issues.
	LPWSTR wstr = kallocate_aligned(sizeof(WCHAR) * len, 16, MEMORY_TAG_STRING);
	if (!wstr) {
		return KNULL;
	}

	i32 result = MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
	if (result == 0) {
		kfree(wstr);
		return KNULL;
	}

	return wstr;
}

static const char *wcstr_to_cstr (LPCWSTR wstr) {
	if (!wstr) {
		return 0;
	}

	i32 length = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
	if (length == 0) {
		return 0;
	}
	char *str = kallocate(sizeof(char) * length, MEMORY_TAG_STRING);
	if (!str) {
		return 0;
	}
	if (WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, length, NULL, NULL) == 0) {
		kfree(str);
		return 0;
	}

	return str;
}

void platform_gamepads_initialize (void) {
	platform_state *state = state_ptr;
	state->gamepad_state = kallocate_aligned(sizeof(platform_gamepad_state), 8, MEMORY_TAG_PLATFORM);

	// Load DirectInput
	state->gamepad_state->direct_input_lib = LoadLibraryW(L"DINPUT8.dll");
	if (state->gamepad_state->direct_input_lib == KNULL) {
		KINFO("Unable to load DINPUT8.dll.");
		FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_DIRECTINPUT_AVAILABLE, false);
		return;
	}

	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_DIRECTINPUT_AVAILABLE, true);
	state->gamepad_state->kDirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(state->gamepad_state->direct_input_lib, "DirectInput8Create");
	HRESULT result = state->gamepad_state->kDirectInput8Create(GetModuleHandleW(KNULL), DIRECTINPUT_VERSION, &IID_IDirectInput8, (void **)&state->gamepad_state->direct_input_iface, KNULL);
	if (result != S_OK) {
		KERROR("DirectInput8Create returned 0x%X", (u32)result);
	}

	state->gamepad_state->devices = darray_reserve(platform_gamepad_device *, 4);

	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT, true);
	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED, true);

	detect_gamepad_devices(state);

	state->gamepad_state->co_initialized = CoInitializeEx(KNULL, COINIT_APARTMENTTHREADED);
	if (state->gamepad_state->co_initialized == RPC_E_CHANGED_MODE) {
		state->gamepad_state->co_initialized = CoInitializeEx(KNULL, COINIT_MULTITHREADED);
	}

	GUID GUID_DEVINTERFACE_HID = {0x4D1E55B2L, 0xF16F, 0x11CF, {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
	DEV_BROADCAST_DEVICEINTERFACE dbh = {0};
	kzero_memory(&dbh, sizeof(dbh));
	dbh.dbcc_size = sizeof(dbh);
	dbh.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	dbh.dbcc_classguid = GUID_DEVINTERFACE_HID;

	// HACK: send all messages through the first window.
	kwindow *first_window = state->windows[0];
	state->gamepad_state->h_notify = RegisterDeviceNotification(first_window->platform_state->hwnd, &dbh, DEVICE_NOTIFY_WINDOW_HANDLE);
	if (!state->gamepad_state->h_notify) {
		KFATAL("Unable to setup gamepad device notification.");
		return;
	}
}

u32 platform_gamepad_device_count (void) {
	return darray_length(state_ptr->gamepad_state->devices);
}

u32 platform_gamepad_deviceid_at_index (u32 device_index) {
	if (device_index >= darray_length(state_ptr->gamepad_state->devices)) {
		return INVALID_ID_U32;
	}
	return state_ptr->gamepad_state->devices[device_index]->device_id;
}

b8 platform_gamepad_get_device_index (u32 device_id, u8 *out_index) {
	u32 len = darray_length(state_ptr->gamepad_state->devices);
	for (u8 i = 0; i < len; ++i) {
		if (!state_ptr->gamepad_state->devices[i]) {
			continue;
		}
		if (state_ptr->gamepad_state->devices[i]->device_id == device_id) {
			*out_index = i;
			return true;
		}
	}

	return false;
}
b8 platform_gamepad_get_device_info (u32 device_id, i32 *out_vendor_id, i32 *out_product_id, u32 *out_button_count, u32 *out_axis_count) {
	u8 device_index = INVALID_ID_U8;
	if (!platform_gamepad_get_device_index(device_id, &device_index)) {
		return false;
	}
	platform_gamepad_device *device = state_ptr->gamepad_state->devices[device_index];
	if (out_vendor_id) {
		*out_vendor_id = device->vendor_id;
	}
	if (out_product_id) {
		*out_product_id = device->product_id;
	}
	if (out_button_count) {
		*out_button_count = device->button_count;
	}
	if (out_axis_count) {
		*out_axis_count = device->axis_count;
	}
	return true;
}

// index must be [0-3]. Otherwise will be ignored. Returns true if set successfully; otherwise false.
b8 platform_gamepad_set_device_player_index (u32 device_id, u8 player_index) {
	if (!FLAG_GET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return false;
	}

	if (player_index < 0 || player_index > 3) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	// TODO: Set device LED.

	/* if (device->input_path != KNULL && device->input_name != KNULL) {
		if (!gamepad_try_set_player_index_led(device, player_index)) {
			KWARN("Failed to set LED player index for gamepad '%s'. Numeric index still set though.", device->description);
		}
	} */

	return true;
}

b8 platform_gamepad_is_button_pressed (u32 device_id, gamepad_button button) {
	if (!FLAG_GET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	return FLAG_GET(device->button_states, (u32)button);
}

f32 platform_gamepad_get_axis_state (u32 device_id, gamepad_axis axis) {
	if (!FLAG_GET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	return device->axis_states[axis];
}

// NOTE: allocates a new string that should be freed by the caller.
const char *platform_gamepad_get_device_desc (u32 device_id) {
	u8 device_index = INVALID_ID_U8;
	if (!platform_gamepad_get_device_index(device_id, &device_index)) {
		return false;
	}
	platform_gamepad_device *device = state_ptr->gamepad_state->devices[device_index];
	return string_duplicate(device->description);
}

static void cleanup_gamepad_device (platform_gamepad_device *device) {
	IDirectInputDevice8_Release(device->device_interface);
	kfree(device->axis_info);
	kfree(device->button_offsets);
	kfree(device->description);

	kzero_memory(device, sizeof(platform_gamepad_device));
	kfree(device);
}

static BOOL CALLBACK count_axes_callback (LPCDIDEVICEOBJECTINSTANCE instance, LPVOID context) {
	platform_gamepad_device *device = context;

	device->axis_count++;
	if (instance->dwType & DIDFT_POV) {
		device->axis_count++;
	}
	return DIENUM_CONTINUE;
}

static BOOL CALLBACK count_buttons_callback (LPCDIDEVICEOBJECTINSTANCE instance, LPVOID context) {
	platform_gamepad_device *device = context;

	device->button_count++;
	return DIENUM_CONTINUE;
}

static BOOL CALLBACK enum_axes_callback (LPCDIDEVICEOBJECTINSTANCE instance, LPVOID context) {
	platform_gamepad_device *device = context;

	DWORD offset;
	device->axis_count++;
	if (instance->dwType & DIDFT_POV) {
		offset = DIJOFS_POV(device->pov_count);
		device->axis_info[device->axis_count - 1].offset = offset;
		device->axis_info[device->axis_count - 1].is_pov = true;
		device->axis_info[device->axis_count - 1].axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count - 1);
		device->axis_count++;
		device->axis_info[device->axis_count - 1].offset = offset;
		device->axis_info[device->axis_count - 1].is_pov = true;
		device->axis_info[device->axis_count - 1].axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count - 1);
		device->pov_count++;
	} else {
		if (!memcmp(&instance->guidType, &GUID_XAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_X;
		} else if (!memcmp(&instance->guidType, &GUID_YAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_Y;
		} else if (!memcmp(&instance->guidType, &GUID_ZAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_Z;
		} else if (!memcmp(&instance->guidType, &GUID_RxAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_RX;
		} else if (!memcmp(&instance->guidType, &GUID_RyAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_RY;
		} else if (!memcmp(&instance->guidType, &GUID_RzAxis, sizeof(instance->guidType))) {
			offset = DIJOFS_RZ;
		} else if (!memcmp(&instance->guidType, &GUID_Slider, sizeof(instance->guidType))) {
			offset = DIJOFS_SLIDER(device->slider_count++);
		} else {
			offset = -1;
		}

		device->axis_info[device->axis_count - 1].offset = offset;
		device->axis_info[device->axis_count - 1].is_pov = false;
		device->axis_info[device->axis_count - 1].axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count - 1);

		DIPROPRANGE range = {0};
		range.diph.dwSize = sizeof(range);
		range.diph.dwHeaderSize = sizeof(range.diph);
		range.diph.dwObj = instance->dwType;
		range.diph.dwHow = DIPH_BYID;
		range.lMin = AXIS_MIN;
		range.lMax = AXIS_MAX;

		HRESULT result = IDirectInputDevice8_SetProperty(device->device_interface, DIPROP_RANGE, &range.diph);
		if (result != DI_OK) {
			KERROR("IDirectInputDevice8_SetProperty returned 0x%X", (u32)result);
		}

		DIPROPDWORD dead_zone = {0};
		dead_zone.diph.dwSize = sizeof(dead_zone);
		dead_zone.diph.dwHeaderSize = sizeof(dead_zone.diph);
		dead_zone.diph.dwObj = instance->dwType;
		dead_zone.diph.dwHow = DIPH_BYID;
		dead_zone.dwData = 0;
		result = IDirectInputDevice8_SetProperty(device->device_interface, DIPROP_DEADZONE, &dead_zone.diph);
		if (result != DI_OK) {
			KERROR("IDirectInputDevice8_SetProperty returned 0x%X", (u32)result);
		}
	}

	return DIENUM_CONTINUE;
}

static BOOL CALLBACK enum_buttons_callback (LPCDIDEVICEOBJECTINSTANCE instance, LPVOID context) {
	platform_gamepad_device *device = context;

	device->button_offsets[device->button_count] = DIJOFS_BUTTON(device->button_count);
	device->buttons[device->button_count] = gamepad_get_button(device->vendor_id, device->product_id, device->button_count);
	device->button_count++;
	return DIENUM_CONTINUE;
}

#	ifndef DIDFT_OPTIONAL
#		define DIDFT_OPTIONAL 0x80000000
#	endif

/* Taken from Wine - Thanks! */
DIOBJECTDATAFORMAT dfDIJoystick2[] = {
	{&GUID_XAxis, DIJOFS_X, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_YAxis, DIJOFS_Y, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_ZAxis, DIJOFS_Z, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RxAxis, DIJOFS_RX, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RyAxis, DIJOFS_RY, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RzAxis, DIJOFS_RZ, DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, DIJOFS_SLIDER(0), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, DIJOFS_SLIDER(1), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_POV, DIJOFS_POV(0), DIDFT_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE, 0},
	{&GUID_POV, DIJOFS_POV(1), DIDFT_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE, 0},
	{&GUID_POV, DIJOFS_POV(2), DIDFT_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE, 0},
	{&GUID_POV, DIJOFS_POV(3), DIDFT_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(0), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(1), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(2), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(3), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(4), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(5), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(6), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(7), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(8), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(9), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(10), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(11), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(12), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(13), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(14), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(15), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(16), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(17), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(18), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(19), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(20), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(21), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(22), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(23), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(24), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(25), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(26), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(27), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(28), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(29), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(30), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(31), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(32), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(33), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(34), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(35), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(36), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(37), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(38), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(39), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(40), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(41), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(42), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(43), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(44), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(45), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(46), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(47), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(48), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(49), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(50), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(51), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(52), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(53), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(54), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(55), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(56), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(57), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(58), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(59), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(60), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(61), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(62), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(63), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(64), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(65), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(66), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(67), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(68), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(69), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(70), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(71), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(72), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(73), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(74), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(75), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(76), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(77), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(78), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(79), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(80), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(81), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(82), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(83), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(84), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(85), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(86), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(87), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(88), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(89), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(90), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(91), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(92), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(93), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(94), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(95), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(96), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(97), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(98), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(99), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(100), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(101), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(102), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(103), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(104), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(105), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(106), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(107), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(108), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(109), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(110), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(111), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(112), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(113), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(114), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(115), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(116), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(117), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(118), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(119), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(120), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(121), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(122), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(123), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(124), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(125), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(126), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{NULL, DIJOFS_BUTTON(127), DIDFT_OPTIONAL | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
	{&GUID_XAxis, FIELD_OFFSET(DIJOYSTATE2, lVX), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_YAxis, FIELD_OFFSET(DIJOYSTATE2, lVY), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_ZAxis, FIELD_OFFSET(DIJOYSTATE2, lVZ), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RxAxis, FIELD_OFFSET(DIJOYSTATE2, lVRx), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RyAxis, FIELD_OFFSET(DIJOYSTATE2, lVRy), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RzAxis, FIELD_OFFSET(DIJOYSTATE2, lVRz), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglVSlider[0]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglVSlider[1]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_XAxis, FIELD_OFFSET(DIJOYSTATE2, lAX), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_YAxis, FIELD_OFFSET(DIJOYSTATE2, lAY), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_ZAxis, FIELD_OFFSET(DIJOYSTATE2, lAZ), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RxAxis, FIELD_OFFSET(DIJOYSTATE2, lARx), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RyAxis, FIELD_OFFSET(DIJOYSTATE2, lARy), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RzAxis, FIELD_OFFSET(DIJOYSTATE2, lARz), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglASlider[0]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglASlider[1]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_XAxis, FIELD_OFFSET(DIJOYSTATE2, lFX), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_YAxis, FIELD_OFFSET(DIJOYSTATE2, lFY), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_ZAxis, FIELD_OFFSET(DIJOYSTATE2, lFZ), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RxAxis, FIELD_OFFSET(DIJOYSTATE2, lFRx), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RyAxis, FIELD_OFFSET(DIJOYSTATE2, lFRy), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_RzAxis, FIELD_OFFSET(DIJOYSTATE2, lFRz), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglFSlider[0]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
	{&GUID_Slider, FIELD_OFFSET(DIJOYSTATE2, rglFSlider[1]), DIDFT_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
};

const DIDATAFORMAT c_dfDIJoystick2 = {
	sizeof(DIDATAFORMAT),
	sizeof(DIOBJECTDATAFORMAT),
	DIDF_ABSAXIS,
	sizeof(DIJOYSTATE2),
	sizeof(dfDIJoystick2) / sizeof(dfDIJoystick2[0]),
	dfDIJoystick2};

static BOOL CALLBACK enum_devices_callback (const DIDEVICEINSTANCE *instance, LPVOID context) {
	platform_state *state = state_ptr;
	b8 buffered = true;

	u32 len = darray_length(state->gamepad_state->devices);
	for (u32 i = 0; i < len; ++i) {
		if (!state->gamepad_state->devices[i]) {
			continue;
		}
		if (!memcmp(&state->gamepad_state->devices[i]->guid_instance, &instance->guidInstance, sizeof(GUID))) {
			return DIENUM_CONTINUE;
		}
	}

	IDirectInputDevice *di_device;
	IDirectInputDevice8 *di8_device;
	HRESULT result = IDirectInput8_CreateDevice(state->gamepad_state->direct_input_iface, &instance->guidInstance, &di_device, KNULL);
	if (result != DI_OK) {
		KERROR("IDirectInput8_CreateDevice returned 0x%X.", (u32)result);
	}
	result = IDirectInputDevice8_QueryInterface(di_device, &IID_IDirectInputDevice8, (LPVOID *)&di8_device);
	if (result != DI_OK) {
		KERROR("IDirectInputDevice8_QueryInterface returned 0x%X.", (u32)result);
	}
	IDirectInputDevice8_Release(di_device);

	result = IDirectInputDevice8_SetCooperativeLevel(di8_device, GetActiveWindow(), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
	if (result != DI_OK) {
		KERROR("IDirectInputDevice8_SetCooperativeLevel returned 0x%X.", (u32)result);
	}

	result = IDirectInputDevice8_SetDataFormat(di8_device, &c_dfDIJoystick2);
	if (result != DI_OK) {
		KERROR("IDirectInputDevice8_SetDataFormat returned 0x%X.", (u32)result);
	}

	DIPROPDWORD buffer_size_prop = {0};
	buffer_size_prop.diph.dwSize = sizeof(DIPROPDWORD);
	buffer_size_prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
	buffer_size_prop.diph.dwObj = 0;
	buffer_size_prop.diph.dwHow = DIPH_DEVICE;
	buffer_size_prop.dwData = INPUT_QUEUE_SIZE;
	result = IDirectInputDevice8_SetProperty(di8_device, DIPROP_BUFFERSIZE, &buffer_size_prop.diph);
	if (result == DI_POLLEDDEVICE) {
		buffered = false;
	} else if (result != DI_OK) {
		KERROR("IDirectInputDevice8_SetDataProperty returned 0x%X.", (u32)result);
	}

	platform_gamepad_device *device = KALLOC_TYPE(platform_gamepad_device, MEMORY_TAG_PLATFORM);
	device->guid_instance = instance->guidInstance;
	device->device_interface = di8_device;
	device->buffered = buffered;
	device->slider_count = 0;
	device->pov_count = 0;
	device->device_id = state->gamepad_state->next_device_id++;

	device->description = (char *)wcstr_to_cstr(instance->tszProductName);
	device->vendor_id = instance->guidProduct.Data1 & 0xFFFF;
	device->product_id = instance->guidProduct.Data1 >> 16 & 0xFFFF;

	// Get axis and button counts.
	device->axis_count = 0;
	IDirectInputDevice8_EnumObjects(di8_device, count_axes_callback, device, DIDFT_AXIS | DIDFT_POV);
	device->button_count = 0;
	IDirectInputDevice8_EnumObjects(di8_device, count_buttons_callback, device, DIDFT_BUTTON);
	kzero_memory(device->axis_states, sizeof(device->axis_states));
	/* kzero_memory(device->button_states, sizeof(device->button_states)); */

	device->axis_info = KALLOC_TYPE_CARRAY(diAxisInfo, device->axis_count);
	device->button_offsets = KALLOC_TYPE_CARRAY(DWORD, device->button_count);
	device->buttons = KALLOC_TYPE_CARRAY(gamepad_button, device->button_count);

	// Enumerate them.
	device->axis_count = 0;
	IDirectInputDevice8_EnumObjects(di8_device, enum_axes_callback, device, DIDFT_AXIS | DIDFT_POV);
	device->button_count = 0;
	IDirectInputDevice8_EnumObjects(di8_device, enum_buttons_callback, device, DIDFT_BUTTON);

	darray_push(state->gamepad_state->devices, &device);

	if (state->gamepad_callbacks.on_attached) {
		state->gamepad_callbacks.on_attached(device->device_id);
	}

	return DIENUM_CONTINUE;
}

static void remove_gamepad_device (u32 device_index) {
	platform_state *state = state_ptr;
	if (state->gamepad_callbacks.on_detached) {
		state->gamepad_callbacks.on_detached(state->gamepad_state->devices[device_index]->device_id);
	}

	cleanup_gamepad_device(state->gamepad_state->devices[device_index]);
	darray_pop_at(state->gamepad_state->devices, device_index, KNULL);
}

void detect_gamepad_devices (platform_state *state) {
	if (FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED)) {
		HRESULT result = IDirectInput_EnumDevices(
			state->gamepad_state->direct_input_iface,
			DI8DEVCLASS_GAMECTRL,
			enum_devices_callback,
			KNULL,
			DIEDFL_ALLDEVICES);
		if (result != S_OK) {
			KERROR("IDirectInput_EnumDevices returned with 0x%X", (u32)result);
		}
		FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_WINDOWS_DEVICE_CHANGED, false);
	}
}

static void gamepad_update_button_value (platform_gamepad_device *device, gamepad_button button, b8 pressed, f64 timestamp) {
	if (pressed != FLAG_GET(device->button_states, (u32)button)) {
		FLAG_SET(device->button_states, (u32)button, pressed);
		if (pressed && state_ptr->gamepad_callbacks.on_button_pressed) {
			state_ptr->gamepad_callbacks.on_button_pressed(device->device_id, button, timestamp);
		} else if (!pressed && state_ptr->gamepad_callbacks.on_button_released) {
			state_ptr->gamepad_callbacks.on_button_released(device->device_id, button, timestamp);
		}
	}
}

static void gamepad_update_axis_value_f32 (platform_gamepad_device *device, gamepad_axis axis, f32 value, f64 timestamp) {
	f32 last_value = device->axis_states[axis];
	if (axis == GAMEPAD_AXIS_IGNORE) {
		// Boot, treat as processed.
		return;
	}
	if (value != last_value) {
		if (axis == GAMEPAD_AXIS_DPAD_LR || axis == GAMEPAD_AXIS_DPAD_UD) {
			// Axis represents a d-pad and needs to be remapped.
			b8 pressed = value != 0;
			f32 val = pressed ? value : last_value;
			gamepad_button mapped_button;
			if (val < 0) {
				mapped_button = (axis == GAMEPAD_AXIS_DPAD_UD) ? GAMEPAD_BUTTON_DPAD_UP : GAMEPAD_BUTTON_DPAD_LEFT;
			} else {
				mapped_button = (axis == GAMEPAD_AXIS_DPAD_UD) ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_RIGHT;
			}

			gamepad_update_button_value(device, mapped_button, pressed, timestamp);
		} else {
			if (axis == GAMEPAD_AXIS_LRZ) {
				// The z axes are combined and must be split.
				// NOTE: Making the assumption here that left is >0 and right is <0
				// May need a way to flip this.
				if (value > 0) {
					axis = GAMEPAD_AXIS_LZ;
				} else if (value < 0) {
					axis = GAMEPAD_AXIS_RZ;
				}

				// Normalize it to [0-1]
				value = kabs(value);
				last_value = kabs(last_value);

			} else if (axis == GAMEPAD_AXIS_LZ || axis == GAMEPAD_AXIS_RZ) {
				// If axis is not split, convert z axes to a range of [0-1].
				value = (value + 1.0f) * 0.5f;
				last_value = (last_value + 1.0f) * 0.5f;
			}
			if (state_ptr->gamepad_callbacks.on_axis_changed) {
				state_ptr->gamepad_callbacks.on_axis_changed(device->device_id, axis, value, last_value, timestamp);
			}
		}

		device->axis_states[axis] = value;
	}
}
static void gamepad_update_axis_value (platform_gamepad_device *device, gamepad_axis axis, i32 value, f64 timestamp) {
	gamepad_update_axis_value_f32(device, axis, (value - AXIS_MIN) / (f32)(AXIS_MAX - AXIS_MIN) * 2.0f - 1.0f, timestamp);
}

#	define POV_UP 0
#	define POV_RIGHT 9000
#	define POV_DOWN 18000
#	define POV_LEFT 27000

static void pov_to_xy (DWORD pov, f32 *out_x, f32 *out_y) {
	if (LOWORD(pov) == 0xFFFF) {
		*out_x = *out_y = 0.0f;
	} else {
		if (pov > POV_UP && pov < POV_DOWN) {
			*out_x = 1.0f;
		} else if (pov > POV_DOWN) {
			*out_x = -1.0f;
		} else {
			*out_x = 0.0f;
		}

		if (pov > POV_LEFT || pov < POV_RIGHT) {
			*out_y = -1.0f;
		} else if (pov > POV_RIGHT && pov < POV_LEFT) {
			*out_y = 1.0f;
		} else {
			*out_y = 0.0f;
		}
	}
}

static void gamepad_update_pov_axis_values (platform_gamepad_device *device, u32 axis_index, DWORD ivalue, f64 timestamp) {
	f32 x = 0.0f, y = 0.0f;

	pov_to_xy(ivalue, &x, &y);
	gamepad_update_axis_value_f32(device, axis_index, x, timestamp);
	gamepad_update_axis_value_f32(device, axis_index + 1, y, timestamp);
}

void process_gamepad_events (platform_state *state) {
	if (!FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return;
	}
	if (FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT)) {
		return;
	}

	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT, true);
	u32 len = darray_length(state->gamepad_state->devices);
	for (u32 i = 0; i < len; ++i) {
		platform_gamepad_device *device = state->gamepad_state->devices[i];

		// Device likely in the middle of being destroyed.
		if (!device->device_interface) {
			continue;
		}

		// DirectInput
		HRESULT result = IDirectInputDevice8_Poll(device->device_interface);
		if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
			if (IDirectInputDevice8_Acquire(device->device_interface) == DI_OK) {
				IDirectInputDevice8_Poll(device->device_interface);
			}
		}

		if (device->buffered) {
			DWORD event_count = INPUT_QUEUE_SIZE;
			DIDEVICEOBJECTDATA events[INPUT_QUEUE_SIZE];

			result = IDirectInputDevice8_GetDeviceData(device->device_interface, sizeof(DIDEVICEOBJECTDATA), events, &event_count, 0);
			if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
				IDirectInputDevice8_Acquire(device->device_interface);
				result = IDirectInputDevice8_GetDeviceData(device->device_interface, sizeof(DIDEVICEOBJECTDATA), events, &event_count, 0);
			}

			if (!SUCCEEDED(result)) {
				remove_gamepad_device(i);
				i--;
				continue;
			}
			for (u32 j = 0; j < event_count; ++j) {

				// Update buttons
				for (u32 b = 0; b < device->button_count; ++b) {
					if (events[j].dwOfs == device->button_offsets[b]) {
						gamepad_update_button_value(device, device->buttons[b], !!events[j].dwData, events[j].dwTimeStamp / 1000.0f);
					}
				}

				// Update axes
				for (u32 a = 0; a < device->axis_count; ++a) {
					if (events[j].dwOfs == device->axis_info[a].offset) {
						if (device->axis_info[a].is_pov) {
							gamepad_update_pov_axis_values(device, device->axis_info[a].axis, events[j].dwData, events[j].dwTimeStamp / 1000.0f);
							a++;
						} else {
							gamepad_update_axis_value(device, device->axis_info[a].axis, events[j].dwData, events[j].dwTimeStamp / 1000.0f);
						}
					}
				}
			}
		} else {
			// not buffered.
			DIJOYSTATE2 j_state;

			result = IDirectInputDevice8_GetDeviceState(device->device_interface, sizeof(DIJOYSTATE2), &j_state);
			if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
				IDirectInputDevice8_Acquire(device->device_interface);
				result = IDirectInputDevice8_GetDeviceState(device->device_interface, sizeof(DIJOYSTATE2), &j_state);
			}

			if (result != DI_OK) {
				remove_gamepad_device(i);
				i--;
				continue;
			}

			// Update buttons.
			for (u8 b = 0; b < device->button_count; ++b) {
				gamepad_update_button_value(device, device->buttons[b], j_state.rgbButtons[b], platform_get_absolute_time());
			}

			// Update axes.
			for (u8 a = 0; a < device->axis_count; ++a) {
				gamepad_axis axis = device->axis_info[a].axis;
				switch (device->axis_info[a].offset) {
				case DIJOFS_X:
					gamepad_update_axis_value(device, axis, j_state.lX, platform_get_absolute_time());
					break;
				case DIJOFS_Y:
					gamepad_update_axis_value(device, axis, j_state.lY, platform_get_absolute_time());
					break;
				case DIJOFS_Z:
					gamepad_update_axis_value(device, axis, j_state.lZ, platform_get_absolute_time());
					break;
				case DIJOFS_RX:
					gamepad_update_axis_value(device, axis, j_state.lRx, platform_get_absolute_time());
					break;
				case DIJOFS_RY:
					gamepad_update_axis_value(device, axis, j_state.lRy, platform_get_absolute_time());
					break;
				case DIJOFS_RZ:
					gamepad_update_axis_value(device, axis, j_state.lRz, platform_get_absolute_time());
					break;
				case DIJOFS_SLIDER(0):
					gamepad_update_axis_value(device, axis, j_state.rglSlider[0], platform_get_absolute_time());
					break;
				case DIJOFS_SLIDER(1):
					gamepad_update_axis_value(device, axis, j_state.rglSlider[1], platform_get_absolute_time());
					break;
				case DIJOFS_POV(0):
					gamepad_update_pov_axis_values(device, axis, j_state.rgdwPOV[0], platform_get_absolute_time());
					a++;
					break;
				case DIJOFS_POV(1):
					gamepad_update_pov_axis_values(device, axis, j_state.rgdwPOV[1], platform_get_absolute_time());
					a++;
					break;
				case DIJOFS_POV(2):
					gamepad_update_pov_axis_values(device, axis, j_state.rgdwPOV[2], platform_get_absolute_time());
					a++;
					break;
				case DIJOFS_POV(3):
					gamepad_update_pov_axis_values(device, axis, j_state.rgdwPOV[3], platform_get_absolute_time());
					a++;
					break;
				}
			}
		}
	}

	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT, false);
}

platform_gamepad_device *get_gamepad_device_by_id (u32 device_id) {
	u32 len = darray_length(state_ptr->gamepad_state->devices);
	for (u32 i = 0; i < len; ++i) {
		if (state_ptr->gamepad_state->devices[i]->device_id == device_id) {
			// Found it.
			return state_ptr->gamepad_state->devices[i];
		}
	}

	return KNULL;
}

static gamepad_axis gamepad_get_axis_microsoft (u16 product_id, u8 axis_id) {
	switch (product_id) {
	default:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_0:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_1:
	case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER_PACC:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_2015:
	case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_ELITE_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_1:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_S_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_SXS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ELITE_SERIES_2_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_1:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 1:
			return GAMEPAD_AXIS_LX;
		case 0:
			return GAMEPAD_AXIS_LY;
		case 4:
			// These are a single axis on there. L>0, R<0
			return GAMEPAD_AXIS_LRZ; // LT and RT
		case 3:
			return GAMEPAD_AXIS_RX;
		case 2:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_DPAD_LR;
		case 6:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	}
}

static gamepad_axis gamepad_get_axis_sony (u16 product_id, u8 axis_id) {
	switch (product_id) {
	case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 9:
			return GAMEPAD_AXIS_LX;
		case 8:
			return GAMEPAD_AXIS_LY;
		case 3:
			return GAMEPAD_AXIS_LZ; // L2
		case 7:
			return GAMEPAD_AXIS_RX;
		case 6:
			return GAMEPAD_AXIS_RY;
		case 2:
			return GAMEPAD_AXIS_RZ; // R2
		case 0:
			return GAMEPAD_AXIS_DPAD_LR;
		case 1:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 3:
			return GAMEPAD_AXIS_LX;
		case 2:
			return GAMEPAD_AXIS_LY;
		case 7:
			return GAMEPAD_AXIS_LZ; // L2
		case 1:
			return GAMEPAD_AXIS_RX;
		case 0:
			return GAMEPAD_AXIS_RY;
		case 6:
			return GAMEPAD_AXIS_RZ; // R2
		case 4:
			return GAMEPAD_AXIS_DPAD_LR;
		case 5:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 5:
			return GAMEPAD_AXIS_LX;
		case 4:
			return GAMEPAD_AXIS_LY;
		case 1:
			return GAMEPAD_AXIS_LZ; // L2
		case 3:
			return GAMEPAD_AXIS_RX;
		case 2:
			return GAMEPAD_AXIS_RY;
		case 0:
			return GAMEPAD_AXIS_RZ; // R2
		case 6:
			return GAMEPAD_AXIS_DPAD_LR;
		case 7:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	}
}

static gamepad_axis gamepad_get_axis_nintendo (u16 product_id, u8 axis_id) {
	switch (product_id) {
	default:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 2:
			return GAMEPAD_AXIS_LZ; // LT
		case 3:
			return GAMEPAD_AXIS_RX;
		case 4:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_RZ; // RT
		}
	}
}

static gamepad_axis gamepad_get_axis_default (u16 product_id, u8 axis_id) {
	switch (product_id) {
	default:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 2:
			return GAMEPAD_AXIS_LZ; // LT
		case 3:
			return GAMEPAD_AXIS_RX;
		case 4:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_RZ; // RT
		}
	}
}

static gamepad_button gamepad_get_button_microsoft (u16 product_id, u8 button_id) {
	switch (product_id) {
	default:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_0:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_1:
	case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER_PACC:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_2015:
	case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_ELITE_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_1:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_S_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ONE_SXS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_ELITE_SERIES_2_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_1:
		switch (button_id) {
		case 0:
			return GAMEPAD_BUTTON_SOUTH;
		case 1:
			return GAMEPAD_BUTTON_EAST;
		case 2:
			return GAMEPAD_BUTTON_WEST;
		case 3:
			return GAMEPAD_BUTTON_NORTH;
		case 4:
			return GAMEPAD_BUTTON_L1; // LB
		case 5:
			return GAMEPAD_BUTTON_R1; // RB
		case 6:
			return GAMEPAD_BUTTON_SELECT; // back
		case 7:
			return GAMEPAD_BUTTON_START;
		case 10:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 8:
			return GAMEPAD_BUTTON_L3;
		case 9:
			return GAMEPAD_BUTTON_R3;
		case 11:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 12:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 13:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 14:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		default:
			return GAMEPAD_BUTTON_UNKNOWN;
		}
	}
}

static gamepad_button gamepad_get_button_sony (u16 product_id, u8 button_id) {
	switch (product_id) {
	case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
		switch (button_id) {
		case 2:
			return GAMEPAD_BUTTON_SOUTH;
		case 1:
			return GAMEPAD_BUTTON_EAST;
		case 0:
			return GAMEPAD_BUTTON_NORTH;
		case 3:
			return GAMEPAD_BUTTON_WEST;
		case 6:
			return GAMEPAD_BUTTON_L1;
		case 7:
			return GAMEPAD_BUTTON_R1;
		case 4:
			return GAMEPAD_BUTTON_L2;
		case 5:
			return GAMEPAD_BUTTON_R2;
		case 9:
			return GAMEPAD_BUTTON_SELECT;
		case 8:
			return GAMEPAD_BUTTON_START;
		case 12:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 10:
			return GAMEPAD_BUTTON_L3;
		case 11:
			return GAMEPAD_BUTTON_R3;
			// These are axes on PS3 and not mapped to button ids.
		case 13:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 14:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 15:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 16:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		default:
			return GAMEPAD_BUTTON_UNKNOWN;
		}
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
		switch (button_id) {
		case 1:
			return GAMEPAD_BUTTON_SOUTH;
		case 2:
			return GAMEPAD_BUTTON_EAST;
		case 3:
			return GAMEPAD_BUTTON_NORTH;
		case 0:
			return GAMEPAD_BUTTON_WEST;
		case 4:
			return GAMEPAD_BUTTON_L1;
		case 5:
			return GAMEPAD_BUTTON_R1;
		case 6:
			return GAMEPAD_BUTTON_L2;
		case 7:
			return GAMEPAD_BUTTON_R2;
		case 8:
			return GAMEPAD_BUTTON_SELECT;
		case 9:
			return GAMEPAD_BUTTON_START;
		case 12:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 10:
			return GAMEPAD_BUTTON_L3;
		case 11:
			return GAMEPAD_BUTTON_R3;
		case 13:
			// touchpad click
			return GAMEPAD_BUTTON_ADDL0;
		case 14:
			// PS5 mic mute button
			return GAMEPAD_BUTTON_ADDL1;
		default:
			return GAMEPAD_BUTTON_UNKNOWN;
		}
	}
}

static gamepad_button gamepad_get_button_nintendo (u16 product_id, u8 button_id) {
	switch (product_id) {

	default:
		switch (button_id) {
		default:
		case 0:
			return GAMEPAD_BUTTON_SOUTH;
		case 1:
			return GAMEPAD_BUTTON_EAST;
		case 2:
			return GAMEPAD_BUTTON_NORTH;
		case 3:
			return GAMEPAD_BUTTON_WEST;
		case 4:
			return GAMEPAD_BUTTON_L1;
		case 5:
			return GAMEPAD_BUTTON_R1;
		case 6:
			return GAMEPAD_BUTTON_L2;
		case 7:
			return GAMEPAD_BUTTON_R2;
		case 8:
			return GAMEPAD_BUTTON_SELECT;
		case 9:
			return GAMEPAD_BUTTON_START;
		case 10:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 11:
			return GAMEPAD_BUTTON_L3;
		case 12:
			return GAMEPAD_BUTTON_R3;
		case 13:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 14:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 15:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 16:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		}
	}
}

static gamepad_button gamepad_get_button_default (u16 product_id, u8 button_id) {
	switch (product_id) {

	default:
		switch (button_id) {
		default:
		case 0:
			return GAMEPAD_BUTTON_SOUTH;
		case 1:
			return GAMEPAD_BUTTON_EAST;
		case 2:
			return GAMEPAD_BUTTON_NORTH;
		case 3:
			return GAMEPAD_BUTTON_WEST;
		case 4:
			return GAMEPAD_BUTTON_L1;
		case 5:
			return GAMEPAD_BUTTON_R1;
		case 6:
			return GAMEPAD_BUTTON_L2;
		case 7:
			return GAMEPAD_BUTTON_R2;
		case 8:
			return GAMEPAD_BUTTON_SELECT;
		case 9:
			return GAMEPAD_BUTTON_START;
		case 10:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 11:
			return GAMEPAD_BUTTON_L3;
		case 12:
			return GAMEPAD_BUTTON_R3;
		case 13:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 14:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 15:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 16:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		}
	}
}

gamepad_axis gamepad_get_axis (u16 vendor_id, u16 product_id, u8 axis_id) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_axis_microsoft(product_id, axis_id);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_axis_sony(product_id, axis_id);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_axis_nintendo(product_id, axis_id);
	default:
		return gamepad_get_axis_default(product_id, axis_id);
	}
}

gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u8 button_id) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_button_microsoft(product_id, button_id);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_button_sony(product_id, button_id);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_button_nintendo(product_id, button_id);
	default:
		return gamepad_get_button_default(product_id, button_id);
	}
}

#endif // KPLATFORM_WINDOWS
