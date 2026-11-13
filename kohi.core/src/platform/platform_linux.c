/**
 * @file platform_linux.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "platform.h" // must be included here to get platform selection below.

// Linux platform layer.
#if KPLATFORM_LINUX
// #include <X11/extensions/Xrender.h>
// #include <xcb/xproto.h>
#	define _POSIX_C_SOURCE 200809L

// For realpath
#	define _XOPEN_SOURCE 700

#	ifndef XRANDR_ROTATION_LEFT
#		define XRANDR_ROTATION_LEFT (1 << 1)
#	endif
#	ifndef XRANDR_ROTATION_RIGHT
#		define XRANDR_ROTATION_RIGHT (1 << 9)
#	endif

#	include <stddef.h>
#	include <stdio.h>
#	include <string.h>

#	include <X11/XKBlib.h>	  // sudo apt-get install libx11-dev
#	include <X11/Xlib-xcb.h> // sudo apt-get install libxkbcommon-x11-dev libx11-xcb-dev
#	include <X11/Xlib.h>
#	include <X11/Xresource.h>
#	include <bits/time.h>

// #include <X11/extensions/Xrandr.h>
#	include <X11/keysym.h>
#	include <sys/time.h>
#	include <xcb/xcb.h>
#	include <xcb/xproto.h>
#	include <xcb/xkb.h>

// For storage queries
#	include <mntent.h>
#	include <sys/statvfs.h>
// For CPU queries
#	include <dirent.h>

#	if _POSIX_C_SOURCE >= 199309L
#		include <time.h> // nanosleep
#	endif

#	include <errno.h>	// For error reporting
#	include <stdlib.h> // Annoyingly still needed for free, since X uses malloc internally.
#	include <sys/sendfile.h>
#	include <sys/stat.h>
#	include <sys/sysinfo.h> // Processor info
#	include <sys/utsname.h>
#	include <limits.h>
#	include <linux/cdrom.h> // cdrom detection
#	include <sys/ioctl.h>	 // cdrom detection
#	include <unistd.h>
#	include <dlfcn.h> // dlopen
#	include <fcntl.h> // open, O_CREAT, etc.
#	include <dbus/dbus.h>
#	include <dbus/dbus-protocol.h>

// For gamepad support.
#	include <linux/input-event-codes.h>
#	include <linux/input.h>
#	ifndef __USE_UNIX98
#		define __USE_UNIX98
#	endif
#	include <bits/pthreadtypes.h>
#	include <pthread.h>

#	include "defines.h"
#	include "containers/darray.h"
#	include "debug/kassert.h"
#	include "input_types.h"
#	include "logger.h"
#	include "memory/kmemory.h"
#	include "strings/kstring.h"
#	include "platform/syscall_nix.h"
#	include "math/kmath.h"

#	include "kfeatures_runtime.h"

typedef struct linux_handle_info {
	xcb_connection_t *connection;
	xcb_screen_t *screen;
} linux_handle_info;

typedef struct linux_file_watch {
	u32 id;
	const char *file_path;
	b8 is_binary;
	platform_filewatcher_file_written_callback watcher_written_callback;
	void *watcher_written_context;
	platform_filewatcher_file_deleted_callback watcher_deleted_callback;
	void *watcher_deleted_context;
	long last_write_time;
	u32 update_poll_count;
} linux_file_watch;

typedef struct kwindow_platform_state {
	xcb_window_t window;
	f32 device_pixel_ratio;
} kwindow_platform_state;

typedef struct internal_clipboard_state {
	xcb_atom_t clipboard;
	xcb_atom_t targets;
	xcb_atom_t utf8;
	xcb_atom_t text_plain;
	xcb_atom_t text_plain_utf8;
	xcb_atom_t string;

	xcb_atom_t property;

	xcb_window_t requesting_window;

	b8 initialized;

	// Paste state
	b8 paste_pending;
	xcb_atom_t request_targets[4];
	u8 request_index;
	u8 request_count;

	// Owned content for copying
	kclipboard_content_type owned_type;
	u32 owned_size;
	void *owned_data;
	b8 clipboard_owned;

} internal_clipboard_state;

typedef void (*PFN_dbus_error_init)(DBusError *error);
typedef void (*PFN_dbus_error_free)(DBusError *error);
typedef dbus_bool_t (*PFN_dbus_error_is_set)(const DBusError *error);
typedef DBusMessage *(*PFN_dbus_connection_send_with_reply_and_block)(DBusConnection *connection, DBusMessage *message, int timeout_milliseconds, DBusError *error);
typedef DBusConnection *(*PFN_dbus_bus_get)(DBusBusType type, DBusError *error);
typedef void (*PFN_dbus_bus_add_match)(DBusConnection *connection, const char *rule, DBusError *error);
typedef DBusMessage *(*PFN_dbus_message_new_method_call)(const char *bus_name, const char *path, const char *iface, const char *method);
typedef void (*PFN_dbus_message_iter_init_append)(DBusMessage *message, DBusMessageIter *iter);
typedef dbus_bool_t (*PFN_dbus_message_iter_append_basic)(DBusMessageIter *iter, int type, const void *value);
typedef dbus_bool_t (*PFN_dbus_message_iter_append_fixed_array)(DBusMessageIter *iter, int element_type, const void *value, int n_elements);
typedef dbus_bool_t (*PFN_dbus_message_iter_open_container)(DBusMessageIter *iter, int type, const char *contained_signature, DBusMessageIter *sub);
typedef dbus_bool_t (*PFN_dbus_message_iter_close_container)(DBusMessageIter *iter, DBusMessageIter *sub);
typedef dbus_bool_t (*PFN_dbus_message_get_args)(DBusMessage *message, DBusError *error, int first_arg_type, ...);
typedef DBusMessage *(*PFN_dbus_connection_pop_message)(DBusConnection *connection);
typedef dbus_bool_t (*PFN_dbus_message_is_signal)(DBusMessage *message, const char *iface, const char *signal_name);
typedef const char *(*PFN_dbus_message_get_path)(DBusMessage *message);
typedef void (*PFN_dbus_message_unref)(DBusMessage *message);
typedef dbus_bool_t (*PFN_dbus_connection_read_write)(DBusConnection *connection, int timeout_milliseconds);
typedef dbus_bool_t (*PFN_dbus_message_iter_init)(DBusMessage *message, DBusMessageIter *iter);
typedef dbus_bool_t (*PFN_dbus_message_iter_next)(DBusMessageIter *iter);
typedef void (*PFN_dbus_message_iter_recurse)(DBusMessageIter *iter, DBusMessageIter *sub);
typedef int (*PFN_dbus_message_iter_get_arg_type)(DBusMessageIter *iter);
typedef void (*PFN_dbus_message_iter_get_basic)(DBusMessageIter *iter, void *value);

#	define KDECLARE_DBUS_PFN(name) PFN_##name name

#	define KLOAD_DBUS_PFN(name)                                                         \
		state->name = dlsym(state->dbus_lib, #name);                                     \
		if (!state->name) {                                                              \
			KERROR("Linux platform layer failed to load PFN '%s' from dbus-1.0.", #name) \
			return false;                                                                \
		}

// NOTE: to trace gamepad threading/mutex issues, uncomment the below line.
/* #define TRACE_GAMEPAD_THREADING 1 */

typedef enum platform_gamepad_event_type {
	PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_ATTACHED,
	PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_DETACHED,
	PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED,
	PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED,
	PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED
} platform_gamepad_event_type;

typedef struct platform_gamepad_event_data {
	gamepad_button button;
	gamepad_axis axis;
	u8 device_id;
	b8 is_down;
	u8 unused;
	f32 timestamp;
	f32 value;
	f32 last_value;
} platform_gamepad_event_data;

typedef struct platform_gamepad_event {
	u32 device_id;
	platform_gamepad_event_type type;
	platform_gamepad_event_data data;
} platform_gamepad_event;

#	define PLATFORM_GAMEPAD_MAX_AXES 128
#	define PLATFORM_GAMEPAD_MAX_BUTTONS 128

typedef struct platform_gamepad_device {
	u32 device_id;
	const char *description;
	i32 vendor_id;
	i32 product_id;
	u8 axis_count;
	u8 button_count;
	f32 axis_states[PLATFORM_GAMEPAD_MAX_AXES];
	// Bit flags - 1 = pressed, 0 = released.
	u32 button_states;

	pthread_t thread;
	i32 fd;
	// evdev path
	char *path;
	// input path
	char *input_path;
	char *input_name;
	// Driver button codes to internally-kept codes (which are indices into button_states)
	i32 button_map[KEY_CNT - BTN_MISC];
	i32 axis_map[ABS_CNT];
	struct input_absinfo axis_info[ABS_CNT];
} platform_gamepad_device;

typedef enum platform_gamepad_state_flags {
	PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT = 0x01,
	PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT = 0x02,
} platform_gamepad_state_flags;

typedef u32 platform_gamepad_state_flag_bits;

typedef struct __attribute__((aligned(8))) platform_gamepad_state {
	// NOTE: Putting these first since the alignment here matters for mutexes.
	pthread_mutex_t devices_mutex;
	pthread_mutex_t event_queue_mutex;

	// darray
	platform_gamepad_device **devices;
	u32 next_device_id;
	time_t last_input_stat_time;

	// darray
	platform_gamepad_event *event_queue;

	platform_gamepad_state_flag_bits flags;

	f64 start_time;
} platform_gamepad_state;

typedef struct platform_state {
	Display *display;
	linux_handle_info handle;
	// NOTE: May need to be part of window.
	xcb_screen_t *screen;
	xcb_atom_t wm_protocols;
	xcb_atom_t wm_delete_win;
	i32 screen_count;
	// darray
	linux_file_watch *watches;

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

	u8 last_keycode;
	u32 last_key_time;

	internal_clipboard_state clipboard;

	void *dbus_lib;
	KDECLARE_DBUS_PFN (dbus_error_init);
	KDECLARE_DBUS_PFN (dbus_error_free);
	KDECLARE_DBUS_PFN (dbus_error_is_set);
	KDECLARE_DBUS_PFN (dbus_connection_send_with_reply_and_block);
	KDECLARE_DBUS_PFN (dbus_bus_get);
	KDECLARE_DBUS_PFN (dbus_bus_add_match);
	KDECLARE_DBUS_PFN (dbus_message_new_method_call);
	KDECLARE_DBUS_PFN (dbus_message_iter_init_append);
	KDECLARE_DBUS_PFN (dbus_message_iter_append_basic);
	KDECLARE_DBUS_PFN (dbus_message_iter_append_fixed_array);
	KDECLARE_DBUS_PFN (dbus_message_iter_open_container);
	KDECLARE_DBUS_PFN (dbus_message_iter_close_container);
	KDECLARE_DBUS_PFN (dbus_message_get_args);
	KDECLARE_DBUS_PFN (dbus_connection_pop_message);
	KDECLARE_DBUS_PFN (dbus_message_is_signal);
	KDECLARE_DBUS_PFN (dbus_message_get_path);
	KDECLARE_DBUS_PFN (dbus_message_unref);
	KDECLARE_DBUS_PFN (dbus_connection_read_write);
	KDECLARE_DBUS_PFN (dbus_message_iter_init);
	KDECLARE_DBUS_PFN (dbus_message_iter_next);
	KDECLARE_DBUS_PFN (dbus_message_iter_recurse);
	KDECLARE_DBUS_PFN (dbus_message_iter_get_arg_type);
	KDECLARE_DBUS_PFN (dbus_message_iter_get_basic);

	platform_gamepad_state *gamepad_state;
} platform_state;

typedef struct ofd_file_filter {
	const char *name;
	const char **patterns;
	u64 pattern_count;
} ofd_file_filter;

static platform_state *state_ptr = KNULL;

static void platform_update_watches (void);
static b8 key_is_repeat (platform_state *state, const xcb_key_press_event_t *ev);
// Key translation
static keys translate_keycode (u32 x_keycode);
static kwindow *window_from_handle (xcb_window_t window);

#	define OFD_DBUS_PORTAL_NAME "org.freedesktop.portal.Desktop"
#	define OFD_DBUS_PORTAL_PATH "/org/freedesktop/portal/desktop"
#	define OFD_DBUS_PORTAL_INTERFACE "org.freedesktop.portal.FileChooser"
#	define OFD_DBUS_REQUEST_INTERFACE "org.freedesktop.portal.Request"

static i32 kdbus_append_bool_variant (DBusMessageIter *dict, const char *key, b8 value);
// Appends a dictionary entry containing a string variant.
static i32 kdbus_append_string_variant (DBusMessageIter *dict, const char *key, const char *value);
// Appends a dictionary entry containing a byte-array variant.
static i32 kdbus_append_byte_array_variant (DBusMessageIter *dict, const char *key, const u8 *bytes, i32 length);
static i32 kdbus_append_filters_variant (DBusMessageIter *dict, const ofd_file_filter *filters, u64 filter_count);
// >= 0 = number of paths obtained, 0 = user cancelled, -1 = internal error
static char **extract_selected_path (DBusMessage *message, i32 *out_result);

static xcb_atom_t intern_atom (xcb_connection_t *conn, const char *name) {
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, 0, string_length(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, cookie, KNULL);

	xcb_atom_t atom = reply ? reply->atom : XCB_NONE;
	free(reply);

	return atom;
}

