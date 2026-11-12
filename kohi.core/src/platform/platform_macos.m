#include "defines.h"
#include "input_types.h"
#include "platform/platform.h"
#include "utils/input_utils.h"

#if defined(KPLATFORM_APPLE)

#	include "logger.h"
#	include "memory/kmemory.h"
#	include "strings/kstring.h"
#	include "platform/kfeatures_runtime.h"

#	include "containers/darray.h"

#	include <string.h>
#	include <crt_externs.h>
#	include <mach/mach_time.h>

#	include <copyfile.h>
#	include <errno.h>
#	include <sys/stat.h>
#	include <sys/sysctl.h>
#	include <CoreFoundation/CoreFoundation.h>
#	include <DiskArbitration/DiskArbitration.h>
#	include <IOKit/IOKitLib.h>
#	include <IOKit/hid/IOHIDLib.h>
#	include <CoreFoundation/CFArray.h>
#	include <CoreFoundation/CFBase.h>
#	include <CoreFoundation/CFDictionary.h>
#	include <CoreFoundation/CFNumber.h>
#	include <CoreFoundation/CFRunLoop.h>
#	include <CoreFoundation/CFString.h>
#	include <IOKit/IOReturn.h>
#	include <IOKit/hid/IOHIDBase.h>
#	include <IOKit/hid/IOHIDDevice.h>
#	include <IOKit/hid/IOHIDDeviceTypes.h>
#	include <IOKit/hid/IOHIDElement.h>
#	include <IOKit/hid/IOHIDKeys.h>
#	include <IOKit/hid/IOHIDManager.h>
#	include <IOKit/hid/IOHIDUsageTables.h>
#	include <IOKit/hid/IOHIDValue.h>
#	include <AppKit/AppKit.h>
#	include <sys/mount.h>
#	include <sys/mman.h> // mmap/munmap

#	import <Cocoa/Cocoa.h>
#	import <Foundation/Foundation.h>
#	import <QuartzCore/CAMetalLayer.h>
#	import <QuartzCore/QuartzCore.h>
#	import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#	include "math/kmath.h"

#	define GAMEPAD_RUN_LOOP_MODE CFSTR("GamepadRunLoopMode")

@class ApplicationDelegate;
@class WindowDelegate;
@class ContentView;

typedef struct macos_handle_info {
	u32 dummy;
} macos_handle_info;

typedef struct macos_file_watch {
	u32 id;
	const char *file_path;
	b8 is_binary;
	platform_filewatcher_file_written_callback watcher_written_callback;
	void *watcher_written_context;
	platform_filewatcher_file_deleted_callback watcher_deleted_callback;
	void *watcher_deleted_context;
	long last_write_time;
} macos_file_watch;

typedef struct kwindow_platform_state {
	WindowDelegate *wnd_delegate;
	NSWindow *handle;
	ContentView *view;
	CAMetalLayer *layer;
	f32 device_pixel_ratio;
} kwindow_platform_state;

enum macos_modifier_keys {
	MACOS_MODIFIER_KEY_LSHIFT = 0x01,
	MACOS_MODIFIER_KEY_RSHIFT = 0x02,
	MACOS_MODIFIER_KEY_LCTRL = 0x04,
	MACOS_MODIFIER_KEY_RCTRL = 0x08,
	MACOS_MODIFIER_KEY_LOPTION = 0x10,
	MACOS_MODIFIER_KEY_ROPTION = 0x20,
	MACOS_MODIFIER_KEY_LCOMMAND = 0x40,
	MACOS_MODIFIER_KEY_RCOMMAND = 0x80
} macos_modifier_keys;

typedef struct hid_gamepad_axis {
	IOHIDElementCookie cookie;
	gamepad_axis axis;
	CFIndex logical_min;
	CFIndex logical_max;
	b8 has_null_state;
	b8 is_hat_switch;
	b8 is_hat_switch_second_axis;
} hid_gamepad_axis;

typedef struct hid_gamepad_button {
	IOHIDElementCookie cookie;
	gamepad_button button;
} hid_gamepad_button;

typedef enum platform_gamepad_event_type {
	PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_ATTACHED,
	PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_DETACHED,
	PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED,
	PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED,
	PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED
} platform_gamepad_event_type;

typedef enum platform_gamepad_connection_type {
	PLATFORM_GAMEPAD_CONNECTION_TYPE_UNKNOWN,
	PLATFORM_GAMEPAD_CONNECTION_TYPE_WIRED_USB,
	PLATFORM_GAMEPAD_CONNECTION_TYPE_WIRELESS_BT
} platform_gamepad_connection_type;

typedef struct platform_gamepad_event {
	u32 device_id;
	platform_gamepad_event_type type;
	gamepad_button button;
	gamepad_axis axis;
	b8 is_down;
	u8 unused;
	f32 timestamp;
	f32 value;
	f32 last_value;
} platform_gamepad_event;

#	define PLATFORM_GAMEPAD_MAX_AXES 128
#	define PLATFORM_GAMEPAD_MAX_BUTTONS 128

typedef struct platform_gamepad_device {
	u32 device_id;
	const char *description;
	i32 vendor_id;
	i32 product_id;
	platform_gamepad_connection_type connection_type;
	u8 axis_count;
	u8 button_count;
	f32 axis_states[PLATFORM_GAMEPAD_MAX_AXES];
	// Bit flags - 1 = pressed, 0 = released.
	u32 button_states;

	IOHIDDeviceRef device_ref;
	hid_gamepad_axis *axis_elements;
	hid_gamepad_button *button_elements;
} platform_gamepad_device;

typedef struct platform_gamepad_state {
	IOHIDManagerRef hid_manager;

	// darray
	platform_gamepad_device **devices;
	u32 next_device_id;

	// darray
	platform_gamepad_event *input_event_queue;
	platform_gamepad_event *device_event_queue;

	f64 start_time;
} platform_gamepad_state;

typedef struct platform_state {
	ApplicationDelegate *app_delegate;
	macos_handle_info handle;
	b8 quit_flagged;
	u8 modifier_key_states;
	// darray
	macos_file_watch *watches;

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
} platform_state;

static platform_state *state_ptr;

static void handle_resize (kwindow *w, u16 width, u16 height);

// Key translation
static keys translate_keycode (u32 ns_keycode);
// Modifier key handling
static void handle_modifier_keys (u32 ns_keycode, u32 modifier_flags);
static void platform_update_watches (void);

static gamepad_axis gamepad_get_axis (u16 vendor_id, u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type);
static gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type);
static platform_gamepad_device *get_gamepad_device_by_id (u32 device_id);
static void detect_gamepad_devices (void);
static void process_gamepad_events (void);

@interface WindowDelegate : NSObject <NSWindowDelegate> {
  @public
	kwindow *kohi_window;
}

- (instancetype)initWithState:(kwindow *)init_state;

@end // WindowDelegate

@interface ContentView : NSView <NSTextInputClient> {
	NSWindow *window;
	NSTrackingArea *trackingArea;
	NSMutableAttributedString *markedText;
}

- (instancetype)initWithWindow:(NSWindow *)initWindow;

@end // ContentView

@implementation ContentView

- (instancetype)initWithWindow:(NSWindow *)initWindow {
	self = [super init];
	if (self != nil) {
		window = initWindow;
	}

	return self;
}

- (BOOL)canBecomeKeyView {
	return YES;
}

- (BOOL)acceptsFirstResponder {
	return YES;
}

- (BOOL)wantsUpdateLayer {
	return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event {
	return YES;
}

- (void)mouseDown:(NSEvent *)event {
	state_ptr->process_mouse_button(MOUSE_BUTTON_LEFT, true);
}

- (void)mouseDragged:(NSEvent *)event {
	// Equivalent to moving the mouse for now
	[self mouseMoved:event];
}

- (void)mouseUp:(NSEvent *)event {
	state_ptr->process_mouse_button(MOUSE_BUTTON_LEFT, false);
}

- (void)mouseMoved:(NSEvent *)event {
	const NSPoint pos = [event locationInWindow];
	kwindow *w = ((WindowDelegate *)event.window.delegate)->kohi_window;
	kwindow_platform_state *ps = w->platform_state;

	// Need to invert Y on macOS, since origin is bottom-left.
	// Also need to scale the mouse position by the device pixel ratio so screen lookups are correct.
	NSSize window_size = ps->layer.drawableSize;
	i16 x = pos.x * ps->layer.contentsScale;
	i16 y = window_size.height - (pos.y * ps->layer.contentsScale);

	state_ptr->process_mouse_move(x, y);
}

- (void)rightMouseDown:(NSEvent *)event {
	state_ptr->process_mouse_button(MOUSE_BUTTON_RIGHT, true);
}

- (void)rightMouseDragged:(NSEvent *)event {
	// Equivalent to moving the mouse for now
	[self mouseMoved:event];
}

- (void)rightMouseUp:(NSEvent *)event {
	state_ptr->process_mouse_button(MOUSE_BUTTON_RIGHT, false);
}

- (void)otherMouseDown:(NSEvent *)event {
	// Interpreted as middle click
	state_ptr->process_mouse_button(MOUSE_BUTTON_MIDDLE, true);
}

- (void)otherMouseDragged:(NSEvent *)event {
	// Equivalent to moving the mouse for now
	[self mouseMoved:event];
}

- (void)otherMouseUp:(NSEvent *)event {
	// Interpreted as middle click
	state_ptr->process_mouse_button(MOUSE_BUTTON_MIDDLE, false);
}

// Handle modifier keys since they are only registered via modifier flags being set/unset.
- (void)flagsChanged:(NSEvent *)event {
	handle_modifier_keys([event keyCode], [event modifierFlags]);
}

- (void)keyDown:(NSEvent *)event {
	keys key = translate_keycode((u32)[event keyCode]);

	state_ptr->process_key(key, true, [event isARepeat]);

	// [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent *)event {
	keys key = translate_keycode((u32)[event keyCode]);

	state_ptr->process_key(key, false, false);
}

- (void)scrollWheel:(NSEvent *)event {
	state_ptr->process_mouse_wheel((i8)[event scrollingDeltaY]);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {
}

- (void)unmarkText {
}

// Defines a constant for empty ranges in NSTextInputClient
static const NSRange kEmptyRange = {NSNotFound, 0};

- (NSRange)selectedRange {
	return kEmptyRange;
}

- (NSRange)markedRange {
	return kEmptyRange;
}

- (BOOL)hasMarkedText {
	return false;
}

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
	return nil;
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {
	return [NSArray array];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
	return NSMakeRect(0, 0, 0, 0);
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
	return 0;
}

@end // ContentView

@interface ApplicationDelegate : NSObject <NSApplicationDelegate> {
}

@end // ApplicationDelegate

@implementation ApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
	// Posting an empty event at start
	@autoreleasepool {

		NSEvent *event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
											location:NSMakePoint(0, 0)
									   modifierFlags:0
										   timestamp:0
										windowNumber:0
											 context:nil
											 subtype:0
											   data1:0
											   data2:0];
		[NSApp postEvent:event atStart:YES];

	} // autoreleasepool

	[NSApp stop:nil];
}

