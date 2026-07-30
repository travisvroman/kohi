#include "platform.h" // must be included here to get platform selection below.

// Linux platform layer.
#if KPLATFORM_LINUX
// #include <X11/extensions/Xrender.h>
// #include <xcb/xproto.h>
#	define _POSIX_C_SOURCE 200809L

#	ifndef XRANDR_ROTATION_LEFT
#		define XRANDR_ROTATION_LEFT (1 << 1)
#	endif
#	ifndef XRANDR_ROTATION_RIGHT
#		define XRANDR_ROTATION_RIGHT (1 << 9)
#	endif

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
#	include <ctype.h>

#	if _POSIX_C_SOURCE >= 199309L
#		include <time.h> // nanosleep
#	endif

#	include <errno.h> // For error reporting
#	include <fcntl.h>
#	include <limits.h> // Used for SSIZE_MAX
#	include <pthread.h>
#	include <stdio.h>
#	include <stdlib.h>
#	include <string.h>
#	include <sys/sendfile.h>
#	include <sys/stat.h>
#	include <sys/sysinfo.h> // Processor info
#	include <sys/utsname.h>
#	include <unistd.h>
#	include <systemd/sd-bus.h>
#	include <systemd/sd-bus-protocol.h>
#	include <dlfcn.h> // dlopen

#	include "defines.h"
#	include "containers/darray.h"
#	include "debug/kassert.h"
#	include "input_types.h"
#	include "logger.h"
#	include "memory/kmemory.h"
#	include "strings/kstring.h"

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

typedef int (*PFN_sd_bus_open_user)(sd_bus **ret);
typedef int (*PFN_sd_bus_call_method)(sd_bus *bus, const char *destination, const char *path, const char *interface, const char *member, sd_bus_error *reterr_error, sd_bus_message **ret_reply, const char *types, ...);
typedef int (*PFN_sd_bus_message_new_method_call)(sd_bus *bus, sd_bus_message **ret, const char *destination, const char *path, const char *interface, const char *member);
typedef int (*PFN_sd_bus_call)(sd_bus *bus, sd_bus_message *m, uint64_t usec, sd_bus_error *reterr_error, sd_bus_message **ret_reply);
typedef int (*PFN_sd_bus_message_read)(sd_bus_message *m, const char *types, ...);
typedef int (*PFN_sd_bus_message_skip)(sd_bus_message *m, const char *types);
typedef int (*PFN_sd_bus_message_append)(sd_bus_message *m, const char *types, ...);
typedef int (*PFN_sd_bus_message_append_array)(sd_bus_message *m, char type, const void *ptr, size_t size);
typedef int (*PFN_sd_bus_add_match)(sd_bus *bus, sd_bus_slot **ret_slot, const char *match, sd_bus_message_handler_t callback, void *userdata);
typedef int (*PFN_sd_bus_process)(sd_bus *bus, sd_bus_message **ret);
typedef int (*PFN_sd_bus_wait)(sd_bus *bus, uint64_t timeout_usec);
typedef sd_bus *(*PFN_sd_bus_unref)(sd_bus *bus);
typedef int (*PFN_sd_bus_message_enter_container)(sd_bus_message *m, char type, const char *contents);
typedef int (*PFN_sd_bus_message_exit_container)(sd_bus_message *m);
typedef int (*PFN_sd_bus_message_open_container)(sd_bus_message *m, char type, const char *contents);
typedef int (*PFN_sd_bus_message_close_container)(sd_bus_message *m);

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

	u8 last_keycode;
	u32 last_key_time;

	internal_clipboard_state clipboard;

	void *sd_bus_lib;
	b8 kill_sd_bus_loop;
	PFN_sd_bus_open_user ksd_bus_open_user;
	PFN_sd_bus_call_method ksd_bus_call_method;
	PFN_sd_bus_message_new_method_call ksd_bus_message_new_method_call;
	PFN_sd_bus_call ksd_bus_call;
	PFN_sd_bus_message_read ksd_bus_message_read;
	PFN_sd_bus_message_skip ksd_bus_message_skip;
	PFN_sd_bus_message_append ksd_bus_message_append;
	PFN_sd_bus_message_append_array ksd_bus_message_append_array;
	PFN_sd_bus_add_match ksd_bus_add_match;
	PFN_sd_bus_process ksd_bus_process;
	PFN_sd_bus_wait ksd_bus_wait;
	PFN_sd_bus_unref ksd_bus_unref;
	PFN_sd_bus_message_enter_container ksd_bus_message_enter_container;
	PFN_sd_bus_message_exit_container ksd_bus_message_exit_container;
	PFN_sd_bus_message_open_container ksd_bus_message_open_container;
	PFN_sd_bus_message_close_container ksd_bus_message_close_container;
	const char **ofd_files_temp;
} platform_state;