static const char *atom_name (xcb_connection_t *conn, xcb_atom_t atom) {
	xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(conn, atom);
	xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply(conn, cookie, KNULL);

	if (!reply) {
		return KNULL;
	}

	i32 len = xcb_get_atom_name_name_length(reply);
	char *name = kallocate(len + 1, MEMORY_TAG_STRING);
	kcopy_memory(name, xcb_get_atom_name_name(reply), len);
	name[len] = 0;

	free(reply);

	return name;
}

static b8 enable_detectable_autorepeat (xcb_connection_t *conn) {
	// Initialize xkb extension
	xcb_xkb_use_extension_cookie_t uc = xcb_xkb_use_extension(conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);
	xcb_xkb_use_extension_reply_t *ur = xcb_xkb_use_extension_reply(conn, uc, NULL);
	if (!ur) {
		return false;
	}

	b8 ok = (ur->supported);

	free(ur);
	if (!ok) {
		return false;
	}

	// Attempt to set detectable auto repeat for this client
	xcb_xkb_per_client_flags_cookie_t pcfc = xcb_xkb_per_client_flags(
		conn,
		XCB_XKB_ID_USE_CORE_KBD,
		XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
		XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT,
		0, 0, 0);
	xcb_xkb_per_client_flags_reply_t *pfr = xcb_xkb_per_client_flags_reply(conn, pcfc, NULL);
	if (!pfr) {
		return false;
	}

	b8 turned_on = (pfr->value & XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT) != 0;
	free(pfr);
	return turned_on;
}

#	define test_bit(idx, array) ((array[(idx) / (sizeof(i32) * 8)] >> ((idx) % (sizeof(i32) * 8))) & 0x1)

static void detect_gamepad_devices (struct platform_state *state);
static void *gamepad_device_thread_do_work (void *context);
static void gamepad_queue_event (u32 device_id, platform_gamepad_event_type type, platform_gamepad_event_data event_data);
static void gamepad_queue_axis_event (platform_gamepad_device *device, f32 timestamp, gamepad_axis axis, f32 value, f32 last_value);
static void gamepad_queue_button_event (platform_gamepad_device *device, f32 timestamp, gamepad_button button, b8 is_down);
static void process_gamepad_events (platform_state *state);
static void cleanup_gamepad_device (u32 device_id);
static platform_gamepad_device *get_gamepad_device_by_id (u32 device_id);
static gamepad_axis gamepad_get_axis (u16 vendor_id, u16 product_id, u8 axis_id);
static gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u32 button_id);
static b8 gamepad_try_set_player_index_led (platform_gamepad_device *device, i32 index);

b8 platform_system_startup (u64 *memory_requirement, struct platform_state *state, platform_system_config *config) {
	*memory_requirement = sizeof(platform_state);
	if (state == 0) {
		return true;
	}

	state_ptr = state;

	// Connect to X
	state->display = XOpenDisplay(NULL);

	// Retrieve the connection from the display.
	state->handle.connection = XGetXCBConnection(state->display);

	if (xcb_connection_has_error(state->handle.connection)) {
		KFATAL("Failed to connect to X server via XCB.");
		return false;
	}

	b8 detectable_repeat = enable_detectable_autorepeat(state->handle.connection);
	KINFO("XCB: %s detectable auto-repeat.", detectable_repeat ? "Enabled " : "Could not enable ");

	// Get data from the X server
	const struct xcb_setup_t *setup = xcb_get_setup(state->handle.connection);

	// TODO: Does this need to be associated with the window?
	// Loop through screens using iterator
	xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
	for (i32 s = 0; s < state->screen_count; ++s) {
		/* f32 w_inches = it.data->width_in_millimeters * 0.0394;
		f32 h_inches = it.data->height_in_millimeters * 0.0394;
		f32 x_dpi = (f32)it.data->width_in_pixels / w_inches;

		KINFO("Monitor '%s' has a DPI of %.2f for a device pixelratio of %0.2f", it.index, x_dpi, x_dpi / 96.0f);
		// state->device_pixel_ratio = x_dpi / 96.0f;  // Default DPI is considered 96. */

		xcb_screen_next(&it);
	}

	// After screens have been looped through, assign it.
	state->screen = it.data;
	state->handle.screen = state->screen;

	state->windows = darray_create(kwindow *);

	// DBus functions are loaded dynamically to avoid the hard dependency on it.
	// Things depending on it won't execute if it doesn't load, but it won't prevent
	// the entire system from running this way.
	state->dbus_lib = dlopen("libdbus-1.so.3", RTLD_LOCAL | RTLD_NOW);
	if (!state->dbus_lib) {
		state->dbus_lib = dlopen("libdbus-1.so", RTLD_LOCAL | RTLD_NOW);
	}

	if (state->dbus_lib) {
		KLOAD_DBUS_PFN(dbus_error_init);
		KLOAD_DBUS_PFN(dbus_error_free);
		KLOAD_DBUS_PFN(dbus_error_is_set);
		KLOAD_DBUS_PFN(dbus_connection_send_with_reply_and_block);
		KLOAD_DBUS_PFN(dbus_bus_get);
		KLOAD_DBUS_PFN(dbus_bus_add_match);
		KLOAD_DBUS_PFN(dbus_message_new_method_call);
		KLOAD_DBUS_PFN(dbus_message_iter_init_append);
		KLOAD_DBUS_PFN(dbus_message_iter_append_basic);
		KLOAD_DBUS_PFN(dbus_message_iter_append_fixed_array);
		KLOAD_DBUS_PFN(dbus_message_iter_open_container);
		KLOAD_DBUS_PFN(dbus_message_iter_close_container);
		KLOAD_DBUS_PFN(dbus_message_get_args);
		KLOAD_DBUS_PFN(dbus_connection_pop_message);
		KLOAD_DBUS_PFN(dbus_message_is_signal);
		KLOAD_DBUS_PFN(dbus_message_get_path);
		KLOAD_DBUS_PFN(dbus_message_unref);
		KLOAD_DBUS_PFN(dbus_connection_read_write);
		KLOAD_DBUS_PFN(dbus_message_iter_init);
		KLOAD_DBUS_PFN(dbus_message_iter_next);
		KLOAD_DBUS_PFN(dbus_message_iter_recurse);
		KLOAD_DBUS_PFN(dbus_message_iter_get_arg_type);
		KLOAD_DBUS_PFN(dbus_message_iter_get_basic);
	} else {
		KERROR("Linux platform error: Unable to load sd-bus. Some platform functions (such as browsing for files) may not be available.");
	}

	// Gamepad initialization is called later via platform_gamepads_initialize().
	state->gamepad_state = kallocate_aligned(sizeof(platform_gamepad_state), 8, MEMORY_TAG_PLATFORM);
	kzero_memory(state->gamepad_state, sizeof(platform_gamepad_state));

	return true;
}