@end // ApplicationDelegate

/**
 * WindowDelegate implementation
 */
@implementation WindowDelegate

- (instancetype)initWithState:(kwindow *)init_state {
	self = [super init];

	if (self != nil) {
		kohi_window = init_state;
		state_ptr->quit_flagged = false;
	}

	return self;
}

- (BOOL)windowShouldClose:(NSWindow *)sender {
	state_ptr->quit_flagged = true;

	if (state_ptr->window_closed_callback) {
		kwindow *w = ((WindowDelegate *)sender.delegate)->kohi_window;
		if (!w) {
			KERROR("Recieved a window close event for a non-registered window!");
			return NO;
		}
		state_ptr->window_closed_callback(w);
	}

	return YES;
}

- (void)windowDidChangeScreen:(NSNotification *)notification {
	NSWindow *window = [notification object];
	kwindow *w = ((WindowDelegate *)window.delegate)->kohi_window;
	kwindow_platform_state *ps = w->platform_state;
	CGSize viewSize = ps->view.bounds.size;
	NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
	ps->layer.drawableSize = newDrawableSize;
	ps->layer.contentsScale = ps->view.window.backingScaleFactor;
	// Save off the device pixel ratio.
	ps->device_pixel_ratio = ps->layer.contentsScale;

	u16 width = (u16)newDrawableSize.width;
	u16 height = (u16)newDrawableSize.height;
	handle_resize(w, width, height);
}

- (void)windowDidResize:(NSNotification *)notification {
	NSWindow *window = [notification object];
	kwindow *w = ((WindowDelegate *)window.delegate)->kohi_window;
	kwindow_platform_state *ps = w->platform_state;
	CGSize viewSize = ps->view.bounds.size;
	NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
	ps->layer.drawableSize = newDrawableSize;
	ps->layer.contentsScale = ps->view.window.backingScaleFactor;
	// Save off the device pixel ratio.
	ps->device_pixel_ratio = ps->layer.contentsScale;

	u16 width = (u16)newDrawableSize.width;
	u16 height = (u16)newDrawableSize.height;
	handle_resize(w, width, height);
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
	// Send a size of 0, which tells the application it was minimized.
	NSWindow *window = [notification object];
	kwindow *w = ((WindowDelegate *)window.delegate)->kohi_window;
	kwindow_platform_state *ps = w->platform_state;

	handle_resize(w, 0, 0);

	[ps->handle miniaturize:nil];
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
	NSWindow *window = [notification object];
	kwindow *w = ((WindowDelegate *)window.delegate)->kohi_window;
	kwindow_platform_state *ps = w->platform_state;
	CGSize viewSize = ps->view.bounds.size;
	NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
	ps->layer.drawableSize = newDrawableSize;
	ps->layer.contentsScale = ps->view.window.backingScaleFactor;
	// Save off the device pixel ratio.
	ps->device_pixel_ratio = ps->layer.contentsScale;

	u16 width = (u16)newDrawableSize.width;
	u16 height = (u16)newDrawableSize.height;
	handle_resize(w, width, height);

	[ps->handle deminiaturize:nil];
}

@end // WindowDelegate

static void handle_resize (kwindow *w, u16 width, u16 height) {

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
}

b8 platform_system_startup (u64 *memory_requirement, platform_state *state, platform_system_config *config) {
	*memory_requirement = sizeof(platform_state);
	if (state == 0) {
		return true;
	}

	state_ptr = state;

	// Create the array of window pointers.
	state->windows = darray_create(kwindow *);
	state->window_closed_callback = 0;
	state->window_resized_callback = 0;
	state->process_key = 0;
	state->process_mouse_button = 0;
	state->process_mouse_move = 0;
	state->process_mouse_wheel = 0;

	@autoreleasepool {

		[NSApplication sharedApplication];

		// App delegate creation
		state_ptr->app_delegate = [[ApplicationDelegate alloc] init];
		if (!state_ptr->app_delegate) {
			KERROR("Failed to create application delegate")
			return false;
		}
		[NSApp setDelegate:state_ptr->app_delegate];

		// window create
		if (![[NSRunningApplication currentApplication] isFinishedLaunching])
			[NSApp run];

		// Making the app a proper UI app since we're unbundled
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		// Putting window in front on launch
		[NSApp activateIgnoringOtherApps:YES];
		return true;

	} // autoreleasepool
}

void platform_system_shutdown (platform_state *state) {
	if (state_ptr) {

		u32 window_count = darray_length(state_ptr->windows);
		for (u32 i = 0; i < window_count; ++i) {
			platform_window_destroy(state_ptr->windows[i]);
		}
		darray_destroy(state_ptr->windows);

		@autoreleasepool {

			[NSApp setDelegate:nil];
			[state_ptr->app_delegate release];
			state_ptr->app_delegate = nil;

		} // autoreleasepool
	}
	state_ptr = 0;
}

b8 platform_window_create (const kwindow_config *config, struct kwindow *window, b8 show_immediately) {
	if (!window) {
		return false;
	}

	window->platform_state = kallocate(sizeof(kwindow_platform_state), MEMORY_TAG_PLATFORM);
	kwindow_platform_state *ps = window->platform_state;
	ps->device_pixel_ratio = 1.0f;

	// Window delegate creation
	ps->wnd_delegate = [[WindowDelegate alloc] initWithState:window];
	if (!ps->wnd_delegate) {
		KERROR("Failed to create window delegate")
		return false;
	}

	// Window creation
	i32 client_x = config->position_x;
	i32 client_y = config->position_y;
	u32 client_width = config->width;
	u32 client_height = config->height;
	ps->handle = [[NSWindow alloc]
		initWithContentRect:NSMakeRect(client_x, client_y, client_width, client_height)
				  styleMask:NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
					backing:NSBackingStoreBuffered
					  defer:NO];
	if (!ps->handle) {
		KERROR("Failed to create window");
		return false;
	}

	// View creation
	ps->view = [[ContentView alloc] initWithWindow:ps->handle];
	[ps->view setWantsLayer:YES];

	// Layer creation
	ps->layer = [CAMetalLayer layer];
	if (!ps->layer) {
		KERROR("Failed to create layer for view");
	}

	// Setting window properties
	[ps->handle setLevel:NSNormalWindowLevel];
	[ps->handle setContentView:ps->view];
	[ps->handle makeFirstResponder:ps->view];

	if (config->title) {
		window->title = string_duplicate(config->title);
	} else {
		window->title = string_duplicate("Kohi Game Engine Window");
	}
	[ps->handle setTitle:@(window->title)];
	[ps->handle setDelegate:ps->wnd_delegate];
	[ps->handle setAcceptsMouseMovedEvents:YES];
	[ps->handle setRestorable:NO];

	if (show_immediately) {
		platform_window_show(window);
	}

	//[ps->handle makeKeyAndOrderFront:nil];

	// Handle content scaling for various fidelity displays (i.e. Retina)
	ps->layer.bounds = ps->view.bounds;
	// It's important to set the drawableSize to the actual backing pixels. When rendering
	// full-screen, we can skip the macOS compositor if the size matches the display size.
	ps->layer.drawableSize = [ps->view convertSizeToBacking:ps->view.bounds.size];

	// In its implementation of vkGetPhysicalDeviceSurfaceCapabilitiesKHR, MoltenVK takes into
	// consideration both the size (in points) of the bounds, and the contentsScale of the
	// CAMetalLayer from which the Vulkan surface was created.
	// See also https://github.com/KhronosGroup/MoltenVK/issues/428
	ps->layer.contentsScale = ps->view.window.backingScaleFactor;
	KDEBUG("contentScale: %f", ps->layer.contentsScale);
	// Save off the device pixel ratio.
	ps->device_pixel_ratio = ps->layer.contentsScale;

	[ps->view setLayer:ps->layer];

	// This is set to NO by default, but is also important to ensure we can bypass the compositor
	// in full-screen mode
	// See "Direct to Display" http://metalkit.org/2017/06/30/introducing-metal-2.html.
	ps->layer.opaque = YES;

	// Fire off a resize event to make sure the framebuffer is the right size.
	// Again, this should be the actual backing framebuffer size (taking into account pixel density).
	// event_context context;
	// context.data.u16[0] = (u16)state_ptr->handle.layer.drawableSize.width;
	// context.data.u16[1] = (u16)state_ptr->handle.layer.drawableSize.height;
	// event_fire(EVENT_CODE_RESIZED, 0, context);

	////

	window->width = (u16)ps->layer.drawableSize.width;
	window->height = (u16)ps->layer.drawableSize.height;

	// Register the window internally.
	darray_push(state_ptr->windows, &window);

	/*if (show_immediately) {
		platform_window_show(window);
	}*/

	return true;
}