static platform_state *state_ptr;

static void platform_update_watches (void);
static b8 key_is_repeat (platform_state *state, const xcb_key_press_event_t *ev);
// Key translation
static keys translate_keycode (u32 x_keycode);
static kwindow *window_from_handle (xcb_window_t window);

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

	state->kill_sd_bus_loop = false;
	state->sd_bus_lib = dlopen("libsystemd.so", RTLD_LOCAL | RTLD_NOW);
	if (state->sd_bus_lib) {
		state->ksd_bus_open_user = dlsym(state->sd_bus_lib, "sd_bus_open_user");
		state->ksd_bus_call_method = dlsym(state->sd_bus_lib, "sd_bus_call_method");
		state->ksd_bus_message_new_method_call = dlsym(state->sd_bus_lib, "sd_bus_message_new_method_call");
		state->ksd_bus_call = dlsym(state->sd_bus_lib, "sd_bus_call");
		state->ksd_bus_message_read = dlsym(state->sd_bus_lib, "sd_bus_message_read");
		state->ksd_bus_message_skip = dlsym(state->sd_bus_lib, "sd_bus_message_skip");
		state->ksd_bus_message_append = dlsym(state->sd_bus_lib, "sd_bus_message_append");
		state->ksd_bus_message_append_array = dlsym(state->sd_bus_lib, "sd_bus_message_append_array");
		state->ksd_bus_add_match = dlsym(state->sd_bus_lib, "sd_bus_add_match");
		state->ksd_bus_process = dlsym(state->sd_bus_lib, "sd_bus_process");
		state->ksd_bus_wait = dlsym(state->sd_bus_lib, "sd_bus_wait");
		state->ksd_bus_unref = dlsym(state->sd_bus_lib, "sd_bus_unref");
		state->ksd_bus_message_enter_container = dlsym(state->sd_bus_lib, "sd_bus_message_enter_container");
		state->ksd_bus_message_exit_container = dlsym(state->sd_bus_lib, "sd_bus_message_exit_container");
		state->ksd_bus_message_open_container = dlsym(state->sd_bus_lib, "sd_bus_message_open_container");
		state->ksd_bus_message_close_container = dlsym(state->sd_bus_lib, "sd_bus_message_close_container");
	} else {
		KERROR("Linux platform error: Unable to load sd-bus. Some platform functions (such as browsing for files) may not be available.");
	}
	return true;
}