void platform_system_shutdown (struct platform_state *state) {
	if (state) {
		// Gamepad shutdown.
		kzero_memory(&state->gamepad_callbacks, sizeof(state->gamepad_callbacks));
		if (FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {

			pthread_mutex_lock(&state->gamepad_state->devices_mutex);
			u32 device_count = darray_length(state->gamepad_state->devices);
			for (u32 i = 0; i < device_count; ++i) {
				platform_gamepad_device *device = state->gamepad_state->devices[i];
				pthread_cancel(device->thread);
				pthread_join(device->thread, KNULL);
				cleanup_gamepad_device(device->device_id);
			}
			pthread_mutex_unlock(&state->gamepad_state->devices_mutex);

			pthread_mutex_destroy(&state->gamepad_state->devices_mutex);
			pthread_mutex_destroy(&state->gamepad_state->event_queue_mutex);
			darray_destroy(state->gamepad_state->devices);

			kzero_memory(state->gamepad_state, sizeof(*state->gamepad_state));
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
		if (state->watches) {
			u32 len = darray_length(state->watches);
			for (u32 i = 0; i < len; ++i) {
				string_free(state->watches[i].file_path);
			}
			darray_destroy(state->watches);
		}

		xcb_flush(state->handle.connection);
		XCloseDisplay(state->display);
		state->display = KNULL;
		state->handle.connection = KNULL;
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

	window->width = client_width;
	window->height = client_height;

	window->platform_state = kallocate(sizeof(kwindow_platform_state), MEMORY_TAG_PLATFORM);

	// Allocate a XID for the window to be created.
	window->platform_state->window = xcb_generate_id(state_ptr->handle.connection);

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
		XCB_COPY_FROM_PARENT, // depth
		window->platform_state->window,
		state_ptr->screen->root,	   // parent
		client_x,					   // typed_config->x,               // x
		client_y,					   // typed_config->y,               // y
		client_width,				   // typed_config->width,           // width
		client_height,				   // typed_config->height,          // height
		0,							   // No border
		XCB_WINDOW_CLASS_INPUT_OUTPUT, // class
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

	platform_window_title_set(window, config->title);

	// Tell the server to notify when the window manager
	// attempts to destroy the window.
	xcb_intern_atom_cookie_t wm_delete_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, string_length("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
	xcb_intern_atom_cookie_t wm_protocols_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, string_length("WM_PROTOCOLS"), "WM_PROTOCOLS");
	xcb_intern_atom_reply_t *wm_delete_reply = xcb_intern_atom_reply(state_ptr->handle.connection, wm_delete_cookie, NULL);
	xcb_intern_atom_reply_t *wm_protocols_reply = xcb_intern_atom_reply(state_ptr->handle.connection, wm_protocols_cookie, NULL);
	state_ptr->wm_delete_win = wm_delete_reply->atom;
	state_ptr->wm_protocols = wm_protocols_reply->atom;

	xcb_change_property(
		state_ptr->handle.connection,
		XCB_PROP_MODE_REPLACE,
		window->platform_state->window,
		wm_protocols_reply->atom,
		4,
		32,
		1,
		&wm_delete_reply->atom);

	free(wm_delete_reply);
	free(wm_protocols_reply);

	// Map the window to the screen
	xcb_map_window(state_ptr->handle.connection, window->platform_state->window);

	if (!state_ptr->clipboard.initialized) {
		state_ptr->clipboard.clipboard = intern_atom(state_ptr->handle.connection, "CLIPBOARD");
		state_ptr->clipboard.targets = intern_atom(state_ptr->handle.connection, "TARGETS");
		state_ptr->clipboard.utf8 = intern_atom(state_ptr->handle.connection, "UTF8_STRING");
		state_ptr->clipboard.text_plain = intern_atom(state_ptr->handle.connection, "text/plain");
		state_ptr->clipboard.text_plain_utf8 = intern_atom(state_ptr->handle.connection, "text/plain;charset=utf-8");
		state_ptr->clipboard.string = intern_atom(state_ptr->handle.connection, "STRING");
		state_ptr->clipboard.property = intern_atom(state_ptr->handle.connection, "X11_CLIP_TEMP");
		state_ptr->clipboard.paste_pending = false;
		state_ptr->clipboard.clipboard_owned = false;

		state_ptr->clipboard.initialized = true;
	}

	// Flush the stream
	i32 stream_result = xcb_flush(state_ptr->handle.connection);
	if (stream_result <= 0) {
		KFATAL("An error occurred when flusing the stream: %d", stream_result);
		return false;
	}

	// Register the window internally.
	darray_push(state_ptr->windows, &window);

	return true;
}

void platform_window_destroy (struct kwindow *window) {
	if (window) {
		u32 len = darray_length(state_ptr->windows);
		for (u32 i = 0; i < len; ++i) {
			if (state_ptr->windows[i] == window) {
				string_free(window->name);
				string_free(window->title);
				xcb_destroy_window(state_ptr->handle.connection, window->platform_state->window);
				kfree(window->platform_state);
				window->platform_state = KNULL;
				state_ptr->windows[i] = KNULL;
				return;
			}
		}
		KERROR("Destroying a window that was somehow not registered with the platform layer.");
		xcb_destroy_window(state_ptr->handle.connection, window->platform_state->window);
		window->platform_state->window = 0;
	}
}

b8 platform_window_show (struct kwindow *window) {
	if (!window) {
		return false;
	}
	// Show the window
	xcb_map_window(state_ptr->handle.connection, window->platform_state->window);
	// Flush the stream
	i32 stream_result = xcb_flush(state_ptr->handle.connection);
	if (stream_result <= 0) {
		KFATAL("An error occurred when flusing the stream: %d", stream_result);
		return false;
	}

	return true;
}

b8 platform_window_hide (struct kwindow *window) {
	if (!window) {
		return false;
	}

	// Hide the window
	xcb_unmap_window(state_ptr->handle.connection, window->platform_state->window);
	// Flush the stream
	i32 stream_result = xcb_flush(state_ptr->handle.connection);
	if (stream_result <= 0) {
		KFATAL("An error occurred when flusing the stream: %d", stream_result);
		return false;
	}

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

	if (title) {
		window->title = string_duplicate(title);
	} else {
		window->title = string_duplicate("Kohi Game Engine Window");
	}

	xcb_intern_atom_cookie_t utf8_string_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, 11, "UTF8_STRING");
	xcb_intern_atom_reply_t *utf8_string_reply = xcb_intern_atom_reply(state_ptr->handle.connection, utf8_string_cookie, 0);

	xcb_intern_atom_cookie_t net_wm_name_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, 12, "_NET_WM_NAME");
	xcb_intern_atom_reply_t *net_wm_name_reply = xcb_intern_atom_reply(state_ptr->handle.connection, net_wm_name_cookie, 0);

	// Change the title
	xcb_change_property(
		state_ptr->handle.connection,
		XCB_PROP_MODE_REPLACE,
		window->platform_state->window,
		XCB_ATOM_WM_NAME,
		utf8_string_reply->atom, // XCB_ATOM_STRING
		8,						 // data should be viewed 8 bits at a time
		string_length(window->title),
		window->title);

	xcb_change_property(
		state_ptr->handle.connection,
		XCB_PROP_MODE_REPLACE,
		window->platform_state->window,
		net_wm_name_reply->atom, // XCB_ATOM_WM_NAME,
		utf8_string_reply->atom, // XCB_ATOM_STRING
		8,						 // data should be viewed 8 bits at a time
		string_length(window->title),
		window->title);

	free(utf8_string_reply);
	free(net_wm_name_reply);

	return true;
}

static void clipboard_retry_next_target (internal_clipboard_state *cb) {

	cb->request_index++;
	if (cb->request_index >= cb->request_count) {
		cb->paste_pending = false;
		return;
	}

	xcb_convert_selection(
		state_ptr->handle.connection,
		cb->requesting_window,
		cb->clipboard,
		cb->request_targets[cb->request_index],
		cb->property,
		XCB_CURRENT_TIME);

	xcb_flush(state_ptr->handle.connection);
}

b8 platform_pump_messages (void) {
	if (state_ptr) {
		xcb_generic_event_t *event;
		xcb_client_message_event_t *cm;

		b8 quit_flagged = false;

		// Poll for events until null is returned.
		while ((event = xcb_poll_for_event(state_ptr->handle.connection))) {
			// Input events - The reason for the & ~0x80 being done on X11 events is that
			// every event has an 8bit code, and the most significant bit in the code is set
			// if the event was generated from a SendEvent request
			// (like from a window manager vs the X server itself.)
			// & ~0x80 = 0111 1111, so anding it to the event clears that top bit ensuring
			// the event code will always match regardless of the event's origin.
			// Perhaps just anding by 0x7F would be better?
			switch (event->response_type & ~0x80) {
			case XCB_KEY_PRESS:
			case XCB_KEY_RELEASE: {
				// Key press event - xcb_key_press_event_t and xcb_key_release_event_t are the same
				xcb_key_press_event_t *kb_event = (xcb_key_press_event_t *)event;
				b8 pressed = event->response_type == XCB_KEY_PRESS;
				xcb_keycode_t code = kb_event->detail;
				KeySym key_sym = XkbKeycodeToKeysym(
					state_ptr->display,
					(KeyCode)code, // event.xkey.keycode,
					0,
					0 /*code & ShiftMask ? 1 : 0*/);

				keys key = translate_keycode(key_sym);

				b8 is_repeat = key_is_repeat(state_ptr, kb_event);

				// Pass to the input subsystem for processing.
				state_ptr->process_key(key, pressed, is_repeat);

				// Also pass a general "shift" key if either left or right are triggered.
				if (key == K_KEY_LSHIFT || key == K_KEY_RSHIFT) {
					state_ptr->process_key(K_KEY_SHIFT, pressed, is_repeat);
				}

				// Also pass a general "control" key if either left or right are triggered.
				if (key == K_KEY_LCONTROL || key == K_KEY_RCONTROL) {
					state_ptr->process_key(K_KEY_CONTROL, pressed, is_repeat);
				}
			} break;
			case XCB_BUTTON_PRESS:
			case XCB_BUTTON_RELEASE: {
				xcb_button_press_event_t *mouse_event = (xcb_button_press_event_t *)event;
				b8 pressed = event->response_type == XCB_BUTTON_PRESS;
				mouse_buttons mouse_button = MOUSE_BUTTON_MAX;

				if (mouse_event->detail <= XCB_BUTTON_INDEX_3) {
					switch (mouse_event->detail) {
					case XCB_BUTTON_INDEX_1:
						mouse_button = MOUSE_BUTTON_LEFT;
						break;
					case XCB_BUTTON_INDEX_2:
						mouse_button = MOUSE_BUTTON_MIDDLE;
						break;
					case XCB_BUTTON_INDEX_3:
						mouse_button = MOUSE_BUTTON_RIGHT;
						break;
					}

					// Pass over to the input subsystem.
					if (mouse_button != MOUSE_BUTTON_MAX) {
						state_ptr->process_mouse_button(mouse_button, pressed);
					}
				} else if (mouse_event->detail == XCB_BUTTON_INDEX_4 || mouse_event->detail == XCB_BUTTON_INDEX_5) {
					i8 delta = 0;
					switch (mouse_event->detail) {
					case XCB_BUTTON_INDEX_4:
						delta = 1;
						break;
					case XCB_BUTTON_INDEX_5:
						delta = -1;
						break;
					}

					state_ptr->process_mouse_wheel(delta);
				}
			} break;
			case XCB_MOTION_NOTIFY: {
				// Mouse move
				xcb_motion_notify_event_t *move_event = (xcb_motion_notify_event_t *)event;

				// Pass over to the input subsystem.
				state_ptr->process_mouse_move(move_event->event_x, move_event->event_y);
			} break;
			case XCB_CONFIGURE_NOTIFY: {
				// Resizing - note that this is also triggered by moving the window, but should be
				// passed anyway since a change in the x/y could mean an upper-left resize.
				// The application layer can decide what to do with this.
				xcb_configure_notify_event_t *configure_event = (xcb_configure_notify_event_t *)event;

				u16 width = configure_event->width;
				u16 height = configure_event->height;

				kwindow *w = window_from_handle(configure_event->window);
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

			case XCB_CLIENT_MESSAGE: {
				cm = (xcb_client_message_event_t *)event;

				// Window close
				if (cm->data.data32[0] == state_ptr->wm_delete_win) {
					quit_flagged = true;
				}
			} break;
			case XCB_SELECTION_CLEAR: {

				// Clipboard ownership lost (another app copied)

				xcb_selection_clear_event_t *clear_event = (xcb_selection_clear_event_t *)event;
				if (clear_event->selection == state_ptr->clipboard.clipboard) {

					state_ptr->clipboard.clipboard_owned = false;
					if (state_ptr->clipboard.owned_data) {
						if (state_ptr->clipboard.owned_type == KCLIPBOARD_CONTENT_TYPE_STRING) {
							string_free(state_ptr->clipboard.owned_data);
						} else {
							kfree(state_ptr->clipboard.owned_data);
						}
						state_ptr->clipboard.owned_data = KNULL;
						state_ptr->clipboard.owned_size = 0;
					}
				}
			} break;
			case XCB_SELECTION_NOTIFY: {

				internal_clipboard_state *cb = &state_ptr->clipboard;

				xcb_selection_notify_event_t *selection_event = (xcb_selection_notify_event_t *)event;

				if (selection_event->requestor == cb->requesting_window) {
					if (cb->paste_pending) {
						// Pasting reply.
						if (selection_event->property == XCB_NONE) {
							// Retry with next type.
							clipboard_retry_next_target(cb);
							break;
						}

						xcb_get_property_cookie_t prop_cookie = xcb_get_property(
							state_ptr->handle.connection,
							false,
							cb->requesting_window,
							cb->property,
							XCB_GET_PROPERTY_TYPE_ANY,
							0,
							UINT32_MAX);

						xcb_get_property_reply_t *prop = xcb_get_property_reply(state_ptr->handle.connection, prop_cookie, KNULL);

						if (!prop) {
							clipboard_retry_next_target(cb);
							break;
						}

						i32 len = xcb_get_property_value_length(prop);
						const void *val = xcb_get_property_value(prop);

						if (len > 0 && val) {

							// TODO: determine content type
							kclipboard_context ctx = {
								.requesting_window = window_from_handle(cb->requesting_window),
								.content_type = KCLIPBOARD_CONTENT_TYPE_STRING,
								.content = 0,
								.size = 0};

							// For strings, be sure to null-terminate them.
							if (ctx.content_type == KCLIPBOARD_CONTENT_TYPE_STRING) {
								ctx.content = kallocate(len + 1, MEMORY_TAG_STRING);
								kcopy_memory((void *)ctx.content, val, len);
								((char *)ctx.content)[len] = 0;
							} else {
								ctx.content = val;
								ctx.size = len;
							}

							if (state_ptr->on_paste) {
								state_ptr->on_paste(ctx);
							}

							if (ctx.content_type == KCLIPBOARD_CONTENT_TYPE_STRING) {
								kfree((void *)ctx.content);
							}

							cb->paste_pending = false;

						} else {
							clipboard_retry_next_target(cb);
						}
						free(prop);
					}
				}

			} break;
			case XCB_SELECTION_REQUEST: {
				// Paste from external app requested.
				internal_clipboard_state *cb = &state_ptr->clipboard;
				xcb_selection_request_event_t *request_event = (xcb_selection_request_event_t *)event;
				if (cb->clipboard) {
					// Paste request (our app is the owner of the data)

					xcb_selection_notify_event_t reply = {
						.response_type = XCB_SELECTION_NOTIFY,
						.requestor = request_event->requestor,
						.selection = request_event->selection,
						.target = request_event->target,
						.time = request_event->time,
						.property = XCB_NONE};

					if (request_event->target == cb->targets) {
						xcb_atom_t supported[] = {
							cb->utf8,
							cb->text_plain_utf8,
							cb->text_plain,
							cb->string};

						xcb_change_property(
							state_ptr->handle.connection,
							XCB_PROP_MODE_REPLACE,
							request_event->requestor,
							request_event->property,
							XCB_ATOM_ATOM,
							32,
							4,
							supported);

						reply.property = request_event->property;
					} else if (
						request_event->target == cb->utf8 ||
						request_event->target == cb->text_plain_utf8 ||
						request_event->target == cb->text_plain ||
						request_event->target == cb->string) {

						xcb_change_property(
							state_ptr->handle.connection,
							XCB_PROP_MODE_REPLACE,
							request_event->requestor,
							request_event->property,
							request_event->target,
							8,
							cb->owned_size,
							cb->owned_data);

						reply.property = request_event->property;
					}

					xcb_send_event(
						state_ptr->handle.connection,
						0,
						request_event->requestor,
						0,
						(const char *)&reply);
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

		// Check every so often for new controllers.
		static u32 next_poll = 30;
		next_poll--;
		if (next_poll == 0) {
			detect_gamepad_devices(state_ptr);
			next_poll = 30;
		}

		// Also process gamepad events.
		process_gamepad_events(state_ptr);

		return !quit_flagged;
	};
	return true;
}

void *platform_allocate (u64 size, b8 aligned) {
	void *p = (void *)syscall6(
		SYS_mmap,
		0, /* let kernel choose address */
		(i64)size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0);

	/* Linux syscalls return -errno on failure. */
	if ((i64)p < 0) {
		return 0;
	}

	// TODO: track all allocations made this way so the size can be gotten later.

	return p;
}
void platform_free (const void *block, b8 aligned, u64 size) {
	if (block) {
		syscall2(SYS_munmap, (i64)block, (i64)size);
	}
}

void platform_console_write (struct platform_state *platform, log_level level, const char *message) {
	b8 is_error = (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_FATAL);
	const char *colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
	const char *output = string_format("\033[%sm%s\033[0m", colour_strings[level], message);
	i32 fd = is_error ? FD_STDERR : FD_STDOUT;
	syscall3(SYS_write, fd, (i64)output, string_length(output));
	string_free(output);
}

f64 platform_get_absolute_time (void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	return now.tv_sec + now.tv_nsec * 0.000000001;
}

void platform_sleep (u64 ms) {
#	if _POSIX_C_SOURCE >= 199309L
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000 * 1000;
	nanosleep(&ts, 0);
#	else
	if (ms >= 1000) {
		sleep(ms / 1000);
	}
	usleep((ms % 1000) * 1000);
#	endif
}

i32 platform_get_processor_count (void) {
	// Load processor info.
	i32 processor_count = get_nprocs_conf();
	i32 processors_available = get_nprocs();
	KINFO("%i processor cores detected, %i cores available.", processor_count, processors_available);
	return processors_available;
}

void platform_get_handle_info (u64 *out_size, void *memory) {
	*out_size = sizeof(linux_handle_info);
	if (!memory) {
		return;
	}

	kcopy_memory(memory, &state_ptr->handle, *out_size);
}

f32 platform_device_pixel_ratio (const struct kwindow *window) {
	return window->platform_state->device_pixel_ratio;
}

const char *platform_dynamic_library_extension (void) {
	return ".so";
}

const char *platform_dynamic_library_prefix (void) {
	return "lib";
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
		// NOTE: SSIZE_MAX = 0x7FFFFFFFFFFFFFFF
		ssize_t sent = sendfile(dest_fd, source_fd, NULL, (size >= 0x7FFFFFFFFFFFFFFF ? 0x7FFFFFFFFFFFFFFF : (size_t)size));
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
#	if 0
		struct timeval dest_times[2];
		// Update last access time.
		dest_times[0].tv_sec = source_stat.st_atime;
		dest_times[0].tv_usec = source_stat.st_atim.tv_nsec / 1000;
		// Update last modify time.
		dest_times[1].tv_sec = source_stat.st_mtime;
		dest_times[1].tv_usec = source_stat.st_mtim.tv_nsec / 1000;
		result = futimes(dest_fd, dest_times);
#	else
		struct timespec dest_times[2];

		dest_times[0] = source_stat.st_atim;
		dest_times[1] = source_stat.st_mtim;

		result = futimens(dest_fd, dest_times);
#	endif
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
		linux_file_watch *w = &state_ptr->watches[i];
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

	linux_file_watch *w = &state_ptr->watches[watch_id];
	w->id = INVALID_ID;
	kfree((void *)w->file_path);
	w->file_path = 0;
	kzero_memory(&w->last_write_time, sizeof(long));

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
		linux_file_watch *f = &state_ptr->watches[i];
		if (f->id != INVALID_ID) {
			struct stat info;
			int result = stat(f->file_path, &info);
			if (result != 0) {
				if (errno == ENOENT) {
					// File doesn't exist. Which means it was deleted. Remove the watch.
					if (f->watcher_deleted_callback) {
						f->watcher_deleted_callback(f->id, f->watcher_written_context);
					} else {
						KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_deleted_callback()");
					}
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
				f->update_poll_count = 1;
			} else {
				if (f->update_poll_count) {
					f->update_poll_count++;
					if (f->update_poll_count > 10) {
						f->update_poll_count = 0;
						if (f->watcher_written_callback) {
							f->watcher_written_callback(f->id, f->file_path, f->is_binary, f->watcher_written_context);
						} else {
							KWARN("Watcher file was deleted but no handler callback was set. Make sure to call platform_register_watcher_written_callback()");
						}
					}
				}
			}
		}
	}
}

static inline kunix_time_ns
unix_time_from_stat (const struct stat *s) {
#	if defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L
	return (kunix_time_ns)s->st_mtim.tv_sec * 1000000000ULL +
		   (kunix_time_ns)s->st_mtim.tv_nsec;
#	else
	return (kunix_time_ns)s->st_mtime * 1000000000ULL;
#	endif
}

kunix_time_ns platform_get_file_mtime (const char *path) {
	struct stat s;
	if (stat(path, &s) != 0) {
		return 0;
	}

	return unix_time_from_stat(&s);
}

typedef struct linux_core_id {
	i32 physical_id;
	i32 core_id;
} linux_core_id;

static u32 linux_physical_core_count (void) {
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		return 0;
	}

	linux_core_id cores[256];
	u32 core_count = 0;

	i32 physical_id = -1;
	i32 core_id = -1;

	char line[256];
	while (fgets(line, sizeof(line), f)) {

		if (strings_nequal(line, "physical id", 11)) {
			sscanf(line, "physical id : %d", &physical_id);
		} else if (strings_nequal(line, "core id", 7)) {
			sscanf(line, "core id : %d", &core_id);
		} else if (line[0] == '\n') {
			/* end of one processor block */
			if (physical_id >= 0 && core_id >= 0) {

				b8 found = false;
				for (u32 i = 0; i < core_count; ++i) {
					if (cores[i].physical_id == physical_id &&
						cores[i].core_id == core_id) {
						found = true;
						break;
					}
				}

				if (!found && core_count < 256) {
					cores[core_count].physical_id = physical_id;
					cores[core_count].core_id = core_id;
					core_count++;
				}
			}

			physical_id = -1;
			core_id = -1;
		}
	}

	fclose(f);
	return core_count;
}

static u32 linux_ram_speed_mhz (void) {
	FILE *f;
	char path[256];
	char buf[64];
	u32 speed = 0;
	u32 count = 0;

	/* Try memory device entries */
	for (int i = 0; i < 32; ++i) {
		ksnprintf(path, sizeof(path),
				  "/sys/devices/system/memory/memory%d/dimm_speed", i);

		f = fopen(path, "r");
		if (!f)
			continue;

		if (fgets(buf, sizeof(buf), f)) {
			u32 mhz = 0;
			string_to_u32(buf, &mhz);
			if (mhz > 0) {
				speed += mhz;
				count++;
			}
		}
		fclose(f);
	}

	if (count > 0)
		return speed / count;

	return 0; /* unknown */
}

static void linux_cpu (ksystem_info *s) {
	FILE *f = fopen("/proc/cpuinfo", "r");
	if (!f) {
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strings_nequal(line, "model name", 10)) {
			sscanf(line, "model name : %[^\n]", s->cpu_name);
		} else if (strings_nequal(line, "cpu MHz", 7)) {
			// Grab this first in case /sys/.../cpuinfo_max_freq isn't available.
			f64 mhz = 0;
			sscanf(line, "cpu MHz : %lf", &mhz);
			s->cpu_mhz = (u32)mhz;
		}
	}
	fclose(f);

	s->logical_cores = sysconf(_SC_NPROCESSORS_ONLN);
	s->physical_cores = linux_physical_core_count();
	if (s->physical_cores == 0) {
		// This can happen in docker, flatpak, snap
		// derived/unreliable
		s->physical_cores = s->logical_cores;
	}

	// Attempt to get the base CPU clock speed mhz.
	{
		DIR *dir = opendir("/sys/devices/system/cpu/");
		if (dir) {

			struct dirent *entry;
			u32 freq_khz = 0;
			while ((entry = readdir(dir)) != NULL) {
				if (strings_nequal(entry->d_name, "cpu", 3) && kisdigit(entry->d_name[3])) {
					char path[256];
					ksnprintf(path, sizeof(path),
							  "/sys/devices/system/cpu/%s/cpufreq/cpuinfo_max_freq",
							  entry->d_name);

					FILE *f = fopen(path, "r");
					if (!f)
						continue;

					unsigned long val = 0;
					if (fscanf(f, "%lu", &val) == 1) {
						if (val > freq_khz)
							freq_khz = val;
					}
					fclose(f);
					break; // just read first CPU
				}
			}
			closedir(dir);
			s->cpu_mhz = freq_khz / 1000; // MHz
		}
	}

	detect_x86_features(&s->features);
	detect_arm_features(&s->features);
}

static void linux_ram (ksystem_info *s) {
	struct sysinfo info;
	sysinfo(&info);
	s->ram_total_bytes = (u64)info.totalram * info.mem_unit;
	s->ram_available_bytes = (u64)info.freeram * info.mem_unit;
	s->ram_speed_mhz = linux_ram_speed_mhz();
}

static void linux_os (ksystem_info *s) {
	struct utsname u;
	uname(&u);

	string_copy(s->os_name, "Linux");
	string_copy(s->kernel_version, u.release);

	FILE *f = fopen("/etc/os-release", "r");
	if (!f) {
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strings_nequal(line, "PRETTY_NAME=", 12)) {
			sscanf(line, "PRETTY_NAME=\"%[^\"]\"", s->distro);
		}
	}
	fclose(f);
}
static int file_read_string (const char *path, char *out, size_t out_size) {
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	if (!fgets(out, (int)out_size, f)) {
		fclose(f);
		return 0;
	}
	// trim newline
	out[strcspn(out, "\n")] = 0;
	fclose(f);
	return 1;
}

static int file_read_int (const char *path, int *out) {
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	int v = 0;
	int r = fscanf(f, "%d", &v);
	fclose(f);
	if (r != 1)
		return 0;
	*out = v;
	return 1;
}

i32 linux_cdrom_status (const char *device) {
	i32 fd = open(device, O_RDONLY | O_NONBLOCK);
	i32 status;

	if (fd < 0)
		return -1; /* Cannot open device */

	status = ioctl(fd, CDROM_DRIVE_STATUS, CDSL_CURRENT);
	close(fd);

	return status;
}

static kdrive_type linux_classify_drive (
	const char *device,		 // /dev/sda1
	const char *mount_point, // /
	const char *fs_type		 // ext4, tmpfs, nfs, ...
) {
	// 1. No mount point
	if (!mount_point || !mount_point[0])
		return KDRIVE_TYPE_NO_ROOT_DIR;

	// 2. RAM disk
	if (strings_equal(fs_type, "tmpfs") || strings_equal(fs_type, "ramfs"))
		return KDRIVE_TYPE_RAMDISK;

	// 3. Network drive
	if (strings_equal(fs_type, "nfs") ||
		strings_equal(fs_type, "nfs4") ||
		strings_equal(fs_type, "cifs") ||
		strings_equal(fs_type, "smbfs") ||
		strings_equal(fs_type, "sshfs") ||
		strings_equal(fs_type, "fuse.sshfs") ||
		strings_equal(fs_type, "davfs"))
		return KDRIVE_TYPE_REMOTE;

	// Only real block devices below this point
	if (strings_nequal(device, "/dev/", 5)) {
		if (strings_equal(fs_type, "nfs4") ||
			strings_equal(fs_type, "ext4") ||
			strings_equal(fs_type, "btrfs")) {
			return KDRIVE_TYPE_FIXED;
		} else if (strings_equal(device, "/dev/sr0")) {
			return KDRIVE_TYPE_CDROM;
		}
		return KDRIVE_TYPE_UNKNOWN;
	}

	// Extract parent disk: sda1 → sda, nvme0n1p2 → nvme0n1
	char disk[128];
	ksnprintf(disk, sizeof(disk), "%s", device + 5);

	size_t len = string_length(disk);
	while (len && kisdigit(disk[len - 1]))
		disk[--len] = 0;
	if (len && disk[len - 1] == 'p')
		disk[--len] = 0;

	// 4. Optical drive
	char path[256], media[64];
	ksnprintf(path, sizeof(path), "/sys/block/%s/device/media", disk);
	if (file_read_string(path, media, sizeof(media))) {
		if (strings_equal(media, "cdrom"))
			return KDRIVE_TYPE_CDROM;
	}

	// 5. Removable
	int removable = 0;
	ksnprintf(path, sizeof(path), "/sys/block/%s/removable", disk);
	if (file_read_int(path, &removable) && removable == 1)
		return KDRIVE_TYPE_REMOVABLE;

	// 6. Fixed disk
	ksnprintf(path, sizeof(path), "/sys/block/%s", disk);
	if (access(path, F_OK) == 0)
		return KDRIVE_TYPE_FIXED;

	return KDRIVE_TYPE_UNKNOWN;
}

static void linux_query_storage (ksystem_info *s) {
	s->storage_count = 0;

	FILE *f = fopen("/proc/self/mounts", "r");
	if (f) {

		char line[1024];

		b8 devsr0_found = false;
		while (fgets(line, sizeof(line), f) && s->storage_count < KMAX_STORAGE_DEVICES) {
			char device[256];
			char mount[256];
			char fs[64];

			// format: device mount fs options dump pass
			if (sscanf(line, "%255s %255s %63s", device, mount, fs) != 3)
				continue;

			// Only real block devices
			if (!strings_nequal(device, "/dev/", 5)) {
				continue;
			}

			// Skip pseudo FS
			if (strings_equal(fs, "tmpfs") ||
				strings_equal(fs, "proc") ||
				strings_equal(fs, "sysfs") ||
				strings_equal(fs, "devtmpfs"))
				continue;

			struct statvfs vfs;
			if (statvfs(mount, &vfs) != 0)
				continue;

			kstorage_info *m = &s->storage[s->storage_count++];
			string_ncopy(m->name, device, sizeof(m->name));
			string_ncopy(m->mount_point, mount, sizeof(m->mount_point));
			m->total_bytes = (u64)vfs.f_blocks * vfs.f_frsize;
			m->free_bytes = (u64)vfs.f_bavail * vfs.f_frsize;
			m->type = linux_classify_drive(device, mount, fs);

			if (strings_equal(device, "/dev/sr0")) {
				devsr0_found = true;
			}
		}

		// Check cdrom status if not mounted.
		if (!devsr0_found) {
			i32 status = linux_cdrom_status("/dev/sr0");
			const char *cdrom_status_text = "";
			if (status == -1) {
				/* Device unavailable, unsupported, or permission error */
			} else if (status == CDS_DISC_OK) {
				/* Drive is ready with a disc inserted */
				cdrom_status_text = "CD-ROM (Disc Inserted)";
			} else if (status == CDS_NO_DISC) {
				/* Drive exists, but no disc is inserted */
				cdrom_status_text = "CD-ROM (No Disc)";
			} else if (status == CDS_TRAY_OPEN) {
				/* Tray is open */
				cdrom_status_text = "CD-ROM (Tray Open)";
			} else if (status == CDS_DRIVE_NOT_READY) {
				/* Drive is busy or not ready */
				cdrom_status_text = "CD-ROM (Not ready)";
			} else if (status == CDS_NO_INFO) {
				/* Status is unknown */
				cdrom_status_text = "CD-ROM (Unknown status)";
			}
			if (status >= 0) {
				kstorage_info *m = &s->storage[s->storage_count++];
				string_ncopy(m->name, "/dev/sr0", sizeof(m->name));
				string_ncopy(m->mount_point, cdrom_status_text, string_length(cdrom_status_text));
				m->total_bytes = 0;
				m->free_bytes = 0;
				m->type = KDRIVE_TYPE_CDROM;
			}
		}

		fclose(f);
	}
}
static u32 linux_get_ram_speed_mhz (void) {
	DIR *dir = opendir("/sys/firmware/dmi/entries");
	if (!dir)
		return 0;

	struct dirent *ent;
	u32 max_speed = 0;

	while ((ent = readdir(dir)) != NULL) {
		// Memory Device (Type 17)
		if (strings_nequal(ent->d_name, "17-", 3))
			continue;

		char path[512];
		ksnprintf(path, sizeof(path),
				  "/sys/firmware/dmi/entries/%s/raw",
				  ent->d_name);

		FILE *f = fopen(path, "rb");
		if (!f)
			continue;

		uint8_t raw[256];
		size_t len = fread(raw, 1, sizeof(raw), f);
		fclose(f);

		if (len < 0x15)
			continue;

		// SMBIOS spec:
		// Offset 0x15 = Configured Memory Speed (MHz), uint16
		u16 speed = raw[0x15] | (raw[0x16] << 8);
		if (speed > max_speed)
			max_speed = speed;
	}

	closedir(dir);
	return max_speed;
}

b8 platform_system_info_collect (ksystem_info *out_info) {
	kzero_memory(out_info, sizeof(*out_info));

	linux_cpu(out_info);
	linux_ram(out_info);
	linux_os(out_info);
	linux_query_storage(out_info);
	out_info->ram_speed_mhz = linux_get_ram_speed_mhz();

#	if defined(__x86_64__)
	string_copy(out_info->cpu_arch, "x86_64");
#	elif defined(__aarch64__)
	string_copy(out_info->cpu_arch, "arm_64");
	detect_arm_features(&out_info->features);
#	endif

	FLAG_SET(out_info->flags, KSYSTEM_INFO_FLAGS_IS_64_BIT_BIT, true);
	return true;
}

void platform_request_clipboard_content (kwindow *window) {
	if (!state_ptr->clipboard.initialized) {
		KWARN("Clipboard not yet initialized, unable to begin new request.");
		return;
	}

	internal_clipboard_state *cb = &state_ptr->clipboard;

	if (cb->paste_pending) {
		KWARN("Clipboard currently processing, unable to begin new request.");
		return;
	}

	// TODO: may need to expand this for other types of data than strings.
	cb->request_targets[0] = state_ptr->clipboard.utf8;
	cb->request_targets[1] = state_ptr->clipboard.text_plain_utf8;
	cb->request_targets[2] = state_ptr->clipboard.text_plain;
	cb->request_targets[3] = state_ptr->clipboard.string;
	cb->request_count = 4;
	cb->request_index = 0;
	cb->paste_pending = true;

	cb->requesting_window = window->platform_state->window;

	xcb_convert_selection(
		state_ptr->handle.connection,
		window->platform_state->window,
		cb->clipboard,
		cb->request_targets[0],
		cb->property,
		XCB_CURRENT_TIME);

	xcb_flush(state_ptr->handle.connection);
}

void platform_clipboard_content_set (kwindow *window, kclipboard_content_type type, u32 size, void *content) {
	internal_clipboard_state *cb = &state_ptr->clipboard;
	if (cb->owned_data) {
		if (cb->owned_type == KCLIPBOARD_CONTENT_TYPE_STRING) {
			string_free(cb->owned_data);
		} else {
			kfree(cb->owned_data);
		}
		cb->owned_data = KNULL;
		cb->owned_size = 0;
	}

	cb->owned_type = type;
	if (type == KCLIPBOARD_CONTENT_TYPE_STRING) {
		cb->owned_size = string_length(content) + 1;
		cb->owned_data = string_duplicate(content);
	} else {
		cb->owned_size = size;
		cb->owned_data = kallocate(size, MEMORY_TAG_BINARY_DATA);
	}
	kcopy_memory(cb->owned_data, content, size);

	// Take ownership of the clipboard.
	xcb_set_selection_owner(
		state_ptr->handle.connection,
		window->platform_state->window,
		cb->clipboard,
		XCB_CURRENT_TIME);

	cb->clipboard_owned = true;

	xcb_flush(state_ptr->handle.connection);
}

static ofd_file_filter *parse_open_file_filters (const char *filter_str, u8 *out_count) {
	if (!filter_str || !out_count) {
		return KNULL;
	}

	char **parts = darray_create(char *);
	u32 parts_count = string_split(filter_str, '|', &parts, true, false, false);
	if (parts_count < 2) {
		KERROR("Invalid open file filter string '%s'. Must have at least 2 parts.");
		*out_count = 0;
		return KNULL;
	}

	u32 type_count = parts_count / 2;
	ofd_file_filter *types = KALLOC_TYPE_CARRAY(ofd_file_filter, type_count);
	for (u32 i = 0; i < type_count; ++i) {
		char **extensions = darray_create(char *);
		u32 extensions_count = string_split(parts[(i * 2) + 1], ';', &extensions, true, false, false);
		ofd_file_filter type_data = {
			.name = string_duplicate(parts[(i * 2)]),
			.pattern_count = (u8)extensions_count,
		};
		type_data.patterns = KALLOC_TYPE_CARRAY(const char *, extensions_count);
		for (u8 j = 0; j < extensions_count; ++j) {
			type_data.patterns[j] = string_duplicate(extensions[j]);
		}
		string_cleanup_split_darray(extensions);

		types[i] = type_data;
	}

	string_cleanup_split_darray(parts);

	*out_count = type_count;
	return types;
}

platform_open_file_dialog_result platform_open_file_dialog_open (platform_open_file_dialog_options options) {
	DBusConnection *connection;
	DBusMessage *call = KNULL;
	DBusMessage *reply = KNULL;
	DBusMessage *signal = KNULL;
	DBusMessageIter args, opts;
	DBusError error;
	const char *request_path;
	const char *empty_parent = "";
	char match_rule[512];
	ofd_file_filter *types;

	types = KNULL;

	platform_open_file_dialog_result result = {
		.file_count = 0,
		.file_paths = KNULL,
		.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE,
	};

	state_ptr->dbus_error_init(&error);
	connection = state_ptr->dbus_bus_get(DBUS_BUS_SESSION, &error);
	if (!connection) {
		KERROR("Failed to connect to bus. Open file dialog cannot open. Error='%s'.", error.message ? error.message : "unknown error");
		state_ptr->dbus_error_free(&error);
		return result;
	}

	state_ptr->dbus_bus_add_match(connection, "type='signal',interface='org.freedesktop.portal.Request',member='Response'", &error);
	if (state_ptr->dbus_error_is_set(&error)) {
		KERROR("DBus match error: '%s'.", error.message ? error.message : "unknown error");
		state_ptr->dbus_error_free(&error);
		return result;
	}

	call = state_ptr->dbus_message_new_method_call(OFD_DBUS_PORTAL_NAME, OFD_DBUS_PORTAL_PATH, OFD_DBUS_PORTAL_INTERFACE, "OpenFile");
	if (!call) {
		goto ofd_open_cleanup;
	}

	state_ptr->dbus_message_iter_init_append(call, &args);

	// Parent window identifier.
	if (!state_ptr->dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &empty_parent)) {
		goto ofd_open_cleanup;
	}

	// Dialog title.
	const char *title = (options.title) ? options.title : "Open File";
	if (!state_ptr->dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &title)) {
		goto ofd_open_cleanup;
	}

	// Options dict.
	if (!state_ptr->dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &opts)) {
		goto ofd_open_cleanup;
	}

	if (options.starting_dir) {
		u32 len = string_length(options.starting_dir);
		if (len > U32_MAX) {
			goto ofd_open_cleanup;
		}

		if (!kdbus_append_byte_array_variant(&opts, "current_folder", (const u8 *)options.starting_dir, (i32)len)) {
			goto ofd_open_cleanup;
		}
	}

	// multiselect
	if (options.allow_multiselect) {
		// NOTE: only add if the value is true.
		if (!kdbus_append_bool_variant(&opts, "multiple", true)) {
			goto ofd_open_cleanup;
		}
	}

	// Filters, if included. If not, a default of *.*/all files is assumed.
	if (options.filter) {
		u8 type_count = 0;
		types = parse_open_file_filters(options.filter, &type_count);
		if (type_count > 0) {
			if (!kdbus_append_filters_variant(&opts, types, type_count)) {
				goto ofd_open_cleanup;
			}
		}
	}

	if (!state_ptr->dbus_message_iter_close_container(&args, &opts)) {
		goto ofd_open_cleanup;
	}

	reply = state_ptr->dbus_connection_send_with_reply_and_block(connection, call, DBUS_TIMEOUT_USE_DEFAULT, &error);
	if (!reply) {
		KERROR("OpenFile error: '%s'.", error.message ? error.message : "unknown error");
		state_ptr->dbus_error_free(&error);
		goto ofd_open_cleanup;
	}

	if (!state_ptr->dbus_message_get_args(reply, &error, DBUS_TYPE_OBJECT_PATH, &request_path, DBUS_TYPE_INVALID)) {
		KERROR("OpenFile reply error: '%s'.", error.message ? error.message : "invalid reply");
		state_ptr->dbus_error_free(&error);
		goto ofd_open_cleanup;
	}

	string_nformat(match_rule, sizeof(match_rule), "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='%s'", request_path);

	for (;;) {
		DBusMessage *msg;
		const char *path;

		while ((msg = state_ptr->dbus_connection_pop_message(connection)) != KNULL) {
			if (state_ptr->dbus_message_is_signal(msg, OFD_DBUS_REQUEST_INTERFACE, "Response")) {
				path = state_ptr->dbus_message_get_path(msg);

				if (path && strings_equal(path, request_path)) {
					dbus_uint32_t response = 1;
					if (state_ptr->dbus_message_get_args(msg, &error, DBUS_TYPE_UINT32, &response, DBUS_TYPE_INVALID)) {
						if (response == 0) {
							i32 extracted_count = 0;
							char **paths = extract_selected_path(msg, &extracted_count);
							result.result = (extracted_count >= 1) ? PLATFORM_OFD_OP_RESULT_SUCCESS : (extracted_count == 0) ? PLATFORM_OFD_OP_RESULT_USER_CANCELLED
																															 : PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE;
							// Return a standard C array, not a darray.
							result.file_count = darray_length(paths);
							result.file_paths = KALLOC_TYPE_CARRAY(const char *, result.file_count);
							for (u8 i = 0; i < result.file_count; ++i) {
								result.file_paths[i] = paths[i];
								KTRACE("path[%u]: '%s", i, paths[i]);
							}
							darray_destroy(paths);
						} else if (response == 1) {
							result.result = PLATFORM_OFD_OP_RESULT_USER_CANCELLED;
						} else {
							result.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE; // Portal reported failure.
						}
					} else {
						state_ptr->dbus_error_free(&error);
						result.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE;
					}

					state_ptr->dbus_message_unref(msg);
					goto ofd_open_cleanup;
				}
			}

			state_ptr->dbus_message_unref(msg);
		}

		if (!state_ptr->dbus_connection_read_write(connection, -1)) {
			goto ofd_open_cleanup;
		}
	}

ofd_open_cleanup:
	if (signal) {
		state_ptr->dbus_message_unref(signal);
	}
	if (reply) {
		state_ptr->dbus_message_unref(reply);
	}
	if (call) {
		state_ptr->dbus_message_unref(call);
	}

	return result;
}