void platform_window_destroy (struct kwindow *window) {
	if (window) {
		u32 len = darray_length(state_ptr->windows);
		for (u32 i = 0; i < len; ++i) {
			kwindow *w = state_ptr->windows[i];
			if (w == window) {
				KTRACE("Destroying window...");

				kwindow_platform_state *ps = w->platform_state;

				[ps->handle orderOut:nil];

				[ps->handle setDelegate:nil];
				[ps->wnd_delegate release];

				[ps->view release];
				ps->view = nil;

				[ps->handle close];
				ps->handle = nil;

				state_ptr->windows[i] = 0;
				return;
			}
		}
		KERROR("Destroying a window that was somehow not registered with the platform layer.");

		kwindow_platform_state *ps = window->platform_state;
		[ps->handle orderOut:nil];

		[ps->handle setDelegate:nil];
		[ps->wnd_delegate release];

		[ps->view release];
		ps->view = nil;

		[ps->handle close];
		ps->handle = nil;
	}
}

b8 platform_window_show (struct kwindow *window) {
	if (!window) {
		return false;
	}

	kwindow_platform_state *ps = window->platform_state;
	//[ps->handle show];

	// TODO: is this it?
	[ps->handle makeKeyAndOrderFront:nil];
	[ps->handle setIsVisible:(BOOL)YES];

	return true;
}

b8 platform_window_hide (struct kwindow *window) {
	if (!window) {
		return false;
	}

	kwindow_platform_state *ps = window->platform_state;
	[ps->handle setIsVisible:(BOOL)NO];

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

	if (window->title) {
		string_free(window->title);
	}
	window->title = string_duplicate(title);
	kwindow_platform_state *ps = window->platform_state;
	[ps->handle setTitle:@(window->title)];
	return true;
}

b8 platform_pump_messages (void) {
	if (state_ptr) {
		@autoreleasepool {

			NSEvent *event;

			for (;;) {
				event = [NSApp
					nextEventMatchingMask:NSEventMaskAny
								untilDate:[NSDate distantPast]
								   inMode:NSDefaultRunLoopMode
								  dequeue:YES];

				if (!event)
					break;

				[NSApp sendEvent:event];
			}

		} // autoreleasepool

		platform_update_watches();

		// Check every so often for new controllers.
		static u32 next_poll = 30;
		next_poll--;
		if (next_poll == 0) {
			detect_gamepad_devices();
			next_poll = 30;
		}

		// Also process gamepad events.
		process_gamepad_events();

		return !state_ptr->quit_flagged;
	}
	return true;
}

void *platform_allocate (u64 size, b8 aligned) {
	void *p = mmap(
		0,
		size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANON,
		-1,
		0);

	if (p == MAP_FAILED) {
		return 0;
	}

	return p;
}

void platform_free (const void *block, b8 aligned, u64 size) {
	if (block) {
		munmap((void *)block, size);
	}
}

void platform_console_write (struct platform_state *platform, log_level level, const char *message) {
	// FATAL,ERROR,WARN,INFO,DEBUG,TRACE
	const char *colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
	printf("\033[%sm%s\033[0m", colour_strings[level], message);
}

/*void platform_console_write_error(const char* message, u8 colour) {
	// FATAL,ERROR,WARN,INFO,DEBUG,TRACE
	const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
	printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}*/

f64 platform_get_absolute_time (void) {
	mach_timebase_info_data_t clock_timebase;
	mach_timebase_info(&clock_timebase);

	u64 mach_absolute = mach_absolute_time();

	u64 nanos = (f64)(mach_absolute * (u64)clock_timebase.numer) / (f64)clock_timebase.denom;
	return nanos / 1.0e9; // Convert to seconds
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
	return [[NSProcessInfo processInfo] processorCount];
}

void platform_get_handle_info (u64 *out_size, void *memory) {

	*out_size = sizeof(macos_handle_info);
	if (!memory) {
		return;
	}

	kcopy_memory(memory, &state_ptr->handle, *out_size);
}

f32 platform_device_pixel_ratio (const struct kwindow *window) {
	return window->platform_state->device_pixel_ratio;
}