void platform_system_shutdown (struct platform_state *state) {
	if (state) {
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
	xcb_intern_atom_cookie_t wm_delete_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
	xcb_intern_atom_cookie_t wm_protocols_cookie = xcb_intern_atom(state_ptr->handle.connection, 0, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
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
		window->platform_state->window = KNULL;
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
			// Input events
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

		return !quit_flagged;
	}
	return true;
}

void *platform_allocate (u64 size, b8 aligned) {
	return malloc(size);
}
void platform_free (void *block, b8 aligned) {
	free(block);
}
void *platform_zero_memory (void *block, u64 size) {
	return memset(block, 0, size);
}
void *platform_copy_memory (void *dest, const void *source, u64 size) {
	return memcpy(dest, source, size);
}
void *platform_set_memory (void *dest, i32 value, u64 size) {
	return memset(dest, value, size);
}

void platform_console_write (struct platform_state *platform, log_level level, const char *message) {
	b8 is_error = (level == LOG_LEVEL_ERROR || level == LOG_LEVEL_FATAL);
	FILE *console_handle = is_error ? stderr : stdout;
	// FATAL,ERROR,WARN,INFO,DEBUG,TRACE
	const char *colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
	fprintf(console_handle, "\033[%sm%s\033[0m", colour_strings[level], message);
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
	return "./lib";
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

		if (strncmp(line, "physical id", 11) == 0) {
			sscanf(line, "physical id : %d", &physical_id);
		} else if (strncmp(line, "core id", 7) == 0) {
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
		snprintf(path, sizeof(path),
				 "/sys/devices/system/memory/memory%d/dimm_speed", i);

		f = fopen(path, "r");
		if (!f)
			continue;

		if (fgets(buf, sizeof(buf), f)) {
			u32 mhz = (u32)strtoul(buf, NULL, 10);
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
		if (strncmp(line, "model name", 10) == 0) {
			sscanf(line, "model name : %[^\n]", s->cpu_name);
		} else if (strncmp(line, "cpu MHz", 7) == 0) {
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
				if (strncmp(entry->d_name, "cpu", 3) == 0 && isdigit(entry->d_name[3])) {
					char path[256];
					snprintf(path, sizeof(path),
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

	strcpy(s->os_name, "Linux");
	strcpy(s->kernel_version, u.release);

	FILE *f = fopen("/etc/os-release", "r");
	if (!f) {
		return;
	}

	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
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
static kdrive_type linux_classify_drive (
	const char *device,		 // /dev/sda1
	const char *mount_point, // /
	const char *fs_type		 // ext4, tmpfs, nfs, ...
) {
	// 1. No mount point
	if (!mount_point || !mount_point[0])
		return KDRIVE_TYPE_NO_ROOT_DIR;

	// 2. RAM disk
	if (!strcmp(fs_type, "tmpfs") || !strcmp(fs_type, "ramfs"))
		return KDRIVE_TYPE_RAMDISK;

	// 3. Network drive
	if (!strcmp(fs_type, "nfs") ||
		!strcmp(fs_type, "nfs4") ||
		!strcmp(fs_type, "cifs") ||
		!strcmp(fs_type, "smbfs") ||
		!strcmp(fs_type, "sshfs") ||
		!strcmp(fs_type, "fuse.sshfs") ||
		!strcmp(fs_type, "davfs"))
		return KDRIVE_TYPE_REMOTE;

	// Only real block devices below this point
	if (strncmp(device, "/dev/", 5) != 0)
		return KDRIVE_TYPE_UNKNOWN;

	// Extract parent disk: sda1 → sda, nvme0n1p2 → nvme0n1
	char disk[128];
	snprintf(disk, sizeof(disk), "%s", device + 5);

	size_t len = strlen(disk);
	while (len && isdigit(disk[len - 1]))
		disk[--len] = 0;
	if (len && disk[len - 1] == 'p')
		disk[--len] = 0;

	// 4. Optical drive
	char path[256], media[64];
	snprintf(path, sizeof(path), "/sys/block/%s/device/media", disk);
	if (file_read_string(path, media, sizeof(media))) {
		if (!strcmp(media, "cdrom"))
			return KDRIVE_TYPE_CDROM;
	}

	// 5. Removable
	int removable = 0;
	snprintf(path, sizeof(path), "/sys/block/%s/removable", disk);
	if (file_read_int(path, &removable) && removable == 1)
		return KDRIVE_TYPE_REMOVABLE;

	// 6. Fixed disk
	snprintf(path, sizeof(path), "/sys/block/%s", disk);
	if (access(path, F_OK) == 0)
		return KDRIVE_TYPE_FIXED;

	return KDRIVE_TYPE_UNKNOWN;
}

static void linux_query_storage (ksystem_info *s) {
	s->storage_count = 0;

	FILE *f = fopen("/proc/self/mounts", "r");
	if (f) {

		char line[1024];

		while (fgets(line, sizeof(line), f) && s->storage_count < KMAX_STORAGE_DEVICES) {
			char device[256];
			char mount[256];
			char fs[64];

			// format: device mount fs options dump pass
			if (sscanf(line, "%255s %255s %63s", device, mount, fs) != 3)
				continue;

			// Only real block devices
			if (strncmp(device, "/dev/", 5) != 0)
				continue;

			// Skip pseudo FS
			if (!strcmp(fs, "tmpfs") ||
				!strcmp(fs, "proc") ||
				!strcmp(fs, "sysfs") ||
				!strcmp(fs, "devtmpfs"))
				continue;

			struct statvfs vfs;
			if (statvfs(mount, &vfs) != 0)
				continue;

			kstorage_info *m = &s->storage[s->storage_count++];
			strncpy(m->name, device, sizeof(m->name));
			strncpy(m->mount_point, mount, sizeof(m->mount_point));
			m->total_bytes = (u64)vfs.f_blocks * vfs.f_frsize;
			m->free_bytes = (u64)vfs.f_bavail * vfs.f_frsize;
			m->type = linux_classify_drive(device, mount, fs);
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
		if (strncmp(ent->d_name, "17-", 3) != 0)
			continue;

		char path[512];
		snprintf(path, sizeof(path),
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
	strcpy(out_info->cpu_arch, "x86_64");
#	elif defined(__aarch64__)
	strcpy(out_info->cpu_arch, "arm_64");
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

static i32 openfiledialog_on_response (sd_bus_message *m, void *user_data, sd_bus_error *error) {
	u32 response;
	state_ptr->ksd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "u a{sv}");

	// 0=success, 1=cancel
	state_ptr->ksd_bus_message_read(m, "u", &response);
	if (response != 0) {
		KTRACE("User cancelled.");
		state_ptr->kill_sd_bus_loop = true;
		return 0;
	}

	// Read the results dictionary
	state_ptr->ksd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "{sv}");

	state_ptr->ofd_files_temp = darray_create(const char *);

	const char *key;
	while (state_ptr->ksd_bus_message_enter_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv")) {
		state_ptr->ksd_bus_message_read(m, "s", &key);
		if (strings_equal(key, "uris")) {
			state_ptr->ksd_bus_message_enter_container(m, SD_BUS_TYPE_VARIANT, "as");
			state_ptr->ksd_bus_message_enter_container(m, SD_BUS_TYPE_ARRAY, "s");

			const char *uri;
			while (state_ptr->ksd_bus_message_read(m, "s", &uri)) {
				// Add to selection list, sanitizing "file://" from the front.
				// TODO: May need to decode this path too, to handle spaces, etc.
				char *turi = string_duplicate(uri + 7);
				darray_push(state_ptr->ofd_files_temp, &turi);
				/* KTRACE("Selected: '%s'", uri); */
			}

			state_ptr->ksd_bus_message_exit_container(m); // array
			state_ptr->ksd_bus_message_exit_container(m); // variant
		} else {
			state_ptr->ksd_bus_message_skip(m, "v");
		}

		state_ptr->ksd_bus_message_exit_container(m); // dictionary entry
	}

	state_ptr->kill_sd_bus_loop = true;

	return 0;
}

typedef struct open_file_filter_type_data {
	const char *description;
	u8 extension_count;
	const char **extensions;
} open_file_filter_type_data;

static open_file_filter_type_data *parse_open_file_filters (const char *filter_str, u8 *out_count) {
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
	open_file_filter_type_data *types = KALLOC_TYPE_CARRAY(open_file_filter_type_data, type_count);
	for (u32 i = 0; i < type_count; ++i) {
		char **extensions = darray_create(char *);
		u32 extensions_count = string_split(parts[(i * 2) + 1], ';', &extensions, true, false, false);
		open_file_filter_type_data type_data = {
			.description = string_duplicate(parts[(i * 2)]),
			.extension_count = (u8)extensions_count,
		};
		type_data.extensions = KALLOC_TYPE_CARRAY(const char *, extensions_count);
		for (u8 j = 0; j < extensions_count; ++j) {
			type_data.extensions[j] = string_duplicate(extensions[j]);
		}
		string_cleanup_split_darray(extensions);

		types[i] = type_data;
	}

	string_cleanup_split_darray(parts);

	*out_count = type_count;
	return types;
}

platform_open_file_dialog_result platform_open_file_dialog_open (platform_open_file_dialog_options options) {
	platform_open_file_dialog_result result = {
		.file_count = 0,
		.file_paths = KNULL,
		.success = false,
	};
	if (!state_ptr->sd_bus_lib) {
		KERROR("Linux platform: open file dialog not available.");
		return result;
	}

	state_ptr->kill_sd_bus_loop = false;

	sd_bus *bus = KNULL;
	sd_bus_message *m = KNULL;
	sd_bus_message *reply = KNULL;
	sd_bus_error err = SD_BUS_ERROR_NULL;

	i32 r = state_ptr->ksd_bus_open_user(&bus);
	if (r < 0) {
		KERROR("Failed to connect to bus. Open file dialog cannot open.");
		return result;
	}

	r = state_ptr->ksd_bus_message_new_method_call(
		bus,
		&m,
		"org.freedesktop.portal.Desktop",
		"/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.FileChooser",
		"OpenFile");

	// Parent and title
	const char *title = (options.title) ? options.title : "Open File";
	state_ptr->ksd_bus_message_append(m, "ss", "", title);

	// Options dictionary
	state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");

	// multiselect
	if (options.allow_multiselect) {
		state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
		state_ptr->ksd_bus_message_append(m, "s", "multiple");
		state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "b");
		state_ptr->ksd_bus_message_append(m, "b", 1);
		state_ptr->ksd_bus_message_close_container(m); // Close variant
		state_ptr->ksd_bus_message_close_container(m); // Close dict entry
	}

	// starting_dir
	{
		const char *path = options.starting_dir ? options.starting_dir : ".";

		state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
		state_ptr->ksd_bus_message_append(m, "s", "current_folder");
		state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "ay");
		state_ptr->ksd_bus_message_append_array(m, SD_BUS_TYPE_BYTE, path, string_length(path));
		state_ptr->ksd_bus_message_close_container(m); // Close variant
		state_ptr->ksd_bus_message_close_container(m); // Close dict entry
	}

	// Filters, if included. If not, a default of *.*/all files is assumed.
	if (options.filter) {
		u8 type_count = 0;
		open_file_filter_type_data *types = parse_open_file_filters(options.filter, &type_count);
		if (type_count > 0) {
			state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
			state_ptr->ksd_bus_message_append(m, "s", "filters");
			state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "a(sa(us))");
			state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(sa(us))");

			// Each type
			for (u8 i = 0; i < type_count; ++i) {
				open_file_filter_type_data *type = &types[i];
				state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "sa(us)");
				state_ptr->ksd_bus_message_append(m, "s", type->description);
				KTRACE("Type desc: '%s'", type->description);

				state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "(us)");

				// Each extension for the type.
				for (u8 j = 0; j < type->extension_count; ++j) {
					const char *ext = type->extensions[j];
					KTRACE("ext: '%s'", ext);
					state_ptr->ksd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "us");
					state_ptr->ksd_bus_message_append(m, "u", 0);
					state_ptr->ksd_bus_message_append(m, "s", ext);
					state_ptr->ksd_bus_message_close_container(m); // struct
																   //
				}

				state_ptr->ksd_bus_message_close_container(m); // type exts array
				state_ptr->ksd_bus_message_close_container(m); // type struct
			}

			// Close filters containers
			state_ptr->ksd_bus_message_close_container(m); // array
			state_ptr->ksd_bus_message_close_container(m); // variant
			state_ptr->ksd_bus_message_close_container(m); // struct
		}
	}
	state_ptr->ksd_bus_message_close_container(m); // Close options

	state_ptr->ksd_bus_call(bus, m, 0, &err, &reply);

	if (r < 0) {
		KERROR("Call failed with '%s'. Open file dialog cannot open.", err.message);
		return result;
	}

	// Read returned object path.
	const char *request_path = KNULL;
	state_ptr->ksd_bus_message_read(reply, "o", &request_path);

	KTRACE("Request path: '%s'", request_path);

	// Listen for response signal.
	state_ptr->ksd_bus_add_match(
		bus,
		KNULL,
		"type='signal',interface='org.freedesktop.portal.Request'",
		openfiledialog_on_response,
		KNULL // user_data
	);

	// Event loop
	while (true) {
		state_ptr->ksd_bus_process(bus, KNULL);
		state_ptr->ksd_bus_wait(bus, (u64)500000); // Half second between iterations.
		if (state_ptr->kill_sd_bus_loop) {
			break;
		}
	}
	state_ptr->kill_sd_bus_loop = false;

	// Read from results list here and return listing of file paths.
	result.success = state_ptr->ofd_files_temp != KNULL;
	result.file_count = (u8)darray_length(state_ptr->ofd_files_temp);
	if (result.success) {
		KDUPLICATE_TYPE_CARRAY(result.file_paths, state_ptr->ofd_files_temp, const char *, result.file_count);
	}
	darray_destroy(state_ptr->ofd_files_temp);
	state_ptr->ofd_files_temp = KNULL;

	state_ptr->ksd_bus_unref(bus);
	return result;
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
	case XK_Super_L:
		// Treat the "meta" key (if mapped) as super
		return KEY_LSUPER;
	case XK_Meta_R:
	case XK_Super_R:
		// Treat the "meta" key (if mapped) as super
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
	case XK_equal:
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
	case XK_bracketleft:
		return KEY_LBRACKET;
	case XK_bracketright:
		return KEY_RBRACKET;
	case XK_quotedbl:
	case XK_quoteright:
		// case XK_quoteleft: // NOTE: for some reason this code is the same as XK_grave???
		// NOTE: Both of these are required since either can technically show up for this keypress,
		return KEY_QUOTE;
	case XK_backslash:
		return KEY_BACKSLASH;

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