// NOTE: gamepad functions
void platform_gamepads_initialize (void) {
	platform_state *state = state_ptr;
	state->gamepad_state->devices = darray_reserve(platform_gamepad_device *, 4);
	state->gamepad_state->event_queue = darray_reserve(platform_gamepad_event, 64);
#	if 1
	pthread_mutexattr_t recursive_lock;
	pthread_mutexattr_init(&recursive_lock);
	pthread_mutexattr_settype(&recursive_lock, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&state->gamepad_state->devices_mutex, &recursive_lock);
	pthread_mutex_init(&state->gamepad_state->event_queue_mutex, &recursive_lock);
	pthread_mutexattr_destroy(&recursive_lock);
#	else
	pthread_mutex_init(&state->gamepad_state->devices_mutex, 0);
	pthread_mutex_init(&state->gamepad_state->event_queue_mutex, 0);
#	endif

	// NOTE: using CLOCK_MONOTONIC here because platform_get_absolute_time uses CLOCK_MONOTONIC_RAW which has a
	// different epoch.
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	state->gamepad_state->start_time = (f64)now.tv_sec + (f64)now.tv_nsec * 0.000000001;

	FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT, true);
	KINFO("Initializing gamepads at the platform level...");
	detect_gamepad_devices(state);
}

u32 platform_gamepad_device_count (void) {
	pthread_mutex_lock(&state_ptr->gamepad_state->devices_mutex);
	u32 count = darray_length(state_ptr->gamepad_state->devices);
	pthread_mutex_unlock(&state_ptr->gamepad_state->devices_mutex);
	return count;
}