const char *platform_dynamic_library_extension (void) {
	return ".dylib";
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
	u32 flags = COPYFILE_ALL;
	if (!overwrite_if_exists) {
		flags |= overwrite_if_exists;
	}
	int result = copyfile(source, dest, 0, flags);
	if (result != 0) {
		if (result == ENOENT) {
			return PLATFORM_ERROR_FILE_NOT_FOUND;
		} else if (result == EEXIST) {
			// file exists and overwrite is off
			return PLATFORM_ERROR_FILE_EXISTS;
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
		state_ptr->watches = darray_create(macos_file_watch);
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
		macos_file_watch *w = &state_ptr->watches[i];
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
	macos_file_watch w = {0};
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

	macos_file_watch *w = &state_ptr->watches[watch_id];
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
		macos_file_watch *f = &state_ptr->watches[i];
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
unix_time_from_stat (const struct stat *s) {
	return (kunix_time_ns)s->st_mtimespec.tv_sec * 1000000000ULL +
		   (kunix_time_ns)s->st_mtimespec.tv_nsec;
}

kunix_time_ns platform_get_file_mtime (const char *path) {
	struct stat s;
	if (stat(path, &s) != 0) {
		return 0;
	}

	return unix_time_from_stat(&s);
}

static void sysctl_str (const char *key, char *out, size_t cap) {
	size_t len = 0;
	if (sysctlbyname(key, NULL, &len, KNULL, 0) != 0) {
		return;
	}
	if (len >= cap) {
		len = cap - 1;
	}

	sysctlbyname(key, out, &len, KNULL, 0);
	out[len] = 0;
}

static void sysctl_u64 (const char *key, u64 *out) {
	size_t sz = sizeof(*out);
	sysctlbyname(key, out, &sz, KNULL, 0);
}

static const char *macos_codename_from_version (i32 major, i32 minor) {
	// macOS 11+
	if (major >= 11) {
		switch (major) {
		case 11:
			return "Big Sur";
		case 12:
			return "Monterey";
		case 13:
			return "Ventura";
		case 14:
			return "Sonoma";
		case 15:
			return "Sequoia";
		case 26:
			return "Tahoe"; // macOS 26 (WWDC 2025)
		default:
			return "Unknown";
		}
	}

	// macOS 10.x
	if (major == 10) {
		switch (minor) {
		case 15:
			return "Catalina";
		case 14:
			return "Mojave";
		case 13:
			return "High Sierra";
		case 12:
			return "Sierra";
		case 11:
			return "El Capitan";
		case 10:
			return "Yosemite";
		case 9:
			return "Mavericks";
		default:
			return "Unknown";
		}
	}

	return "Unknown";
}

static int cfstring_to_cstr (CFStringRef str, char *out, size_t out_size) {
	if (!str)
		return 0;
	return CFStringGetCString(str, out, out_size, kCFStringEncodingUTF8);
}

b8 is_volume_relevant (const char *mount_point, u64 total_bytes, kdrive_type type) {
	// Ignore tiny volumes
	if (total_bytes < 1024 * 1024)
		return false;

	// Ignore raw device mounts
	if (strcmp(mount_point, "/dev") == 0)
		return false;

	// Ignore known system helper volumes
	if (strncmp(mount_point, "/System/Volumes/Preboot", 23) == 0)
		return false;
	if (strncmp(mount_point, "/System/Volumes/VM", 17) == 0)
		return false;
	if (strncmp(mount_point, "/System/Volumes/Update", 22) == 0)
		return false;
	if (strncmp(mount_point, "/System/Volumes/xarts", 22) == 0)
		return false;
	if (strncmp(mount_point, "/System/Volumes/iSCPreboot", 26) == 0)
		return false;
	if (strncmp(mount_point, "/System/Volumes/Hardware", 25) == 0)
		return false;

	// Keep everything else
	return true;
}

static void macos_query_storage (ksystem_info *out, u32 max_devices) {
	struct statfs *mounts = NULL;
	int count = getfsstat(NULL, 0, MNT_NOWAIT);
	if (count <= 0)
		return;

	mounts = (struct statfs *)malloc(sizeof(struct statfs) * count);
	if (!mounts)
		return;

	count = getfsstat(mounts, sizeof(struct statfs) * count, MNT_NOWAIT);
	if (count <= 0) {
		free(mounts);
		return;
	}

	DASessionRef session = DASessionCreate(kCFAllocatorDefault);
	if (!session) {
		free(mounts);
		return;
	}

	out->storage_count = 0;

	for (int i = 0; i < count && out->storage_count < max_devices; i++) {
		struct statfs *s = &mounts[i];
		kstorage_info *info = &out->storage[out->storage_count];

		// Device (disk0s2)
		const char *dev = strrchr(s->f_mntfromname, '/');
		snprintf(info->name, sizeof(info->name), "%s", dev ? dev + 1 : s->f_mntfromname);

		// Mount point
		snprintf(info->mount_point, sizeof(info->mount_point), "%s", s->f_mntonname);

		// Size
		info->total_bytes = (u64)s->f_blocks * s->f_bsize;
		info->free_bytes = (u64)s->f_bavail * s->f_bsize;

		// Skip irrelevant volumes (i.e. helpers, etc.)
		if (!is_volume_relevant(info->mount_point, info->total_bytes, info->type)) {
			continue;
		}

		// Default
		info->type = KDRIVE_TYPE_UNKNOWN;

		// RAM disk
		if (!strcmp(s->f_fstypename, "tmpfs") ||
			!strcmp(s->f_fstypename, "ramfs")) {
			info->type = KDRIVE_TYPE_RAMDISK;
			out->storage_count++;
			continue;
		}

		// Network FS
		if (!(s->f_flags & MNT_LOCAL)) {
			info->type = KDRIVE_TYPE_REMOTE;
			out->storage_count++;
			continue;
		}

		// Disk Arbitration refinement
		DADiskRef disk = DADiskCreateFromBSDName(
			kCFAllocatorDefault,
			session,
			info->name);

		if (disk) {
			CFDictionaryRef desc = DADiskCopyDescription(disk);
			if (desc) {
				// Optical
				CFStringRef media = CFDictionaryGetValue(desc, kDADiskDescriptionMediaKindKey);
				if (media &&
					CFStringCompare(media, CFSTR("CD-ROM"), 0) == kCFCompareEqualTo) {
					info->type = KDRIVE_TYPE_CDROM;
				}

				// Removable
				CFBooleanRef removable = CFDictionaryGetValue(
					desc, kDADiskDescriptionMediaRemovableKey);
				if (removable && CFBooleanGetValue(removable)) {
					info->type = KDRIVE_TYPE_REMOVABLE;
				}

				// Internal fixed
				if (info->type == KDRIVE_TYPE_UNKNOWN) {
					CFBooleanRef internal = CFDictionaryGetValue(
						desc, kDADiskDescriptionDeviceInternalKey);
					if (internal && CFBooleanGetValue(internal)) {
						info->type = KDRIVE_TYPE_FIXED;
					}
				}

				CFRelease(desc);
			}
			CFRelease(disk);
		}

		// Fallback
		if (info->type == KDRIVE_TYPE_UNKNOWN)
			info->type = KDRIVE_TYPE_FIXED;

		out->storage_count++;
	}

	CFRelease(session);
	free(mounts);
}

b8 platform_system_info_collect (ksystem_info *out_info) {
	kzero_memory(out_info, sizeof(ksystem_info));

	// Intel macs
	sysctl_str("machdep.cpu.brand_string", out_info->cpu_name, sizeof(out_info->cpu_name));
	if (!out_info->cpu_name[0]) {
		// Applle silicon
		sysctl_str("hw.model", out_info->cpu_name, sizeof(out_info->cpu_name));
	}
	sysctlbyname("hw.logicalcpu", &out_info->logical_cores, &(size_t){sizeof(u32)}, KNULL, 0);
	sysctlbyname("hw.physicalcpu", &out_info->physical_cores, &(size_t){sizeof(u32)}, KNULL, 0);

	u64 cpu_freq;
	sysctl_u64("hw.cpufrequency", &cpu_freq);
	out_info->cpu_mhz = cpu_freq / 1000000;

	sysctl_u64("hw.memsize", &out_info->ram_total_bytes);

	// NOTE: this exists on Intel macs, missing on Apple silicon.
	u64 memfreq;
	sysctl_u64("hw.memfrequency", &memfreq);
	out_info->ram_speed_mhz = (u32)(memfreq / 1000000);

	char os_version[64];
	sysctl_str("kern.osproductversion", os_version, sizeof(os_version));
	sysctl_str("kern.version", out_info->kernel_version, sizeof(out_info->kernel_version));
	sysctl_str("kern.osversion", out_info->os_build, sizeof(out_info->os_build));

	i32 major = 0, minor = 0, patch = 0;
	sscanf(os_version, "%d.%d.%d", &major, &minor, &patch);
	const char *codename = macos_codename_from_version(major, minor);
	char *ver_str = string_format("%s %d.%d.%d", codename, major, minor, patch);
	strcpy(out_info->os_version, ver_str);
	string_free(ver_str);

	strcpy(out_info->os_name, "macOS");

	// RAM available
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	integer_t vm_info[HOST_VM_INFO64_COUNT];
	kern_return_t kr = host_statistics64(mach_host_self(), HOST_VM_INFO64, vm_info, &count);

	if (kr == KERN_SUCCESS) {
		vm_statistics64_data_t *vm = (vm_statistics64_data_t *)vm_info;
		u64 page_size;
		size_t sz = sizeof(page_size);
		if (sysctlbyname("hw.pagesize", &page_size, &sz, KNULL, 0) == 0) {
			out_info->ram_available_bytes = (vm->free_count + vm->inactive_count) * page_size;
		}
	}

	// Storage
	macos_query_storage(out_info, KMAX_STORAGE_DEVICES);

#	if defined(__x86_64__)
	strcpy(out_info->cpu_arch, "x86_64");
	detect_x86_features(&out_info->features);
#	elif defined(__aarch64___)
	strcpy(out_info->cpu_arch, "arm64");
	detect_arm_features(&out_info->features);
#	endif

	FLAG_SET(out_info->flags, KSYSTEM_INFO_FLAGS_IS_64_BIT_BIT, true);
	return true;
}

void platform_request_clipboard_content (kwindow *window) {
	NSPasteboard *pb = [NSPasteboard generalPasteboard];
	NSString *str = [pb stringForType:NSPasteboardTypeString];
	if (!str) {
		return;
	}

	const char *utf8 = [str UTF8String];
	kclipboard_context ctx = {
		.size = string_length(utf8) + 1,
		.content = utf8,
		.content_type = KCLIPBOARD_CONTENT_TYPE_STRING,
		.requesting_window = window};
	if (state_ptr->on_paste) {
		state_ptr->on_paste(ctx);
	}
}

void platform_clipboard_content_set (kwindow *window, kclipboard_content_type type, u32 size, void *content) {
	if (!content) {
		return;
	}

	PasteboardRef pb;
	if (PasteboardCreate(kPasteboardClipboard, &pb) != noErr)
		return;

	PasteboardClear(pb);

	CFDataRef data = CFDataCreate(
		kCFAllocatorDefault,
		(const UInt8 *)content,
		(CFIndex)strlen(content));
	if (!data) {
		CFRelease(pb);
		return;
	}

	PasteboardPutItemFlavor(
		pb,
		(PasteboardItemID)1,
		kUTTypeUTF8PlainText,
		data,
		0);

	CFRelease(data);
	CFRelease(pb);
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
	platform_open_file_dialog_result ofd_result = {0};
	ofd_result.result = PLATFORM_OFD_OP_RESULT_INTERNAL_FAILURE;

	@autoreleasepool {
		NSOpenPanel *panel = [NSOpenPanel openPanel];

		if (!options.starting_dir) {
			options.starting_dir = ".";
		}
		NSURL *startUrl = [NSURL fileURLWithPath:@(options.starting_dir)];

		[panel setDirectoryURL:startUrl];

		[panel setCanChooseFiles:YES];
		[panel setCanChooseDirectories:NO];
		[panel setAllowsMultipleSelection:options.allow_multiselect];
		[panel setMessage:@(options.title)];

		// Filters, if included. If not, a default of *.*/all files is assumed.
		if (options.filter) {
			u8 type_count = 0;
			open_file_filter_type_data *types = parse_open_file_filters(options.filter, &type_count);
			if (type_count > 0) {
				NSMutableArray<UTType *> *nstypes = [[NSMutableArray alloc] init];

				// Each type
				for (u8 i = 0; i < type_count; ++i) {
					open_file_filter_type_data *type = &types[i];

					for (u32 j = 0; j < type->extension_count; ++j) {
						const char *ex = type->extensions[j];
						UTType *oType = [UTType typeWithFilenameExtension:@(ex + 2)]; // NOTE: ignoring the leading '*.'
						[nstypes addObject:oType];
					}
				}

				panel.allowedContentTypes = nstypes;
			}
		}

		NSInteger result = [panel runModal];

		if (result == NSModalResponseOK) {
			NSArray<NSURL *> *urls = [panel URLs];
			ofd_result.file_count = (u8)urls.count;
			ofd_result.file_paths = KALLOC_TYPE_CARRAY(const char *, ofd_result.file_count);

			u32 i = 0;
			for (NSURL *url in urls) {
				const char *path = [[url path] UTF8String];
				ofd_result.file_paths[i] = string_duplicate(path);
				++i;
			}
			ofd_result.result = PLATFORM_OFD_OP_RESULT_SUCCESS;
		} else {
			ofd_result.result = PLATFORM_OFD_OP_RESULT_USER_CANCELLED;
		}

		return ofd_result;
	}
}

// NOTE: gamepad implementation
static void hat_value_to_xy (CFIndex value, CFIndex range, i32 *out_x, i32 *out_y) {
	if (value == range) {
		*out_x = *out_y = 0;
	} else {
		if (value > 0 && value < range / 2) {
			*out_x = 1;
		} else if (value > range / 2) {
			*out_x = -1;
		} else {
			*out_x = 0;
		}

		if (value > range / 4 * 3 || value < range / 4) {
			*out_y = -1;
		} else if (value > range / 4 && value < range / 4 * 3) {
			*out_y = 1;
		} else {
			*out_y = 0;
		}
	}
}

static void queue_axis_event (platform_gamepad_device *device, f32 timestamp, gamepad_axis axis, f32 value, f32 last_value) {
	platform_gamepad_event queued_event = {
		.device_id = device->device_id,
		.type = PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED,
		.timestamp = timestamp,
		.axis = axis,
		.value = value,
		.last_value = last_value};

	darray_push(state_ptr->gamepad_state->input_event_queue, &queued_event);
}

static void queue_button_event (platform_gamepad_device *device, f32 timestamp, gamepad_button button, b8 pressed) {
	platform_gamepad_event queued_event = {
		.device_id = device->device_id,
		.type = pressed ? PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED : PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED,
		.timestamp = timestamp,
		.button = button};

	darray_push(state_ptr->gamepad_state->input_event_queue, &queued_event);
}

static void on_device_value_changed (void *context, IOReturn result, void *sender, IOHIDValueRef value) {
	static mach_timebase_info_data_t timebase_info;
	if (timebase_info.denom == 0) {
		mach_timebase_info(&timebase_info);
	}

	platform_gamepad_device *device = context;
	IOHIDElementRef element = IOHIDValueGetElement(value);
	IOHIDElementCookie cookie = IOHIDElementGetCookie(element);

	f64 timestamp_64 = IOHIDValueGetTimeStamp(value) * timebase_info.numer / timebase_info.denom * 0.000000001;
	f32 timestamp = (f32)timestamp_64;

	// Handle axis updates
	for (u32 i = 0; i < device->axis_count; ++i) {
		hid_gamepad_axis *ae = &device->axis_elements[i];
		gamepad_axis axis = ae->axis;
		if (!ae->is_hat_switch_second_axis && ae->cookie == cookie) {
			if (axis == GAMEPAD_AXIS_IGNORE) {
				// Boot, treat as processed.
				continue;
			}
			if (IOHIDValueGetLength(value) > 4) {
				// NOTE: PS3 controller gives super high value here.
				continue;
			}
			CFIndex int_val = IOHIDValueGetIntegerValue(value);

			if (ae->is_hat_switch) {

				hid_gamepad_axis *ae2 = &device->axis_elements[i + 1];
				gamepad_axis axis2 = ae2->axis;

				// Axis represents a d-pad and needs to be remapped.
				/* f32 float_val = (int_val - ae->logical_min) / (f32)(ae->logical_max - ae->logical_min) * 2.0f - 1.0f; */

				// Fix for Saitek X52
				if (!ae->has_null_state) {
					if (int_val < ae->logical_min) {
						int_val = ae->logical_max - ae->logical_min + 1;
					} else {
						int_val--;
					}
				}

				i32 x, y;
				hat_value_to_xy(int_val, ae->logical_max - ae->logical_min + 1, &x, &y);

				if (x != device->axis_states[i]) {
					if (axis == GAMEPAD_AXIS_DPAD_LR) {
						f32 float_val = x;
						f32 last_value = device->axis_states[i];
						b8 pressed = float_val != 0;
						f32 val = pressed ? float_val : last_value;
						gamepad_button mapped_button = (val < 0) ? GAMEPAD_BUTTON_DPAD_LEFT : GAMEPAD_BUTTON_DPAD_RIGHT;

						queue_button_event(device, timestamp, mapped_button, pressed);
						FLAG_SET(device->button_states, mapped_button, pressed);
					} else {
						queue_axis_event(device, timestamp, ae->axis, x, device->axis_states[i]);
					}
					device->axis_states[i] = x;
				}
				if (y != device->axis_states[i + 1]) {
					if (axis2 == GAMEPAD_AXIS_DPAD_UD) {
						f32 float_val = y;
						f32 last_value = device->axis_states[i + 1];
						b8 pressed = float_val != 0;
						f32 val = pressed ? float_val : last_value;
						gamepad_button mapped_button = (val < 0) ? GAMEPAD_BUTTON_DPAD_UP : GAMEPAD_BUTTON_DPAD_DOWN;

						queue_button_event(device, timestamp, mapped_button, pressed);
						FLAG_SET(device->button_states, mapped_button, pressed);
					} else {
						queue_axis_event(device, timestamp, ae2->axis, y, device->axis_states[i + 1]);
					}
					device->axis_states[i + 1] = y;
				}
			} else {
				if (int_val < ae->logical_min) {
					ae->logical_min = int_val;
				}
				if (int_val > ae->logical_max) {
					ae->logical_max = int_val;
				}
				f32 float_val = (int_val - ae->logical_min) / (f32)(ae->logical_max - ae->logical_min) * 2.0f - 1.0f;
				f32 last_value = device->axis_states[i];

				if (axis == GAMEPAD_AXIS_DPAD_LR || axis == GAMEPAD_AXIS_DPAD_UD) {
					// Axis represents a d-pad and needs to be remapped.
					b8 pressed = float_val != 0;
					f32 val = pressed ? float_val : last_value;
					gamepad_button mapped_button;
					if (val < 0) {
						mapped_button = (axis == GAMEPAD_AXIS_DPAD_UD) ? GAMEPAD_BUTTON_DPAD_UP : GAMEPAD_BUTTON_DPAD_LEFT;
					} else {
						mapped_button = (axis == GAMEPAD_AXIS_DPAD_UD) ? GAMEPAD_BUTTON_DPAD_DOWN : GAMEPAD_BUTTON_DPAD_RIGHT;
					}

					queue_button_event(device, timestamp, mapped_button, pressed);
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
						float_val = kabs(float_val);
						// last_value = kabs(last_value);
					} else if (axis == GAMEPAD_AXIS_LZ || axis == GAMEPAD_AXIS_RZ) {
						// If axis is not split, convert z axes to a range of [0-1].
						float_val = (float_val + 1.0f) * 0.5f;
						// last_value = (last_value + 1.0f) * 0.5f;
					}

					queue_axis_event(device, timestamp, ae->axis, float_val, last_value);
					device->axis_states[i] = float_val;
				}
			}

			return;
		}
	}

	// Handle button updates.
	for (u32 i = 0; i < device->button_count; ++i) {
		hid_gamepad_button *hbtn = &device->button_elements[i];
		if (hbtn->cookie == cookie) {
			b8 pressed = IOHIDValueGetIntegerValue(value);

			queue_button_event(device, timestamp, hbtn->button, pressed);

			FLAG_SET(device->button_states, hbtn->button, pressed);
			return;
		}
	}
}

static i32 IOHIDDeviceGetIntProperty (IOHIDDeviceRef device_ref, CFStringRef key) {
	CFTypeRef type_ref = IOHIDDeviceGetProperty(device_ref, key);
	if (type_ref == KNULL || CFGetTypeID(type_ref) != CFNumberGetTypeID()) {
		return 0;
	}

	i32 value;
	CFNumberGetValue((CFNumberRef)type_ref, kCFNumberSInt32Type, &value);

	return value;
}

static i32 IOHIDDeviceGetVendorID (IOHIDDeviceRef device_ref) {
	return IOHIDDeviceGetIntProperty(device_ref, CFSTR(kIOHIDVendorIDKey));
}

static i32 IOHIDDeviceGetProductID (IOHIDDeviceRef device_ref) {
	return IOHIDDeviceGetIntProperty(device_ref, CFSTR(kIOHIDProductIDKey));
}

static platform_gamepad_connection_type IOHIDDeviceGetConnectionType (IOHIDDeviceRef device_ref) {
	CFStringRef transport_type = IOHIDDeviceGetProperty(device_ref, CFSTR(kIOHIDTransportKey));
	NSString *transport_str = (NSString *)transport_type ?: @"Unknown Transport";
	platform_gamepad_connection_type connection_type = PLATFORM_GAMEPAD_CONNECTION_TYPE_UNKNOWN;
	if ([transport_str isEqualToString:@"USB"]) {
		connection_type = PLATFORM_GAMEPAD_CONNECTION_TYPE_WIRED_USB;
	} else if ([transport_str localizedCaseInsensitiveContainsString:@"Bluetooth"]) {
		// Covers both "Bluetooth" and "BluetoothLowEnergy"
		connection_type = PLATFORM_GAMEPAD_CONNECTION_TYPE_WIRELESS_BT;
	}

	return connection_type;
}

static void on_device_matched (void *context, IOReturn result, void *sender, IOHIDDeviceRef device_ref) {
	platform_state *state = state_ptr;
	platform_gamepad_device *device = KALLOC_TYPE(platform_gamepad_device, MEMORY_TAG_PLATFORM);
	device->device_id = state->gamepad_state->next_device_id++;
	device->vendor_id = IOHIDDeviceGetVendorID(device_ref);
	device->product_id = IOHIDDeviceGetProductID(device_ref);
	device->connection_type = IOHIDDeviceGetConnectionType(device_ref);

	device->axis_count = 0;
	device->button_count = 0;
	device->device_ref = device_ref;
	device->axis_elements = darray_reserve(hid_gamepad_axis, 32);
	device->button_elements = darray_reserve(hid_gamepad_button, 32);

	darray_push(state->gamepad_state->devices, &device);

	CFStringRef cf_product_name = IOHIDDeviceGetProperty(device_ref, CFSTR(kIOHIDProductKey));
	if (cf_product_name == KNULL || CFGetTypeID(cf_product_name) != CFStringGetTypeID()) {
		device->description = string_duplicate("[Unknown]");
	} else {
		CFIndex length;
		CFStringGetBytes(cf_product_name, CFRangeMake(0, CFStringGetLength(cf_product_name)), kCFStringEncodingUTF8, '?', false, KNULL, 100, &length);
		device->description = kallocate(length, MEMORY_TAG_STRING);
		CFStringGetBytes(cf_product_name, CFRangeMake(0, CFStringGetLength(cf_product_name)), kCFStringEncodingUTF8, '?', false, (u8 *)device->description, length + 1, KNULL);
		((char *)device->description)[length] = 0;
	}

	CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device_ref, KNULL, kIOHIDOptionsTypeNone);
	for (CFIndex i = 0; i < CFArrayGetCount(elements); ++i) {
		IOHIDElementRef element = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
		IOHIDElementType type = IOHIDElementGetType(element);

		if (type == kIOHIDElementTypeInput_Misc || type == kIOHIDElementTypeInput_Axis) {
			hid_gamepad_axis e = {
				.cookie = IOHIDElementGetCookie(element),
				.axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count, device->connection_type),
				.logical_min = IOHIDElementGetLogicalMin(element),
				.logical_max = IOHIDElementGetLogicalMax(element),
				.has_null_state = !!IOHIDElementHasNullState(element),
				.is_hat_switch = IOHIDElementGetUsage(element) == kHIDUsage_GD_Hatswitch,
				.is_hat_switch_second_axis = false};

			u32 len = darray_length(device->axis_elements);
			darray_push(device->axis_elements, &e);
			device->axis_count++;
			hid_gamepad_axis *axis = &device->axis_elements[len];

			if (axis->is_hat_switch) {
				hid_gamepad_axis e2 = {
					.axis = gamepad_get_axis(device->vendor_id, device->product_id, device->axis_count, device->connection_type),
					.is_hat_switch_second_axis = true};
				darray_push(device->axis_elements, &e2);
				device->axis_count++;
			}
		} else if (type == kIOHIDElementTypeInput_Button) {
			gamepad_button button = gamepad_get_button(device->vendor_id, device->product_id, device->button_count, device->connection_type);
			hid_gamepad_button b = {
				.button = button,
				.cookie = IOHIDElementGetCookie(element)};
			darray_push(device->button_elements, &b);
			device->button_count++;
		}
	}
	CFRelease(elements);

	IOHIDDeviceRegisterInputValueCallback(device_ref, on_device_value_changed, device);

	platform_gamepad_event queued_event = {
		.device_id = device->device_id,
		.type = PLATFORM_GAMEPAD_EVENT_TYPE_DEVICE_ATTACHED};
	darray_push(state->gamepad_state->device_event_queue, &queued_event);
}

static void cleanup_gamepad_device (platform_gamepad_device *device) {
	IOHIDDeviceRegisterInputValueCallback(device->device_ref, KNULL, KNULL);

	u32 input_len = darray_length(state_ptr->gamepad_state->input_event_queue);
	for (u32 i = 0; i < input_len; ++i) {
		if (state_ptr->gamepad_state->input_event_queue[i].device_id == device->device_id) {
			darray_pop_at(state_ptr->gamepad_state->input_event_queue, i, KNULL);
		}
	}

	u32 device_len = darray_length(state_ptr->gamepad_state->device_event_queue);
	for (u32 i = 0; i < device_len; ++i) {
		if (state_ptr->gamepad_state->device_event_queue[i].device_id == device->device_id) {
			darray_pop_at(state_ptr->gamepad_state->device_event_queue, i, KNULL);
		}
	}

	darray_destroy(device->axis_elements);
	darray_destroy(device->button_elements);

	string_free(device->description);

	kfree(device);
}

static void on_device_removed (void *context, IOReturn result, void *sender, IOHIDDeviceRef device_ref) {
	u32 len = darray_length(state_ptr->gamepad_state->devices);
	for (u32 i = 0; i < len; ++i) {
		if (state_ptr->gamepad_state->devices[i]->device_ref == device_ref) {
			if (state_ptr->gamepad_callbacks.on_detached) {
				state_ptr->gamepad_callbacks.on_detached(state_ptr->gamepad_state->devices[i]->device_id);
			}

			cleanup_gamepad_device(state_ptr->gamepad_state->devices[i]);
			darray_pop_at(state_ptr->gamepad_state->devices, i, KNULL);
			return;
		}
	}
}