b8 platform_gamepad_get_device_info (u32 device_id, i32 *out_vendor_id, i32 *out_product_id, u32 *out_button_count, u32 *out_axis_count) {
	if (!FLAG_GET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	pthread_mutex_lock(&state_ptr->gamepad_state->devices_mutex);
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
	pthread_mutex_unlock(&state_ptr->gamepad_state->devices_mutex);

	return true;
}

const char *platform_gamepad_get_device_desc (u32 device_id) {
	if (!FLAG_GET(state_ptr->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return KNULL;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return KNULL;
	}

	return string_duplicate(device->description);
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

	if (device->input_path != KNULL && device->input_name != KNULL) {
		if (!gamepad_try_set_player_index_led(device, player_index)) {
			KWARN("Failed to set LED player index for gamepad '%s'. Numeric index still set though.", device->description);
		}
	}

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

	return FLAG_GET(device->button_states, button);
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

b8 platform_initialize_input (void) {
	platform_state *state = state_ptr;
	if (!state) {
		return false;
	}
	// TODO: initialize keyboard and mouse devices. Gamepads handled separately.
	KINFO("GNU/Linux platform input initialized.");

	return true;
}

// NOTE: syscall implementation
i64 syscall2 (i64 number, i64 arg0, i64 arg1) {
	i64 ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(number),
		  "D"(arg0),
		  "S"(arg1)
		: "rcx", "r11", "memory");

	return ret;
}

i64 syscall3 (i64 number, i64 arg0, i64 arg1, i64 arg2) {
	i64 ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(number),
		  "D"(arg0),
		  "S"(arg1),
		  "d"(arg2)
		: "rcx", "r11", "memory");

	return ret;
}

i64 syscall4 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3) {
	i64 ret;

	register i64 r10 __asm__("r10") = arg3;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(number),
		  "D"(arg0),
		  "S"(arg1),
		  "d"(arg2),
		  "r"(r10)
		: "rcx", "r11", "memory");

	return ret;
}