void platform_gamepads_initialize (void) {
	platform_state *state = state_ptr;

	state->gamepad_state = KALLOC_TYPE(platform_gamepad_state, MEMORY_TAG_PLATFORM);

	state->gamepad_state->devices = darray_reserve(platform_gamepad_device *, 4);
	state->gamepad_state->device_event_queue = darray_reserve(platform_gamepad_event, 64);
	state->gamepad_state->input_event_queue = darray_reserve(platform_gamepad_event, 64);

	if (state->gamepad_state->hid_manager == KNULL) {
		state->gamepad_state->hid_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);

		CFStringRef keys[2] = {
			CFSTR(kIOHIDDeviceUsagePageKey),
			CFSTR(kIOHIDDeviceUsageKey)};

		CFNumberRef values[2];
		i32 value = kHIDPage_GenericDesktop;
		values[0] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		value = kHIDUsage_GD_Joystick;
		values[1] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		CFDictionaryRef dictionaries[3];
		dictionaries[0] = CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFRelease(values[0]);
		CFRelease(values[1]);

		value = kHIDPage_GenericDesktop;
		values[0] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		value = kHIDUsage_GD_GamePad;
		values[1] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		dictionaries[1] = CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFRelease(values[0]);
		CFRelease(values[1]);

		value = kHIDPage_GenericDesktop;
		values[0] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		value = kHIDUsage_GD_MultiAxisController;
		values[1] = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
		dictionaries[2] = CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFRelease(values[0]);
		CFRelease(values[1]);

		CFArrayRef array = CFArrayCreate(kCFAllocatorDefault, (const void **)dictionaries, 3, &kCFTypeArrayCallBacks);
		CFRelease(dictionaries[0]);
		CFRelease(dictionaries[1]);
		CFRelease(dictionaries[2]);
		IOHIDManagerSetDeviceMatchingMultiple(state_ptr->gamepad_state->hid_manager, array);
		CFRelease(array);

		IOHIDManagerRegisterDeviceMatchingCallback(state_ptr->gamepad_state->hid_manager, on_device_matched, KNULL);
		IOHIDManagerRegisterDeviceRemovalCallback(state_ptr->gamepad_state->hid_manager, on_device_removed, KNULL);

		IOHIDManagerOpen(state_ptr->gamepad_state->hid_manager, kIOHIDOptionsTypeNone);

		IOHIDManagerScheduleWithRunLoop(state_ptr->gamepad_state->hid_manager, CFRunLoopGetCurrent(), GAMEPAD_RUN_LOOP_MODE);
		CFRunLoopRunInMode(GAMEPAD_RUN_LOOP_MODE, 0, true);
	}
}

u32 platform_gamepad_device_count (void) {
	return darray_length(state_ptr->gamepad_state->devices);
}

u32 platform_gamepad_deviceid_at_index (u32 device_index) {
	u32 device_id;
	u32 count = darray_length(state_ptr->gamepad_state->devices);
	if (device_index >= count) {
		device_id = INVALID_ID_U32;
	} else {
		device_id = state_ptr->gamepad_state->devices[device_index]->device_id;
	}
	return device_id;
}

b8 platform_gamepad_get_device_index (u32 device_id, u8 *out_index) {
	u32 device_count = darray_length(state_ptr->gamepad_state->devices);
	for (u32 i = 0; i < device_count; ++i) {
		platform_gamepad_device *device = state_ptr->gamepad_state->devices[i];
		if (device->device_id == device_id) {
			*out_index = (u8)i;
			return true;
		}
	}
	return false;
}

b8 platform_gamepad_get_device_info (u32 device_id, i32 *out_vendor_id, i32 *out_product_id, u32 *out_button_count, u32 *out_axis_count) {
	if (!state_ptr->gamepad_state->devices) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

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

const char *platform_gamepad_get_device_desc (u32 device_id) {
	if (!state_ptr->gamepad_state->devices) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return KNULL;
	}

	return string_duplicate(device->description);
}

// index must be [0-3]. Otherwise will be ignored. Returns true if set successfully; otherwise false.
b8 platform_gamepad_set_device_player_index (u32 device_id, u8 player_index) {
	if (player_index < 0 || player_index > 3) {
		return false;
	}

	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	if (device != KNULL) {
		/* TODO: set player index LED via report. Note that the report needs to be appropriate for the device,
		 * as it can break the device if not!
		CFIndex report_id = 0;
		u8 *report_data = KNULL;
		CFIndex report_length = 0;
		IOReturn result = IOHIDDeviceSetReport(device->device_ref, kIOHIDReportTypeOutput, report_id, report_data, report_length);
		if (result != kIOReturnSuccess) {
			KWARN("Failed to set LED player index for gamepad '%s'. Numeric index still set though.", device->description);
		}
		*/
	}

	return true;
}

b8 platform_gamepad_is_button_pressed (u32 device_id, gamepad_button button) {
	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	return FLAG_GET(device->button_states, button);
}

f32 platform_gamepad_get_axis_state (u32 device_id, gamepad_axis axis) {
	platform_gamepad_device *device = get_gamepad_device_by_id(device_id);
	if (!device) {
		return false;
	}

	for (u32 i = 0; i < device->axis_count; ++i) {
		if (device->axis_elements[i].axis == axis) {
			return device->axis_states[i];
		}
	}

	return 0.0f;
}

b8 platform_initialize_input (void) {
	platform_state *state = state_ptr;
	if (!state) {
		return false;
	}
	// TODO: initialize keyboard and mouse devices. Gamepads handled separately.
	KINFO("macOS platform input initialized.");

	return true;
}

static void process_gamepad_event (platform_gamepad_event *event) {
	platform_state *state = state_ptr;
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
		cleanup_gamepad_device(device);
		break;
	case PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_PRESSED:
		if (state->gamepad_callbacks.on_button_pressed) {
			state->gamepad_callbacks.on_button_pressed(event->device_id, event->button, event->timestamp);
		}
		break;
	case PLATFORM_GAMEPAD_EVENT_TYPE_BUTTON_RELEASED:
		if (state->gamepad_callbacks.on_button_released) {
			state->gamepad_callbacks.on_button_released(event->device_id, event->button, event->timestamp);
		}
		break;
	case PLATFORM_GAMEPAD_EVENT_TYPE_AXIS_CHANGED:
		if (state->gamepad_callbacks.on_axis_changed) {
			if (state_ptr->gamepad_callbacks.on_axis_changed) {
				state_ptr->gamepad_callbacks.on_axis_changed(
					device->device_id,
					event->axis,
					event->value,
					event->last_value,
					event->timestamp);
			}
		}
		break;
	}
}

static void detect_gamepad_devices (void) {
	if (state_ptr->gamepad_state->hid_manager == KNULL) {
		return;
	}

	CFRunLoopRunInMode(GAMEPAD_RUN_LOOP_MODE, 0, true);
	u32 len = darray_length(state_ptr->gamepad_state->device_event_queue);
	for (u32 i = 0; i < len; ++i) {
		platform_gamepad_event *event = &state_ptr->gamepad_state->device_event_queue[i];
		process_gamepad_event(event);
	}
	darray_clear(state_ptr->gamepad_state->device_event_queue);
}

static void process_gamepad_events (void) {
	static b8 in_process_events = false;
	if (state_ptr->gamepad_state->hid_manager == KNULL || in_process_events) {
		return;
	}

	in_process_events = true;
	CFRunLoopRunInMode(GAMEPAD_RUN_LOOP_MODE, 0, true);
	u32 len = darray_length(state_ptr->gamepad_state->input_event_queue);
	for (u32 i = 0; i < len; ++i) {
		platform_gamepad_event *event = &state_ptr->gamepad_state->input_event_queue[i];
		process_gamepad_event(event);
	}
	darray_clear(state_ptr->gamepad_state->input_event_queue);
	in_process_events = false;
}