i64 syscall5 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4) {
	i64 ret;

	register i64 r10 __asm__("r10") = arg3;
	register i64 r8 __asm__("r8") = arg4;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(number),
		  "D"(arg0),
		  "S"(arg1),
		  "d"(arg2),
		  "r"(r10),
		  "r"(r8)
		: "rcx", "r11", "memory");

	return ret;
}

i64 syscall6 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4, i64 arg5) {
	i64 ret;

	register i64 r10 __asm__("r10") = arg3;
	register i64 r8 __asm__("r8") = arg4;
	register i64 r9 __asm__("r9") = arg5;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(number),
		  "D"(arg0),
		  "S"(arg1),
		  "d"(arg2),
		  "r"(r10),
		  "r"(r8),
		  "r"(r9)
		: "rcx", "r11", "memory");

	return ret;
}

static kwindow *window_from_handle (xcb_window_t window) {
	u32 len = darray_length(state_ptr->windows);
	for (u32 i = 0; i < len; ++i) {
		kwindow *w = state_ptr->windows[i];
		if (w && w->platform_state->window == window) {
			return state_ptr->windows[i];
		}
	}
	return 0;
}

static b8 key_is_repeat (platform_state *state, const xcb_key_press_event_t *ev) {
	b8 repeat = (ev->detail == state->last_keycode) && (ev->time == state->last_key_time); // Some servers send identical timestamps for repeats

	state->last_keycode = ev->detail;
	state->last_key_time = ev->time;

	return repeat;
}

// Key translation
static keys translate_keycode (u32 x_keycode) {
	switch (x_keycode) {
	case XK_BackSpace:
		return K_KEY_BACKSPACE;
	case XK_Return:
		return K_KEY_ENTER;
	case XK_Tab:
		return K_KEY_TAB;
		// case XK_Shift: return K_KEY_SHIFT;
		// case XK_Control: return K_KEY_CONTROL;

	case XK_Pause:
		return K_KEY_PAUSE;
	case XK_Caps_Lock:
		return K_KEY_CAPITAL;

	case XK_Escape:
		return K_KEY_ESCAPE;

		// Not supported
		// case : return K_KEY_CONVERT;
		// case : return K_KEY_NONCONVERT;
		// case : return K_KEY_ACCEPT;

	case XK_Mode_switch:
		return K_KEY_MODECHANGE;

	case XK_space:
		return K_KEY_SPACE;
	case XK_Prior:
		return K_KEY_PAGEUP;
	case XK_Next:
		return K_KEY_PAGEDOWN;
	case XK_End:
		return K_KEY_END;
	case XK_Home:
		return K_KEY_HOME;
	case XK_Left:
		return K_KEY_LEFT;
	case XK_Up:
		return K_KEY_UP;
	case XK_Right:
		return K_KEY_RIGHT;
	case XK_Down:
		return K_KEY_DOWN;
	case XK_Select:
		return K_KEY_SELECT;
	case XK_Print:
		return K_KEY_PRINT;
	case XK_Execute:
		return K_KEY_EXECUTE;
	// case XK_snapshot: return K_KEY_SNAPSHOT; // not supported
	case XK_Insert:
		return K_KEY_INSERT;
	case XK_Delete:
		return K_KEY_DELETE;
	case XK_Help:
		return K_KEY_HELP;

	case XK_Meta_L:
	case XK_Super_L:
		// Treat the "meta" key (if mapped) as super
		return K_KEY_LSUPER;
	case XK_Meta_R:
	case XK_Super_R:
		// Treat the "meta" key (if mapped) as super
		return K_KEY_RSUPER;
		// case XK_apps: return K_KEY_APPS; // not supported

		// case XK_sleep: return K_KEY_SLEEP; //not supported

	case XK_KP_0:
		return K_KEY_NUMPAD0;
	case XK_KP_1:
		return K_KEY_NUMPAD1;
	case XK_KP_2:
		return K_KEY_NUMPAD2;
	case XK_KP_3:
		return K_KEY_NUMPAD3;
	case XK_KP_4:
		return K_KEY_NUMPAD4;
	case XK_KP_5:
		return K_KEY_NUMPAD5;
	case XK_KP_6:
		return K_KEY_NUMPAD6;
	case XK_KP_7:
		return K_KEY_NUMPAD7;
	case XK_KP_8:
		return K_KEY_NUMPAD8;
	case XK_KP_9:
		return K_KEY_NUMPAD9;
	case XK_multiply:
		return K_KEY_MULTIPLY;
	case XK_KP_Add:
		return K_KEY_ADD;
	case XK_KP_Separator:
		return K_KEY_SEPARATOR;
	case XK_KP_Subtract:
		return K_KEY_SUBTRACT;
	case XK_KP_Decimal:
		return K_KEY_DECIMAL;
	case XK_KP_Divide:
		return K_KEY_DIVIDE;
	case XK_F1:
		return K_KEY_F1;
	case XK_F2:
		return K_KEY_F2;
	case XK_F3:
		return K_KEY_F3;
	case XK_F4:
		return K_KEY_F4;
	case XK_F5:
		return K_KEY_F5;
	case XK_F6:
		return K_KEY_F6;
	case XK_F7:
		return K_KEY_F7;
	case XK_F8:
		return K_KEY_F8;
	case XK_F9:
		return K_KEY_F9;
	case XK_F10:
		return K_KEY_F10;
	case XK_F11:
		return K_KEY_F11;
	case XK_F12:
		return K_KEY_F12;
	case XK_F13:
		return K_KEY_F13;
	case XK_F14:
		return K_KEY_F14;
	case XK_F15:
		return K_KEY_F15;
	case XK_F16:
		return K_KEY_F16;
	case XK_F17:
		return K_KEY_F17;
	case XK_F18:
		return K_KEY_F18;
	case XK_F19:
		return K_KEY_F19;
	case XK_F20:
		return K_KEY_F20;
	case XK_F21:
		return K_KEY_F21;
	case XK_F22:
		return K_KEY_F22;
	case XK_F23:
		return K_KEY_F23;
	case XK_F24:
		return K_KEY_F24;

	case XK_Num_Lock:
		return K_KEY_NUMLOCK;
	case XK_Scroll_Lock:
		return K_KEY_SCROLL;

	case XK_KP_Equal:
		return K_KEY_NUMPAD_EQUAL;

	case XK_Shift_L:
		return K_KEY_LSHIFT;
	case XK_Shift_R:
		return K_KEY_RSHIFT;
	case XK_Control_L:
		return K_KEY_LCONTROL;
	case XK_Control_R:
		return K_KEY_RCONTROL;
	case XK_Alt_L:
		return K_KEY_LALT;
	case XK_Alt_R:
		return K_KEY_RALT;

	case XK_semicolon:
		return K_KEY_SEMICOLON;
	case XK_equal:
		return K_KEY_EQUAL;
	case XK_comma:
		return K_KEY_COMMA;
	case XK_minus:
		return K_KEY_MINUS;
	case XK_period:
		return K_KEY_PERIOD;
	case XK_slash:
		return K_KEY_SLASH;
	case XK_grave:
		return K_KEY_GRAVE;
	case XK_bracketleft:
		return K_KEY_LBRACKET;
	case XK_bracketright:
		return K_KEY_RBRACKET;
	case XK_quotedbl:
	case XK_quoteright:
		// case XK_quoteleft: // NOTE: for some reason this code is the same as XK_grave???
		// NOTE: Both of these are required since either can technically show up for this keypress,
		return K_KEY_QUOTE;
	case XK_backslash:
		return K_KEY_BACKSLASH;

	case XK_0:
		return K_KEY_0;
	case XK_1:
		return K_KEY_1;
	case XK_2:
		return K_KEY_2;
	case XK_3:
		return K_KEY_3;
	case XK_4:
		return K_KEY_4;
	case XK_5:
		return K_KEY_5;
	case XK_6:
		return K_KEY_6;
	case XK_7:
		return K_KEY_7;
	case XK_8:
		return K_KEY_8;
	case XK_9:
		return K_KEY_9;

	case XK_a:
	case XK_A:
		return K_KEY_A;
	case XK_b:
	case XK_B:
		return K_KEY_B;
	case XK_c:
	case XK_C:
		return K_KEY_C;
	case XK_d:
	case XK_D:
		return K_KEY_D;
	case XK_e:
	case XK_E:
		return K_KEY_E;
	case XK_f:
	case XK_F:
		return K_KEY_F;
	case XK_g:
	case XK_G:
		return K_KEY_G;
	case XK_h:
	case XK_H:
		return K_KEY_H;
	case XK_i:
	case XK_I:
		return K_KEY_I;
	case XK_j:
	case XK_J:
		return K_KEY_J;
	case XK_k:
	case XK_K:
		return K_KEY_K;
	case XK_l:
	case XK_L:
		return K_KEY_L;
	case XK_m:
	case XK_M:
		return K_KEY_M;
	case XK_n:
	case XK_N:
		return K_KEY_N;
	case XK_o:
	case XK_O:
		return K_KEY_O;
	case XK_p:
	case XK_P:
		return K_KEY_P;
	case XK_q:
	case XK_Q:
		return K_KEY_Q;
	case XK_r:
	case XK_R:
		return K_KEY_R;
	case XK_s:
	case XK_S:
		return K_KEY_S;
	case XK_t:
	case XK_T:
		return K_KEY_T;
	case XK_u:
	case XK_U:
		return K_KEY_U;
	case XK_v:
	case XK_V:
		return K_KEY_V;
	case XK_w:
	case XK_W:
		return K_KEY_W;
	case XK_x:
	case XK_X:
		return K_KEY_X;
	case XK_y:
	case XK_Y:
		return K_KEY_Y;
	case XK_z:
	case XK_Z:
		return K_KEY_Z;

	default:
		return 0;
	}
}

// Appends a dictionary entry containing a bool variant.
static i32 kdbus_append_bool_variant (DBusMessageIter *dict, const char *key, b8 value) {
	DBusMessageIter entry, variant;

	if (!state_ptr->dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, KNULL, &entry)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_append_basic(dict, DBUS_TYPE_STRING, &key)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_append_basic(dict, DBUS_TYPE_BOOLEAN, &value)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_close_container(&entry, &variant)) {
		return 0;
	}

	return state_ptr->dbus_message_iter_close_container(dict, &entry);
}

// Appends a dictionary entry containing a string variant.
static i32 kdbus_append_string_variant (DBusMessageIter *dict, const char *key, const char *value) {
	DBusMessageIter entry, variant;

	if (!state_ptr->dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, KNULL, &entry)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_append_basic(dict, DBUS_TYPE_STRING, &key)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_append_basic(dict, DBUS_TYPE_STRING, &value)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_close_container(&entry, &variant)) {
		return 0;
	}

	return state_ptr->dbus_message_iter_close_container(dict, &entry);
}

// Appends a dictionary entry containing a byte-array variant.
static i32 kdbus_append_byte_array_variant (DBusMessageIter *dict, const char *key, const u8 *bytes, i32 length) {
	DBusMessageIter entry, variant, array;

	if (!state_ptr->dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, KNULL, &entry)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_append_basic(dict, DBUS_TYPE_STRING, &key)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "ay", &variant)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "y", &array)) {
		return 0;
	}

	if (length > 0 && !state_ptr->dbus_message_iter_append_fixed_array(&array, DBUS_TYPE_BYTE, &bytes, length)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_close_container(&variant, &array)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_close_container(&entry, &variant)) {
		return 0;
	}

	return state_ptr->dbus_message_iter_close_container(dict, &entry);
}

static i32 kdbus_append_filters_variant (DBusMessageIter *dict, const ofd_file_filter *filters, u64 filter_count) {
	DBusMessageIter entry, variant, array;
	u64 i, j;

	if (!state_ptr->dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, KNULL, &entry)) {
		return 0;
	}

	const char *key = "filters";
	if (!state_ptr->dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "a(sa(us))", &variant)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "(sa(us))", &array)) {
		return 0;
	}

	for (i = 0; i < filter_count; ++i) {
		DBusMessageIter structure, patterns;

		if (!state_ptr->dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, KNULL, &structure)) {
			return 0;
		}

		if (!state_ptr->dbus_message_iter_append_basic(&structure, DBUS_TYPE_STRING, &filters[i].name)) {
			return 0;
		}

		if (!state_ptr->dbus_message_iter_open_container(&structure, DBUS_TYPE_ARRAY, "(us)", &patterns)) {
			return 0;
		}

		for (j = 0; j < filters[i].pattern_count; ++j) {
			DBusMessageIter pattern;
			dbus_uint32_t type = 0;
			const char *text = filters[i].patterns[j];

			if (!state_ptr->dbus_message_iter_open_container(&patterns, DBUS_TYPE_STRUCT, KNULL, &pattern)) {
				return 0;
			}

			if (!state_ptr->dbus_message_iter_append_basic(&pattern, DBUS_TYPE_UINT32, &type)) {
				return 0;
			}

			if (!state_ptr->dbus_message_iter_append_basic(&pattern, DBUS_TYPE_STRING, &text)) {
				return 0;
			}

			if (!state_ptr->dbus_message_iter_close_container(&patterns, &pattern)) {
				return 0;
			}
		}

		if (!state_ptr->dbus_message_iter_close_container(&structure, &patterns)) {
			return 0;
		}

		if (!state_ptr->dbus_message_iter_close_container(&array, &structure)) {
			return 0;
		}
	}

	if (!state_ptr->dbus_message_iter_close_container(&variant, &array)) {
		return 0;
	}

	if (!state_ptr->dbus_message_iter_close_container(&entry, &variant)) {
		return 0;
	}

	return state_ptr->dbus_message_iter_close_container(dict, &entry);
}