// NOTE: syscall implementation
i64 syscall2 (i64 number, i64 arg0, i64 arg1) {
#	if defined(__aarch64__)
	register i64 x16 __asm__("x16") = number;
	register i64 x0 __asm__("x0") = arg0;
	register i64 x1 __asm__("x1") = arg1;

	__asm__ volatile(
		"svc #0x80"
		: "+r"(x0)
		: "r"(x16),
		  "r"(x1)
		: "memory");

	return x0;
#	else
#		error "Only Apple silicon is supported for macOS at this time."
#	endif
}

i64 syscall3 (i64 number, i64 arg0, i64 arg1, i64 arg2) {
#	if defined(__aarch64__)
	register i64 x16 __asm__("x16") = number;
	register i64 x0 __asm__("x0") = arg0;
	register i64 x1 __asm__("x1") = arg1;
	register i64 x2 __asm__("x2") = arg2;

	__asm__ volatile(
		"svc #0x80"
		: "+r"(x0)
		: "r"(x16),
		  "r"(x1),
		  "r"(x2)
		: "memory");

	return x0;
#	else
#		error "Only Apple silicon is supported for macOS at this time."
#	endif
}

i64 syscall4 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3) {
#	if defined(__aarch64__)
	register i64 x16 __asm__("x16") = number;
	register i64 x0 __asm__("x0") = arg0;
	register i64 x1 __asm__("x1") = arg1;
	register i64 x2 __asm__("x2") = arg2;
	register i64 x3 __asm__("x3") = arg3;

	__asm__ volatile(
		"svc #0x80"
		: "+r"(x0)
		: "r"(x16),
		  "r"(x1),
		  "r"(x2),
		  "r"(x3)
		: "memory");

	return x0;
#	else
#		error "Only Apple silicon is supported for macOS at this time."
#	endif
}

i64 syscall5 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4) {
#	if defined(__aarch64__)
	register i64 x16 __asm__("x16") = number;
	register i64 x0 __asm__("x0") = arg0;
	register i64 x1 __asm__("x1") = arg1;
	register i64 x2 __asm__("x2") = arg2;
	register i64 x3 __asm__("x3") = arg3;
	register i64 x4 __asm__("x4") = arg4;

	__asm__ volatile(
		"svc #0x80"
		: "+r"(x0)
		: "r"(x16),
		  "r"(x1),
		  "r"(x2),
		  "r"(x3),
		  "r"(x4)
		: "memory");

	return x0;
#	else
#		error "Only Apple silicon is supported for macOS at this time."
#	endif
}

i64 syscall6 (i64 number, i64 arg0, i64 arg1, i64 arg2, i64 arg3, i64 arg4, i64 arg5) {
#	if defined(__aarch64__)
	register i64 x16 __asm__("x16") = number;
	register i64 x0 __asm__("x0") = arg0;
	register i64 x1 __asm__("x1") = arg1;
	register i64 x2 __asm__("x2") = arg2;
	register i64 x3 __asm__("x3") = arg3;
	register i64 x4 __asm__("x4") = arg4;
	register i64 x5 __asm__("x5") = arg5;

	__asm__ volatile(
		"svc #0x80"
		: "+r"(x0)
		: "r"(x16),
		  "r"(x1),
		  "r"(x2),
		  "r"(x3),
		  "r"(x4),
		  "r"(x5)
		: "memory");

	return x0;
#	else
#		error "Only Apple silicon is supported for macOS at this time."
#	endif
}

static keys translate_keycode (u32 ns_keycode) {
	// https://boredzo.org/blog/wp-content/uploads/2007/05/IMTx-virtual-keycodes.pdf
	// https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
	switch (ns_keycode) {
	case 0x52:
		return K_KEY_NUMPAD0;
	case 0x53:
		return K_KEY_NUMPAD1;
	case 0x54:
		return K_KEY_NUMPAD2;
	case 0x55:
		return K_KEY_NUMPAD3;
	case 0x56:
		return K_KEY_NUMPAD4;
	case 0x57:
		return K_KEY_NUMPAD5;
	case 0x58:
		return K_KEY_NUMPAD6;
	case 0x59:
		return K_KEY_NUMPAD7;
	case 0x5B:
		return K_KEY_NUMPAD8;
	case 0x5C:
		return K_KEY_NUMPAD9;

	case 0x12:
		return K_KEY_1;
	case 0x13:
		return K_KEY_2;
	case 0x14:
		return K_KEY_3;
	case 0x15:
		return K_KEY_4;
	case 0x17:
		return K_KEY_5;
	case 0x16:
		return K_KEY_6;
	case 0x1A:
		return K_KEY_7;
	case 0x1C:
		return K_KEY_8;
	case 0x19:
		return K_KEY_9;
	case 0x1D:
		return K_KEY_0;

	case 0x00:
		return K_KEY_A;
	case 0x0B:
		return K_KEY_B;
	case 0x08:
		return K_KEY_C;
	case 0x02:
		return K_KEY_D;
	case 0x0E:
		return K_KEY_E;
	case 0x03:
		return K_KEY_F;
	case 0x05:
		return K_KEY_G;
	case 0x04:
		return K_KEY_H;
	case 0x22:
		return K_KEY_I;
	case 0x26:
		return K_KEY_J;
	case 0x28:
		return K_KEY_K;
	case 0x25:
		return K_KEY_L;
	case 0x2E:
		return K_KEY_M;
	case 0x2D:
		return K_KEY_N;
	case 0x1F:
		return K_KEY_O;
	case 0x23:
		return K_KEY_P;
	case 0x0C:
		return K_KEY_Q;
	case 0x0F:
		return K_KEY_R;
	case 0x01:
		return K_KEY_S;
	case 0x11:
		return K_KEY_T;
	case 0x20:
		return K_KEY_U;
	case 0x09:
		return K_KEY_V;
	case 0x0D:
		return K_KEY_W;
	case 0x07:
		return K_KEY_X;
	case 0x10:
		return K_KEY_Y;
	case 0x06:
		return K_KEY_Z;

	case 0x27:
		return K_KEY_APOSTROPHE;
	case 0x2A:
		return K_KEY_BACKSLASH;
	case 0x2B:
		return K_KEY_COMMA;
	case 0x18:
		return K_KEY_EQUAL; // Equal/Plus
	case 0x32:
		return K_KEY_GRAVE;
	case 0x21:
		return K_KEY_LBRACKET;
	case 0x1B:
		return K_KEY_MINUS;
	case 0x2F:
		return K_KEY_PERIOD;
	case 0x1E:
		return K_KEY_RBRACKET;
	case 0x29:
		return K_KEY_SEMICOLON;
	case 0x2C:
		return K_KEY_SLASH;
	case 0x0A:
		return KEYS_MAX_KEYS; // ?

	case 0x33:
		return K_KEY_BACKSPACE;
	case 0x39:
		return K_KEY_CAPITAL;
	case 0x75:
		return K_KEY_DELETE;
	case 0x7D:
		return K_KEY_DOWN;
	case 0x77:
		return K_KEY_END;
	case 0x24:
		return K_KEY_ENTER;
	case 0x35:
		return K_KEY_ESCAPE;
	case 0x7A:
		return K_KEY_F1;
	case 0x78:
		return K_KEY_F2;
	case 0x63:
		return K_KEY_F3;
	case 0x76:
		return K_KEY_F4;
	case 0x60:
		return K_KEY_F5;
	case 0x61:
		return K_KEY_F6;
	case 0x62:
		return K_KEY_F7;
	case 0x64:
		return K_KEY_F8;
	case 0x65:
		return K_KEY_F9;
	case 0x6D:
		return K_KEY_F10;
	case 0x67:
		return K_KEY_F11;
	case 0x6F:
		return K_KEY_F12;
	case 0x69:
		return K_KEY_PRINT;
	case 0x6B:
		return K_KEY_F14;
	case 0x71:
		return K_KEY_F15;
	case 0x6A:
		return K_KEY_F16;
	case 0x40:
		return K_KEY_F17;
	case 0x4F:
		return K_KEY_F18;
	case 0x50:
		return K_KEY_F19;
	case 0x5A:
		return K_KEY_F20;
	case 0x73:
		return K_KEY_HOME;
	case 0x72:
		return K_KEY_INSERT;
	case 0x7B:
		return K_KEY_LEFT;
	case 0x3A:
		return K_KEY_LALT;
	case 0x3B:
		return K_KEY_LCONTROL;
	case 0x38:
		return K_KEY_LSHIFT;
	case 0x37:
		return K_KEY_LSUPER;
	case 0x6E:
		return KEYS_MAX_KEYS; // Menu
	case 0x47:
		return K_KEY_NUMLOCK;
	case 0x79:
		return KEYS_MAX_KEYS; // Page down
	case 0x74:
		return KEYS_MAX_KEYS; // Page up
	case 0x7C:
		return K_KEY_RIGHT;
	case 0x3D:
		return K_KEY_RALT;
	case 0x3E:
		return K_KEY_RCONTROL;
	case 0x3C:
		return K_KEY_RSHIFT;
	case 0x36:
		return K_KEY_RSUPER;
	case 0x31:
		return K_KEY_SPACE;
	case 0x30:
		return K_KEY_TAB;
	case 0x7E:
		return K_KEY_UP;

	case 0x45:
		return K_KEY_ADD;
	case 0x41:
		return K_KEY_DECIMAL;
	case 0x4B:
		return K_KEY_DIVIDE;
	case 0x4C:
		return K_KEY_ENTER;
	case 0x51:
		return K_KEY_NUMPAD_EQUAL;
	case 0x43:
		return K_KEY_MULTIPLY;
	case 0x4E:
		return K_KEY_SUBTRACT;

	default:
		return KEYS_MAX_KEYS;
	}
}

// Bit masks for left and right versions of these keys.
#	define MACOS_LSHIFT_MASK (1 << 1)
#	define MACOS_RSHIFT_MASK (1 << 2)
#	define MACOS_LCTRL_MASK (1 << 0)
#	define MACOS_RCTRL_MASK (1 << 13)
#	define MACOS_LCOMMAND_MASK (1 << 3)
#	define MACOS_RCOMMAND_MASK (1 << 4)
#	define MACOS_LALT_MASK (1 << 5)
#	define MACOS_RALT_MASK (1 << 6)