// out_result: >0 = number of paths obtained, 0 = user cancelled, -1 = error
static char **extract_selected_path (DBusMessage *message, i32 *out_result) {
	DBusMessageIter iter, dict;
	char **out_paths;

	out_paths = KNULL;
	*out_result = -2;

	if (!state_ptr->dbus_message_iter_init(message, &iter)) {
		*out_result = -1;
		goto extract_selected_path_cleanup;
	}

	// Response arguments: u32 response, a{sv} results
	if (state_ptr->dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) {
		*out_result = -1;
		goto extract_selected_path_cleanup;
	}

	state_ptr->dbus_message_iter_next(&iter);

	if (state_ptr->dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
		*out_result = -1;
		goto extract_selected_path_cleanup;
	}

	state_ptr->dbus_message_iter_recurse(&iter, &dict);

	while (state_ptr->dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
		DBusMessageIter entry, value;

		state_ptr->dbus_message_iter_recurse(&dict, &entry);

		if (state_ptr->dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
			*out_result = -1;
			goto extract_selected_path_cleanup;
		}

		const char *key;
		state_ptr->dbus_message_iter_get_basic(&entry, &key);

		if (strings_equal(key, "uris")) {
			state_ptr->dbus_message_iter_next(&entry);

			if (state_ptr->dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
				*out_result = -1;
				goto extract_selected_path_cleanup;
			}

			state_ptr->dbus_message_iter_recurse(&entry, &value);

			if (state_ptr->dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_ARRAY) {
				*out_result = -1;
				goto extract_selected_path_cleanup;
			}

			state_ptr->dbus_message_iter_recurse(&value, &entry);

			if (state_ptr->dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
				*out_result = -1;
				goto extract_selected_path_cleanup;
			}

			const char *uri;
			state_ptr->dbus_message_iter_get_basic(&entry, &uri);

			// Add to the list of strings.
			const char *val = string_from_file_uri(uri);
			if (val) {
				if (!out_paths) {
					out_paths = darray_create(char *);
				}

				darray_push(out_paths, &val);
			}
		}

		state_ptr->dbus_message_iter_next(&dict);
	}

	*out_result = darray_length(out_paths);

extract_selected_path_cleanup:
	if (*out_result < 1) {
		if (out_paths) {
			u32 len = darray_length(out_paths);
			for (u32 i = 0; i < len; ++i) {
				kfree(out_paths[i]);
			}
			darray_destroy(out_paths);
			out_paths = KNULL;
		}
	}

	return out_paths;
}

static b8 path_is_ancestor (const char *parent, const char *child) {
	u32 len = string_length(parent);

	return strings_nequal(parent, child, len) && (child[len] == '\0' || child[len] == '/');
}

static b8 write_sysfs (const char *path, u32 value) {
	i32 fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0) {
		KDEBUG("Failed to open LED: %s\n", path);
		if (errno == EACCES || errno == EPERM) {
			KDEBUG("Insufficient access to brightness controls.");
		} else {
			KDEBUG("errno: %d (%s)\n", errno, strerror(errno));
		}
		return false;
	}

	char buffer[16];
	i64 len = snprintf(buffer, sizeof(buffer), "%u\n", value);
	i64 written = write(fd, buffer, (u64)len);
	close(fd);

	return written == len;
}

#	define SYSFS_INPUT "/sys/class/input"
#	define SYSFS_LEDS "/sys/class/leds"

// Resolves an evdev path to it's corresponding input device.
//
// ex: /dev/input/event5 -> /sys/devices/.../input/input12
//
// Also returns the input device name (ex: input12)
static b8 get_input_device_path (const char *evdev_path, char **out_path, char **out_name) {
	char event_name[NAME_MAX];
	char path[PATH_MAX];
	char *name;
	char *slash;

	name = strrchr(evdev_path, '/');
	name = name ? name + 1 : (char *)evdev_path;

	if (!strings_nequal(name, "event", 5)) {
		return false;
	}

	if (ksnprintf(path, sizeof(path), SYSFS_INPUT "/%s/device", name) >= (i32)sizeof(path)) {
		return false;
	}

	*out_path = realpath(path, *out_path);
	if (!out_path) {
		return false;
	}

	slash = strrchr(*out_path, '/');
	if (!slash) {
		return false;
	}

	name = slash + 1;

	*out_name = string_duplicate(name);

	(void)event_name;

	return true;
}

static b8 sixaxis_get_led_device_path (const char *evdev_path, char **out_path, u32 index) {
	char path[PATH_MAX];
	char *name;

	name = strrchr(evdev_path, '/');
	name = name ? name + 1 : (char *)evdev_path;

	if (ksnprintf(path, sizeof(path), SYSFS_INPUT "/%s/device", name) >= (i32)sizeof(path)) {
		return false;
	}

	char *chopped_name = realpath(path, KNULL);
	if (!out_path) {
		return false;
	}

	// Get the slash just before the device name.
	i32 last_slash = string_last_index_of(chopped_name, '/');
	string_mid(chopped_name, chopped_name, last_slash + 1, -1);
	*out_path = string_format("/sys/class/leds/%s::sony%u/brightness", chopped_name, index);

	return true;
}

// Returns an xpad LED brightness sfs path belonging to the given input device.
// Returns KNULL on error.
static const char *find_xpad_led (const char *input_path) {
	DIR *dir = opendir(SYSFS_LEDS);
	if (!dir) {
		return KNULL;
	}

	struct dirent *entry;
	char device_path[PATH_MAX];
	char resolved_path[PATH_MAX];
	while ((entry = readdir(dir)) != KNULL) {
		if (!strings_nequal(entry->d_name, "xpad", 4)) {
			continue;
		}

		if (ksnprintf(device_path, sizeof(device_path), SYSFS_LEDS "/%s/device", entry->d_name) >= (i32)sizeof(device_path)) {
			continue;
		}

		if (!realpath(device_path, resolved_path)) {
			continue;
		}

		const char *out_path = string_format(SYSFS_LEDS "/%s/brightness", entry->d_name);

		closedir(dir);

		return out_path;
	}

	closedir(dir);
	return KNULL;
}

// xpad devices
/**
 * xpad LED brightness commands:
 * 0 = off
 * 6 = player 1
 * 7 = player 2
 * 8 = player 3
 * 9 = player 4
 */
static b8 xpad_set_player_index (const char *input_path, i32 index) {
	const char *led_path = find_xpad_led(input_path);
	if (led_path == KNULL) {
		return false;
	}

	u32 command = index < 0 ? 0 : (u32)(6 + index);

	b8 result = write_sysfs(led_path, command);
	string_free(led_path);
	return result;
}

/**
 * DualShock 4 (PS4) has an RGB lightbar, not separate LEDs.
 *
 * inputN:red
 * inputN:green
 * inputN:blue
 * inputN:global (enables/disables the light bar)
 */
static b8 dualshock4_set_player_index (const char *input_name, i32 index) {
	static const u8 colours[4][3] = {
		{0x00, 0x00, 0x40}, // Player 1 = blue
		{0x40, 0x00, 0x00}, // Player 2 = red
		{0x00, 0x40, 0x00}, // Player 3 = green
		{0x20, 0x00, 0x20}	// Player 4 = pink
	};
	static const char *channels[4] = {
		"red", "green", "blue", "global"};

	u32 values[4];
	if (index < 0) {
		kzero_memory(values, sizeof(values));
	} else {
		for (u8 i = 0; i < 3; ++i) {
			values[i] = colours[index][i];
		}
		values[3] = 1;
	}

	char path[PATH_MAX];
	for (u8 i = 0; i < 4; ++i) {
		if (ksnprintf(path, sizeof(path), SYSFS_LEDS "/%s:%s/brightness", input_name, channels[i]) >= (i32)sizeof(path)) {
			return false;
		}

		if (!write_sysfs(path, values[i])) {
			return false;
		}
	}

	return true;
}

/**
 * Dualsense (PS5) has *five* individial LEDs:
 *
 * Player 1: --x--
 * Player 2: -x-x-
 * Player 3: x-x-x
 * Player 4: xx-xx
 *
 * inputN:white:player1
 * inputN:white:player2
 * inputN:white:player3
 * inputN:white:player4
 * inputN:white:player5
 */
static b8 dualsense_set_player_index (const char *input_name, i32 index) {
	static const u8 patterns[4] = {
		0x04, // --x--
		0x0A, // -x-x-
		0x15, // x-x-x
		0x1B  // xx-xx
	};

	u32 pattern = index < 0 ? 0 : patterns[index];

	char led_name[PATH_MAX];
	for (u8 i = 0; i < 5; ++i) {
		ksnprintf(led_name, sizeof(led_name), SYSFS_LEDS "/%s:white:player-%u/brightness", input_name, i + 1);

		if (!write_sysfs(led_name, (pattern & (1u << i)) ? 1 : 0)) {
			return false;
		}
	}

	return true;
}

static b8 sixaxis_set_player_index (const char *input_path, i32 index) {

	char *led_path = KNULL;
	for (u8 i = 0; i < 4; ++i) {
		sixaxis_get_led_device_path(input_path, &led_path, i + 1);

		if (!write_sysfs(led_path, i == index ? 1 : 0)) {
			return false;
		}
	}
	return true;
}

static b8 gamepad_try_set_player_index_led (platform_gamepad_device *device, i32 index) {

	b8 result = false;
	switch (device->vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		result = xpad_set_player_index(device->input_path, index);
		break;
	case USB_VENDOR_ID_SONY:
		switch (device->product_id) {
		case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
			break;
		case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
			result = sixaxis_set_player_index(device->input_path, index);
			break;
		default:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
			result = dualshock4_set_player_index(device->input_name, index);
			break;
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
			result = dualsense_set_player_index(device->input_name, index);
			break;
		}
	case USB_VENDOR_ID_NINTENDO:
	default:
		break;
	}

	if (result) {
		return true;
	}

	// If a bespoke method didn't work, try them all.
	if (xpad_set_player_index(device->input_path, index)) {
		return true;
	}

	if (dualsense_set_player_index(device->input_name, index)) {
		return true;
	}

	if (dualshock4_set_player_index(device->input_name, index)) {
		return true;
	}

	return false;
}