static void handle_modifier_key (
	u32 ns_keycode,
	u32 ns_key_mask,
	u32 ns_l_keycode,
	u32 ns_r_keycode,
	u32 k_l_keycode,
	u32 k_r_keycode,
	u32 modifier_flags,
	u32 l_mod,
	u32 r_mod,
	u32 l_mask,
	u32 r_mask) {
	if (modifier_flags & ns_key_mask) {
		// Check left variant
		if (modifier_flags & l_mask) {
			if (!(state_ptr->modifier_key_states & l_mod)) {
				state_ptr->modifier_key_states |= l_mod;
				// Report the keypress
				state_ptr->process_key(k_l_keycode, true, false);
			}
		}

		// Check right variant
		if (modifier_flags & r_mask) {
			if (!(state_ptr->modifier_key_states & r_mod)) {
				state_ptr->modifier_key_states |= r_mod;
				// Report the keypress
				state_ptr->process_key(k_r_keycode, true, false);
			}
		}
	} else {
		if (ns_keycode == ns_l_keycode) {
			if (state_ptr->modifier_key_states & l_mod) {
				state_ptr->modifier_key_states &= ~(l_mod);
				// Report the release.
				state_ptr->process_key(k_l_keycode, false, false);
			}
		}

		if (ns_keycode == ns_r_keycode) {
			if (state_ptr->modifier_key_states & r_mod) {
				state_ptr->modifier_key_states &= ~(r_mod);
				// Report the release.
				state_ptr->process_key(k_r_keycode, false, false);
			}
		}
	}
}

static void handle_modifier_keys (u32 ns_keycode, u32 modifier_flags) {
	// Shift
	handle_modifier_key(
		ns_keycode,
		NSEventModifierFlagShift,
		0x38,
		0x3C,
		K_KEY_LSHIFT,
		K_KEY_RSHIFT,
		modifier_flags,
		MACOS_MODIFIER_KEY_LSHIFT,
		MACOS_MODIFIER_KEY_RSHIFT,
		MACOS_LSHIFT_MASK,
		MACOS_RSHIFT_MASK);

	KTRACE("modifier flags keycode: %u", ns_keycode);

	// Ctrl
	handle_modifier_key(
		ns_keycode,
		NSEventModifierFlagControl,
		0x3B,
		0x3E,
		K_KEY_LCONTROL,
		K_KEY_RCONTROL,
		modifier_flags,
		MACOS_MODIFIER_KEY_LCTRL,
		MACOS_MODIFIER_KEY_RCTRL,
		MACOS_LCTRL_MASK,
		MACOS_RCTRL_MASK);

	// Alt/Option
	handle_modifier_key(
		ns_keycode,
		NSEventModifierFlagOption,
		0x3A,
		0x3D,
		K_KEY_LALT,
		K_KEY_RALT,
		modifier_flags,
		MACOS_MODIFIER_KEY_LOPTION,
		MACOS_MODIFIER_KEY_ROPTION,
		MACOS_LALT_MASK,
		MACOS_RALT_MASK);

	// Command/Super
	handle_modifier_key(
		ns_keycode,
		NSEventModifierFlagCommand,
		0x37,
		0x36,
		K_KEY_LSUPER,
		K_KEY_RSUPER,
		modifier_flags,
		MACOS_MODIFIER_KEY_LCOMMAND,
		MACOS_MODIFIER_KEY_RCOMMAND,
		MACOS_LCOMMAND_MASK,
		MACOS_RCOMMAND_MASK);

	// Caps lock - handled a bit differently than other keys.
	if (ns_keycode == 0x39) {
		if (modifier_flags & NSEventModifierFlagCapsLock) {
			// Report as a keypress. This notifies the system
			// that caps lock has been turned on.
			state_ptr->process_key(K_KEY_CAPITAL, true, false);
		} else {
			// Report as a release. This notifies the system
			// that caps lock has been turned off.
			state_ptr->process_key(K_KEY_CAPITAL, false, false);
		}
	}
}

static gamepad_axis gamepad_get_axis_microsoft (u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type) {
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
		case 4:
			return GAMEPAD_AXIS_LZ; // LT
		case 2:
			return GAMEPAD_AXIS_RX;
		case 3:
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

static gamepad_axis gamepad_get_axis_sony (u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type) {
	b8 wireless = connection_type == PLATFORM_GAMEPAD_CONNECTION_TYPE_WIRELESS_BT;
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
		case 4:
			return GAMEPAD_AXIS_LZ; // L2
		case 2:
			return GAMEPAD_AXIS_RX;
		case 3:
			return GAMEPAD_AXIS_RY;
		case 5:
			return GAMEPAD_AXIS_RZ; // R2
		case 16:
			return GAMEPAD_AXIS_DPAD_LR;
		case 17:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 7:
			return wireless ? GAMEPAD_AXIS_RZ : GAMEPAD_AXIS_LZ;
		case 2:
			return GAMEPAD_AXIS_RX;
		case 3:
			return GAMEPAD_AXIS_RY;
		case 8:
			return GAMEPAD_AXIS_RZ; // R2
		case 6:
			return wireless ? GAMEPAD_AXIS_LZ : GAMEPAD_AXIS_IGNORE; // For some reason on macos this axis goes bananas when wired, and doesn't seem to contain any useful information.
		case 4:
			return GAMEPAD_AXIS_DPAD_LR;
		case 5:
			return GAMEPAD_AXIS_DPAD_UD;
		}
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
		// For some reason, the mapping is different depending on the device being connected via USB vs wireless.
		switch (axis_id) {
		default:
			return GAMEPAD_AXIS_UNKNOWN;
		case 0:
			return GAMEPAD_AXIS_LX;
		case 1:
			return GAMEPAD_AXIS_LY;
		case 4:
			return wireless ? GAMEPAD_AXIS_DPAD_LR : GAMEPAD_AXIS_LZ; // L2
		case 2:
			return GAMEPAD_AXIS_RX;
		case 3:
			return GAMEPAD_AXIS_RY;
		case 5:
			return wireless ? GAMEPAD_AXIS_DPAD_UD : GAMEPAD_AXIS_RZ; // R2
		case 6:
			return wireless ? GAMEPAD_AXIS_LZ : GAMEPAD_AXIS_IGNORE; // For some reason on macos this axis goes bananas when wired, and doesn't seem to contain any useful information.
		case 7:
			return wireless ? GAMEPAD_AXIS_RZ : GAMEPAD_AXIS_DPAD_LR;
		case 8:
			return wireless ? GAMEPAD_AXIS_IGNORE : GAMEPAD_AXIS_DPAD_UD;
		}
	}
}

static gamepad_axis gamepad_get_axis_nintendo (u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type) {
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

static gamepad_axis gamepad_get_axis_default (u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type) {
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

static gamepad_button gamepad_get_button_microsoft (u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type) {
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
		case 16:
			return GAMEPAD_BUTTON_SOUTH;
		case 17:
			return GAMEPAD_BUTTON_EAST;
		case 18:
			return GAMEPAD_BUTTON_WEST;
		case 19:
			return GAMEPAD_BUTTON_NORTH;
		case 13:
			return GAMEPAD_BUTTON_L1; // LB
		case 14:
			return GAMEPAD_BUTTON_R1; // RB
		case 10:
			return GAMEPAD_BUTTON_SELECT; // back
		case 9:
			return GAMEPAD_BUTTON_START;
		case 15:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 11:
			return GAMEPAD_BUTTON_L3;
		case 12:
			return GAMEPAD_BUTTON_R3;
		case 5:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 6:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 7:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 8:
			return GAMEPAD_BUTTON_DPAD_RIGHT;
		}
	}
}

static gamepad_button gamepad_get_button_sony (u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type) {
	switch (product_id) {
	case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
		switch (button_id) {
		default:
		case 14:
			return GAMEPAD_BUTTON_SOUTH;
		case 13:
			return GAMEPAD_BUTTON_EAST;
		case 12:
			return GAMEPAD_BUTTON_NORTH;
		case 15:
			return GAMEPAD_BUTTON_WEST;
		case 10:
			return GAMEPAD_BUTTON_L1;
		case 11:
			return GAMEPAD_BUTTON_R1;
		case 8:
			return GAMEPAD_BUTTON_L2;
		case 9:
			return GAMEPAD_BUTTON_R2;
		case 0:
			return GAMEPAD_BUTTON_SELECT;
		case 3:
			return GAMEPAD_BUTTON_START;
		case 16:
			return GAMEPAD_BUTTON_OEM_HOME;
		case 1:
			return GAMEPAD_BUTTON_L3;
		case 2:
			return GAMEPAD_BUTTON_R3;
			// These are axes on PS3 and not mapped to button ids.
		case 4:
			return GAMEPAD_BUTTON_DPAD_UP;
		case 6:
			return GAMEPAD_BUTTON_DPAD_DOWN;
		case 7:
			return GAMEPAD_BUTTON_DPAD_LEFT;
		case 5:
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
			// PS5 microphone button
			return GAMEPAD_BUTTON_ADDL1;
		}
	}
}

static gamepad_button gamepad_get_button_nintendo (u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type) {
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

static gamepad_button gamepad_get_button_default (u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type) {
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

gamepad_axis gamepad_get_axis (u16 vendor_id, u16 product_id, u8 axis_id, platform_gamepad_connection_type connection_type) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_axis_microsoft(product_id, axis_id, connection_type);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_axis_sony(product_id, axis_id, connection_type);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_axis_nintendo(product_id, axis_id, connection_type);
	default:
		return gamepad_get_axis_default(product_id, axis_id, connection_type);
	}
}

gamepad_button gamepad_get_button (u16 vendor_id, u16 product_id, u8 button_id, platform_gamepad_connection_type connection_type) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_button_microsoft(product_id, button_id, connection_type);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_button_sony(product_id, button_id, connection_type);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_button_nintendo(product_id, button_id, connection_type);
	default:
		return gamepad_get_button_default(product_id, button_id, connection_type);
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

#endif