void detect_gamepad_devices (struct platform_state *state) {
	if (!FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT)) {
		return;
	}

	pthread_mutex_lock(&state->gamepad_state->devices_mutex);
	DIR *dev_input = opendir("/dev/input");
	time_t current_time = time(KNULL);
	if (dev_input != KNULL) {
		struct dirent *entity;
		while ((entity = readdir(dev_input)) != KNULL) {
			u32 chars_consumed = 0;
			i32 num;
			char file_name[PATH_MAX];
			struct stat stat_buf;
			if (sscanf(entity->d_name, "event%d%n", &num, &chars_consumed) && chars_consumed == string_length(entity->d_name)) {
				ksnprintf(file_name, PATH_MAX, "/dev/input/%s", entity->d_name);
				if (stat(file_name, &stat_buf) || stat_buf.st_mtime < state->gamepad_state->last_input_stat_time) {
					continue;
				}

				b8 exists = false;
				u32 device_count = darray_length(state->gamepad_state->devices);
				for (u32 i = 0; i < device_count; ++i) {
					if (strings_equal(state->gamepad_state->devices[i]->path, file_name)) {
						exists = true;
						break;
					}
				}
				if (exists) {
					continue;
				}

				i32 fd = open(file_name, O_RDONLY, 0);
				i32 ev_cap_bits[(EV_CNT - 1) / sizeof(i32) * 8 + 1];
				kzero_memory(ev_cap_bits, sizeof(ev_cap_bits));
				i32 ev_key_bits[(KEY_CNT - 1) / sizeof(i32) * 8 + 1];
				kzero_memory(ev_key_bits, sizeof(ev_key_bits));
				i32 ev_abs_bits[(ABS_CNT - 1) / sizeof(i32) * 8 + 1];
				kzero_memory(ev_abs_bits, sizeof(ev_abs_bits));

				// Explicitly use CLOCK_MONOTONIC for timestamps.
				i32 clock_id = CLOCK_MONOTONIC;
				ioctl(fd, EVIOCSCLOCKID, &clock_id);
				u32 evc_res = ioctl(fd, EVIOCGBIT(0, sizeof(ev_cap_bits)), ev_cap_bits);
				u32 evk_res = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(ev_key_bits)), ev_key_bits);
				u32 eva_res = ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ev_abs_bits)), ev_abs_bits);
				if (evc_res < 0 || evk_res < 0 || eva_res < 0) {
					close(fd);
					continue;
				}

				if (!test_bit(EV_KEY, ev_cap_bits) || !test_bit(EV_ABS, ev_cap_bits) ||
					!test_bit(ABS_X, ev_abs_bits) || !test_bit(ABS_Y, ev_abs_bits) ||
					(!test_bit(BTN_TRIGGER, ev_key_bits) && !test_bit(BTN_A, ev_key_bits) && !test_bit(BTN_1, ev_key_bits))) {
					close(fd);
					continue;
				}

				// Add a new device to the collection.
				platform_gamepad_device *n = KALLOC_TYPE(platform_gamepad_device, MEMORY_TAG_PLATFORM);
				u32 dev_id = state->gamepad_state->next_device_id++;
				u32 new_idx = darray_length(state->gamepad_state->devices);
				darray_push(state->gamepad_state->devices, &n);
				platform_gamepad_device *device = state->gamepad_state->devices[new_idx];

				device->device_id = dev_id;
				device->fd = fd;
				device->path = string_duplicate(file_name);
				if (!get_input_device_path(device->path, &device->input_path, &device->input_name)) {
					KTRACE("Unable to get input device path for evdev '%s'. Controller player LEDs won't work.", device->path);
					device->input_name = KNULL;
					device->input_path = KNULL;
				}

				kset_memory(device->button_map, 0xFF, sizeof(device->button_map));
				kset_memory(device->axis_map, 0xFF, sizeof(device->axis_map));

				char name[128];
				if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) > 0) {
					device->description = string_duplicate(name);
				} else {
					device->description = string_duplicate(file_name);
				}

				struct input_id id;
				if (!ioctl(fd, EVIOCGID, &id)) {
					device->vendor_id = id.vendor;
					device->product_id = id.product;
				} else {
					device->vendor_id = device->product_id = 0;
				}

				kzero_memory(ev_key_bits, sizeof(ev_key_bits));
				ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(ev_key_bits)), ev_key_bits);
				kzero_memory(ev_abs_bits, sizeof(ev_abs_bits));
				ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(ev_abs_bits)), ev_abs_bits);

				// Detect axes.
				device->axis_count = 0;
				for (i32 i = 0; i < ABS_CNT; ++i) {
					if (test_bit(i, ev_abs_bits)) {
						if (ioctl(fd, EVIOCGABS(i), &device->axis_info[i]) < 0 ||
							device->axis_info[i].minimum == device->axis_info[i].maximum) {
							continue;
						}
					}

					// Store the event code as the index, and the button as its value.
					gamepad_axis axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count);
					device->axis_map[i] = axis;
					device->axis_count++;
				}

				// Detect buttons.
				device->button_count = 0;
				for (i32 i = BTN_MISC; i < KEY_CNT; ++i) {
					if (test_bit(i, ev_key_bits)) {

						// Store the event code as the index, and the button as the value.
						u32 button_code = i - BTN_MISC;
						gamepad_button button = gamepad_get_button(device->vendor_id, device->product_id, device->button_count);
						device->button_map[button_code] = button;
						device->button_count++;
					}
				}

				kzero_memory(device->axis_states, sizeof(device->axis_states));
				/* kzero_memory(device->button_states, sizeof(device->button_states)); */
				device->button_states = 0;

				/* KTRACE(
					"(Linux Platform) Gamepad detected: device_id=%u, desc='%s', path='%s', vendor_id=%d, product_id=%d, button_count=%u, axis_count=%u",
					device->device_id,
					device->description,
					device->path,
					device->vendor_id,
					device->product_id,
					device->button_count,
					device->axis_count); */

				// Invoke a callback to notify that a gamepad was attached.
				if (state->gamepad_callbacks.on_attached) {
					state->gamepad_callbacks.on_attached(device->device_id);
				}

				pthread_create(&device->thread, KNULL, gamepad_device_thread_do_work, device);
			}
		}

		closedir(dev_input);
	}

	state->gamepad_state->last_input_stat_time = current_time;
	pthread_mutex_unlock(&state->gamepad_state->devices_mutex);
}

void *gamepad_device_thread_do_work (void *context) {
	platform_gamepad_device *device = context;
	struct input_event event;

	// While the device can be read, it is considered attached.
	i64 rr;
	while ((rr = read(device->fd, &event, sizeof(struct input_event))) > 0) {

		f64 timestamp64 = ((f64)event.time.tv_sec - state_ptr->gamepad_state->start_time) + (f64)event.time.tv_usec * 0.000001;
		f32 timestamp = (f32)timestamp64;

		if (event.type == EV_ABS) {
			// Axis move event.
			if (event.code > ABS_MAX || device->axis_map[event.code] == -1) {
				continue;
			}

			struct input_absinfo ai = device->axis_info[event.code];

			gamepad_axis axis = (gamepad_axis)device->axis_map[event.code];
			if (axis == GAMEPAD_AXIS_IGNORE) {
				// Boot, treat as processed.
				continue;
			}

			f32 value = (event.value - ai.minimum) / (f32)(ai.maximum - ai.minimum) * 2.0f - 1.0f;
			f32 last_value = device->axis_states[device->axis_map[event.code]];

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

				gamepad_queue_button_event(device, timestamp, mapped_button, pressed);
				FLAG_SET(device->button_states, mapped_button, pressed);
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

				gamepad_queue_axis_event(device, timestamp, axis, value, last_value);
			}

			device->axis_states[device->axis_map[event.code]] = value;
		} else if (event.type == EV_KEY) {
			// Button press event.
			i32 button_code = event.code - BTN_MISC;

			if (event.code < BTN_MISC || event.code > KEY_MAX || device->button_map[button_code] == -1) {
				continue;
			}

			gamepad_button button = (gamepad_button)device->button_map[button_code];
			if (button == GAMEPAD_BUTTON_IGNORE) {
				// Boot, treat as processed.
				continue;
			}
			b8 pressed = (b8)(!!event.value);

			gamepad_queue_button_event(device, timestamp, button, pressed);
			FLAG_SET(device->button_states, button, pressed);
		}
	}

	if (rr == -1) {
		KDEBUG("read failed: errno=%d\n", errno);
	}

	// As soon as the device cannot be read, notify of a detachment.
	platform_gamepad_event_data detach_event = {
		.device_id = device->device_id};
	gamepad_queue_event(device->device_id, PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_DETACHED, detach_event);

	// Remove the device from the listing.
	pthread_mutex_lock(&state_ptr->gamepad_state->devices_mutex);

	i32 index = -1;
	u32 len = darray_length(state_ptr->gamepad_state->devices);
	for (u32 i = 0; i < len; ++i) {
		if (state_ptr->gamepad_state->devices[i]->device_id == device->device_id) {
			// Found it.
			index = (i32)i;
			break;
		}
	}

	KASSERT_MSG(index != -1, "Tried to remove gamepad that was not found (-1)");

	// Pop out of that index.
	darray_pop_at(state_ptr->gamepad_state->devices, index, KNULL);

	pthread_mutex_unlock(&state_ptr->gamepad_state->devices_mutex);

	return KNULL;
}

void gamepad_queue_event (u32 device_id, platform_gamepad_event_type type, platform_gamepad_event_data event_data) {
	platform_gamepad_event queued_event = {
		.device_id = device_id,
		.type = type,
		.data = event_data};

#	if TRACE_GAMEPAD_THREADING
	pthread_t thread = pthread_self();
	KTRACE("Thread %lu locking event mutex\n", (unsigned long)thread);
	KTRACE("Mutex address: %p\n", (void *)&state_ptr->gamepad_state->event_queue_mutex);
#	endif
	i32 code = pthread_mutex_lock(&state_ptr->gamepad_state->event_queue_mutex);
	if (code != 0) {
		KFATAL("pthread mutex lock failed: %d\n", code);
	}
#	if TRACE_GAMEPAD_THREADING
	KTRACE("Thread %lu lock returned %d\n", (unsigned long)thread, code);
#	endif

	darray_push(state_ptr->gamepad_state->event_queue, &queued_event);

	pthread_mutex_unlock(&state_ptr->gamepad_state->event_queue_mutex);

#	if TRACE_GAMEPAD_THREADING
	KTRACE("Thread %lu unlocking event mutex\n", (unsigned long)thread);
#	endif
}

void gamepad_queue_axis_event (platform_gamepad_device *device, f32 timestamp, gamepad_axis axis, f32 value, f32 last_value) {
	platform_gamepad_event_data event_data = {
		.device_id = device->device_id,
		.timestamp = timestamp,
		.axis = axis,
		.value = value,
		.last_value = last_value};

	gamepad_queue_event(device->device_id, PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED, event_data);
}
void gamepad_queue_button_event (platform_gamepad_device *device, f32 timestamp, gamepad_button button, b8 is_down) {
	platform_gamepad_event_data event_data = {
		.device_id = device->device_id,
		.timestamp = timestamp,
		.button = button,
		.is_down = is_down};

	platform_gamepad_event_type type = is_down ? PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED : PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED;

	gamepad_queue_event(device->device_id, type, event_data);
}

void process_gamepad_events (platform_state *state) {
	if (!FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_INITIALIZED_BIT) || FLAG_GET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT)) {
		return;
	}

	if (!FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT, true)) {
		return;
	}

#	if TRACE_GAMEPAD_THREADING
	pthread_t thread = pthread_self();
	KTRACE("Thread %lu locking event mutex\n", (unsigned long)thread);
	KTRACE("Mutex address: %p\n", (void *)&state->gamepad_state->event_queue_mutex);
#	endif
	i32 code = pthread_mutex_lock(&state->gamepad_state->event_queue_mutex);
	if (code != 0) {
		KFATAL("pthread mutex lock failed: %d\n", code);
	}
#	if TRACE_GAMEPAD_THREADING
	KTRACE("Thread %lu lock returned %d\n", (unsigned long)thread, code);
#	endif

	u32 count = darray_length(state->gamepad_state->event_queue);
	for (u32 i = 0; i < count; ++i) {
		platform_gamepad_event *event = &state->gamepad_state->event_queue[i];
		platform_gamepad_device *device = get_gamepad_device_by_id(event->device_id);
		switch (event->type) {
		case PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_ATTACHED:
			if (state->gamepad_callbacks.on_attached) {
				state->gamepad_callbacks.on_attached(event->device_id);
			}
			break;
		case PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_DETACHED:
			if (state->gamepad_callbacks.on_detached) {
				state->gamepad_callbacks.on_detached(event->device_id);
			}
			cleanup_gamepad_device(event->device_id);
			break;
		case PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED:
			if (state->gamepad_callbacks.on_button_pressed) {
				state->gamepad_callbacks.on_button_pressed(
					event->device_id,
					event->data.button,
					event->data.timestamp);
			}
			break;
		case PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED:
			if (state->gamepad_callbacks.on_button_released) {
				state->gamepad_callbacks.on_button_released(
					event->device_id,
					event->data.button,
					event->data.timestamp);
			}
			break;
		case PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED: {

			if (state->gamepad_callbacks.on_axis_changed) {
				state_ptr->gamepad_callbacks.on_axis_changed(
					device->device_id,
					event->data.axis,
					event->data.value,
					event->data.last_value,
					event->data.timestamp);
			}
		} break;
		}
	}

	darray_clear(state->gamepad_state->event_queue);

	pthread_mutex_unlock(&state->gamepad_state->event_queue_mutex);

#	if TRACE_GAMEPAD_THREADING
	KTRACE("Thread %lu unlocking event mutex\n", (unsigned long)thread);
#	endif

	if (!FLAG_SET(state->gamepad_state->flags, PLATFORM_GAMEPAD_STATE_FLAG_PROCESSING_EVENTS_BIT, false)) {
		return;
	}
}

void cleanup_gamepad_device (u32 device_id) {
	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (device) {
		close(device->fd);
		string_free(device->path);
		string_free(device->description);
	}
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
		case 16:
			return GAMEPAD_AXIS_DPAD_LR;
		case 17:
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
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 2:
			return GAMEPAD_AXIS_LZ; // L2
		case 3:
			return GAMEPAD_AXIS_RX;
		case 4:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_RZ; // R2
		}
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 2:
			return GAMEPAD_AXIS_LZ; // L2
		case 3:
			return GAMEPAD_AXIS_RX;
		case 4:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_RZ; // R2
		case 16:
			return GAMEPAD_AXIS_DPAD_LR;
		case 17:
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
		case 16:
			return GAMEPAD_AXIS_DPAD_LR;
		case 17:
			return GAMEPAD_AXIS_DPAD_UD;
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
		case 16:
			return GAMEPAD_AXIS_DPAD_LR;
		case 17:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	}
}

static gamepad_button gamepad_get_button_microsoft (u16 product_id, u32 button_id) {
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
		default:
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
		case 8:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 9:
			return GAMEPAD_BUTTON_L3;
		case 10:
			return GAMEPAD_BUTTON_R3;
		case 11:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 12:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 13:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 14:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		}
	}
}

static gamepad_button gamepad_get_button_sony (u16 product_id, u32 button_id) {
	switch (product_id) {
	case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
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
			// These are axes on PS3 and not mapped to button ids.
		case 13:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 14:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 15:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 16:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		}
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
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
			// touchpad click
			return GAMEPAD_BUTTON_ADDL0;
		}
	}
}

static gamepad_button gamepad_get_button_nintendo (u16 product_id, u32 button_id) {
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

static gamepad_button gamepad_get_button_default (u16 product_id, u32 button_id) {
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

gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u32 button_id) {
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

#endif
