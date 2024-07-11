#include "platform.h"
#include "platform/platform.h"

// TODO: put this back // nocheckin
// #if defined(KPLATFORM_APPLE)

#include "logger.h"
#include "memory/kmemory.h"
#include "strings/kstring.h"

#include "containers/darray.h"

#include <crt_externs.h>
#include <mach/mach_time.h>

#include <copyfile.h>
#include <errno.h>
#include <sys/stat.h>

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/QuartzCore.h>

@class ApplicationDelegate;
@class WindowDelegate;
@class ContentView;

typedef struct macos_handle_info {
    u32 dummy;
} macos_handle_info;

typedef struct macos_file_watch {
    u32 id;
    const char* file_path;
    long last_write_time;
} macos_file_watch;

typedef struct kwindow_platform_state {
    WindowDelegate* wnd_delegate;
    NSWindow* handle;
    ContentView* view;
    CAMetalLayer* layer;
    f32 device_pixel_ratio;
} kwindow_platform_state;

typedef struct platform_state {
    ApplicationDelegate* app_delegate;
    macos_handle_info handle;
    b8 quit_flagged;
    u8 modifier_key_states;
    // darray
    macos_file_watch* watches;

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

static platform_state* state_ptr;

// Key translation
static keys translate_keycode(u32 ns_keycode);
// Modifier key handling
static void handle_modifier_keys(u32 ns_keycode, u32 modifier_flags);
static void platform_update_watches(void);

@interface WindowDelegate : NSObject <NSWindowDelegate> {
  @public
    kwindow* kohi_window;
}

- (instancetype)initWithState:(kwindow*)init_state;

@end // WindowDelegate

@interface ContentView : NSView <NSTextInputClient> {
    NSWindow* window;
    NSTrackingArea* trackingArea;
    NSMutableAttributedString* markedText;
}

- (instancetype)initWithWindow:(NSWindow*)initWindow;

@end // ContentView

@implementation ContentView

- (instancetype)initWithWindow:(NSWindow*)initWindow {
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

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (void)mouseDown:(NSEvent*)event {
    state_ptr->process_mouse_button(MOUSE_BUTTON_LEFT, true);
}

- (void)mouseDragged:(NSEvent*)event {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)mouseUp:(NSEvent*)event {
    state_ptr->process_mouse_button(MOUSE_BUTTON_LEFT, false);
}

- (void)mouseMoved:(NSEvent*)event {
    const NSPoint pos = [event locationInWindow];
    kwindow* w = ((WindowDelegate*)event.window.delegate)->kohi_window;
    kwindow_platform_state* ps = w->platform_state;

    // Need to invert Y on macOS, since origin is bottom-left.
    // Also need to scale the mouse position by the device pixel ratio so screen lookups are correct.
    NSSize window_size = ps->layer.drawableSize;
    i16 x = pos.x * ps->layer.contentsScale;
    i16 y = window_size.height - (pos.y * ps->layer.contentsScale);

    state_ptr->process_mouse_move(x, y);
}

- (void)rightMouseDown:(NSEvent*)event {
    state_ptr->process_mouse_button(MOUSE_BUTTON_RIGHT, true);
}

- (void)rightMouseDragged:(NSEvent*)event {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)rightMouseUp:(NSEvent*)event {
    state_ptr->process_mouse_button(MOUSE_BUTTON_RIGHT, false);
}

- (void)otherMouseDown:(NSEvent*)event {
    // Interpreted as middle click
    state_ptr->process_mouse_button(MOUSE_BUTTON_MIDDLE, true);
}

- (void)otherMouseDragged:(NSEvent*)event {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)otherMouseUp:(NSEvent*)event {
    // Interpreted as middle click
    state_ptr->process_mouse_button(MOUSE_BUTTON_MIDDLE, false);
}

// Handle modifier keys since they are only registered via modifier flags being set/unset.
- (void)flagsChanged:(NSEvent*)event {
    handle_modifier_keys([event keyCode], [event modifierFlags]);
}

- (void)keyDown:(NSEvent*)event {
    keys key = translate_keycode((u32)[event keyCode]);

    state_ptr->process_key(key, true);

    // [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent*)event {
    keys key = translate_keycode((u32)[event keyCode]);

    state_ptr->process_key(key, false);
}

- (void)scrollWheel:(NSEvent*)event {
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

- (nullable NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
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

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // Posting an empty event at start
    @autoreleasepool {

        NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
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

- (instancetype)initWithState:(kwindow*)init_state {
    self = [super init];

    if (self != nil) {
        kohi_window = init_state;
        state_ptr->quit_flagged = false;
    }

    return self;
}

- (BOOL)windowShouldClose:(NSWindow*)sender {
    state_ptr->quit_flagged = true;

    if (state_ptr->window_closed_callback) {
        kwindow* w = ((WindowDelegate*)sender.delegate)->kohi_window;
        if (!w) {
            KERROR("Recieved a window close event for a non-registered window!");
            return NO;
        }
        state_ptr->window_closed_callback(w);
    }

    return YES;
}

- (void)windowDidChangeScreen:(NSNotification*)notification {
    kwindow* w = ((WindowDelegate*)notification.object)->kohi_window;
    kwindow_platform_state* ps = w->platform_state;
    CGSize viewSize = ps->view.bounds.size;
    NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
    ps->layer.drawableSize = newDrawableSize;
    ps->layer.contentsScale = ps->view.window.backingScaleFactor;
    // Save off the device pixel ratio.
    ps->device_pixel_ratio = ps->layer.contentsScale;

    w->width = (u16)newDrawableSize.width;
    w->height = (u16)newDrawableSize.height;
    state_ptr->window_resized_callback(w);
}

- (void)windowDidResize:(NSNotification*)notification {
    kwindow* w = ((WindowDelegate*)notification.object)->kohi_window;
    kwindow_platform_state* ps = w->platform_state;
    CGSize viewSize = ps->view.bounds.size;
    NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
    ps->layer.drawableSize = newDrawableSize;
    ps->layer.contentsScale = ps->view.window.backingScaleFactor;
    // Save off the device pixel ratio.
    ps->device_pixel_ratio = ps->layer.contentsScale;

    w->width = (u16)newDrawableSize.width;
    w->height = (u16)newDrawableSize.height;
    state_ptr->window_resized_callback(w);
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
    // Send a size of 0, which tells the application it was minimized.
    kwindow* w = ((WindowDelegate*)notification.object)->kohi_window;
    kwindow_platform_state* ps = w->platform_state;

    w->width = 0;
    w->height = 0;
    state_ptr->window_resized_callback(w);

    [ps->handle miniaturize:nil];
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
    kwindow* w = ((WindowDelegate*)notification.object)->kohi_window;
    kwindow_platform_state* ps = w->platform_state;
    CGSize viewSize = ps->view.bounds.size;
    NSSize newDrawableSize = [ps->view convertSizeToBacking:viewSize];
    ps->layer.drawableSize = newDrawableSize;
    ps->layer.contentsScale = ps->view.window.backingScaleFactor;
    // Save off the device pixel ratio.
    ps->device_pixel_ratio = ps->layer.contentsScale;

    w->width = (u16)newDrawableSize.width;
    w->height = (u16)newDrawableSize.height;
    state_ptr->window_resized_callback(w);

    [ps->handle deminiaturize:nil];
}

@end // WindowDelegate

b8 platform_system_startup(u64* memory_requirement, platform_state* state, platform_system_config* config) {
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }

    state_ptr = state;

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

void platform_system_shutdown(platform_state* state) {
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

b8 platform_window_create(const kwindow_config* config, struct kwindow* window, b8 show_immediately) {
    if (!window) {
        return false;
    }

    window->platform_state = kallocate(sizeof(kwindow_platform_state), MEMORY_TAG_UNKNOWN);
    kwindow_platform_state* ps = window->platform_state;
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
    darray_push(state_ptr->windows, window);

    /*if (show_immediately) {
        platform_window_show(window);
    }*/

    return true;
}

void platform_window_destroy(struct kwindow* window) {
    if (window) {
        u32 len = darray_length(state_ptr->windows);
        for (u32 i = 0; i < len; ++i) {
            kwindow* w = state_ptr->windows[i];
            if (w == window) {
                KTRACE("Destroying window...");

                kwindow_platform_state* ps = w->platform_state;

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

        kwindow_platform_state* ps = window->platform_state;
        [ps->handle orderOut:nil];

        [ps->handle setDelegate:nil];
        [ps->wnd_delegate release];

        [ps->view release];
        ps->view = nil;

        [ps->handle close];
        ps->handle = nil;
    }
}

b8 platform_window_show(struct kwindow* window) {
    if (!window) {
        return false;
    }

    kwindow_platform_state* ps = window->platform_state;
    //[ps->handle show];

    // TODO: is this it?
    [ps->handle makeKeyAndOrderFront:nil];
    [ps->handle setIsVisible:(BOOL)YES];

    return true;
}

b8 platform_window_hide(struct kwindow* window) {
    if (!window) {
        return false;
    }

    kwindow_platform_state* ps = window->platform_state;
    [ps->handle setIsVisible:(BOOL)NO];

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

    if (window->title) {
        string_free(window->title);
    }
    window->title = string_duplicate(title);
    kwindow_platform_state* ps = window->platform_state;
    [ps->handle setTitle:@(window->title)];
    return true;
}

b8 platform_pump_messages(void) {
    if (state_ptr) {
        @autoreleasepool {

            NSEvent* event;

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

        return !state_ptr->quit_flagged;
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

void platform_console_write(struct platform_state* platform, log_level level, const char* message) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[level], message);
}

/*void platform_console_write_error(const char* message, u8 colour) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}*/

f64 platform_get_absolute_time(void) {
    mach_timebase_info_data_t clock_timebase;
    mach_timebase_info(&clock_timebase);

    u64 mach_absolute = mach_absolute_time();

    u64 nanos = (f64)(mach_absolute * (u64)clock_timebase.numer) / (f64)clock_timebase.denom;
    return nanos / 1.0e9; // Convert to seconds
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
    return [[NSProcessInfo processInfo] processorCount];
}

void platform_get_handle_info(u64* out_size, void* memory) {

    *out_size = sizeof(macos_handle_info);
    if (!memory) {
        return;
    }

    kcopy_memory(memory, &state_ptr->handle, *out_size);
}

f32 platform_device_pixel_ratio(void) {
    // LEFTOFF: This is window specific and will require an interface change
    // at the platform-agnostic layer frontend.
    return state_ptr->device_pixel_ratio;
}

const char* platform_dynamic_library_extension(void) {
    return ".dylib";
}

const char* platform_dynamic_library_prefix(void) {
    return "lib";
}

platform_error_code platform_copy_file(const char* source, const char* dest, b8 overwrite_if_exists) {
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

static b8 register_watch(const char* file_path, u32* out_watch_id) {
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
        macos_file_watch* w = &state_ptr->watches[i];
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

    macos_file_watch* w = &state_ptr->watches[watch_id];
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
        macos_file_watch* f = &state_ptr->watches[i];
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

static keys translate_keycode(u32 ns_keycode) {
    // https://boredzo.org/blog/wp-content/uploads/2007/05/IMTx-virtual-keycodes.pdf
    // https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
    switch (ns_keycode) {
    case 0x52:
        return KEY_NUMPAD0;
    case 0x53:
        return KEY_NUMPAD1;
    case 0x54:
        return KEY_NUMPAD2;
    case 0x55:
        return KEY_NUMPAD3;
    case 0x56:
        return KEY_NUMPAD4;
    case 0x57:
        return KEY_NUMPAD5;
    case 0x58:
        return KEY_NUMPAD6;
    case 0x59:
        return KEY_NUMPAD7;
    case 0x5B:
        return KEY_NUMPAD8;
    case 0x5C:
        return KEY_NUMPAD9;

    case 0x12:
        return KEY_1;
    case 0x13:
        return KEY_2;
    case 0x14:
        return KEY_3;
    case 0x15:
        return KEY_4;
    case 0x17:
        return KEY_5;
    case 0x16:
        return KEY_6;
    case 0x1A:
        return KEY_7;
    case 0x1C:
        return KEY_8;
    case 0x19:
        return KEY_9;
    case 0x1D:
        return KEY_0;

    case 0x00:
        return KEY_A;
    case 0x0B:
        return KEY_B;
    case 0x08:
        return KEY_C;
    case 0x02:
        return KEY_D;
    case 0x0E:
        return KEY_E;
    case 0x03:
        return KEY_F;
    case 0x05:
        return KEY_G;
    case 0x04:
        return KEY_H;
    case 0x22:
        return KEY_I;
    case 0x26:
        return KEY_J;
    case 0x28:
        return KEY_K;
    case 0x25:
        return KEY_L;
    case 0x2E:
        return KEY_M;
    case 0x2D:
        return KEY_N;
    case 0x1F:
        return KEY_O;
    case 0x23:
        return KEY_P;
    case 0x0C:
        return KEY_Q;
    case 0x0F:
        return KEY_R;
    case 0x01:
        return KEY_S;
    case 0x11:
        return KEY_T;
    case 0x20:
        return KEY_U;
    case 0x09:
        return KEY_V;
    case 0x0D:
        return KEY_W;
    case 0x07:
        return KEY_X;
    case 0x10:
        return KEY_Y;
    case 0x06:
        return KEY_Z;

    case 0x27:
        return KEY_APOSTROPHE;
    case 0x2A:
        return KEY_BACKSLASH;
    case 0x2B:
        return KEY_COMMA;
    case 0x18:
        return KEY_EQUAL; // Equal/Plus
    case 0x32:
        return KEY_GRAVE;
    case 0x21:
        return KEY_LBRACKET;
    case 0x1B:
        return KEY_MINUS;
    case 0x2F:
        return KEY_PERIOD;
    case 0x1E:
        return KEY_RBRACKET;
    case 0x29:
        return KEY_SEMICOLON;
    case 0x2C:
        return KEY_SLASH;
    case 0x0A:
        return KEYS_MAX_KEYS; // ?

    case 0x33:
        return KEY_BACKSPACE;
    case 0x39:
        return KEY_CAPITAL;
    case 0x75:
        return KEY_DELETE;
    case 0x7D:
        return KEY_DOWN;
    case 0x77:
        return KEY_END;
    case 0x24:
        return KEY_ENTER;
    case 0x35:
        return KEY_ESCAPE;
    case 0x7A:
        return KEY_F1;
    case 0x78:
        return KEY_F2;
    case 0x63:
        return KEY_F3;
    case 0x76:
        return KEY_F4;
    case 0x60:
        return KEY_F5;
    case 0x61:
        return KEY_F6;
    case 0x62:
        return KEY_F7;
    case 0x64:
        return KEY_F8;
    case 0x65:
        return KEY_F9;
    case 0x6D:
        return KEY_F10;
    case 0x67:
        return KEY_F11;
    case 0x6F:
        return KEY_F12;
    case 0x69:
        return KEY_PRINT;
    case 0x6B:
        return KEY_F14;
    case 0x71:
        return KEY_F15;
    case 0x6A:
        return KEY_F16;
    case 0x40:
        return KEY_F17;
    case 0x4F:
        return KEY_F18;
    case 0x50:
        return KEY_F19;
    case 0x5A:
        return KEY_F20;
    case 0x73:
        return KEY_HOME;
    case 0x72:
        return KEY_INSERT;
    case 0x7B:
        return KEY_LEFT;
    case 0x3A:
        return KEY_LALT;
    case 0x3B:
        return KEY_LCONTROL;
    case 0x38:
        return KEY_LSHIFT;
    case 0x37:
        return KEY_LSUPER;
    case 0x6E:
        return KEYS_MAX_KEYS; // Menu
    case 0x47:
        return KEY_NUMLOCK;
    case 0x79:
        return KEYS_MAX_KEYS; // Page down
    case 0x74:
        return KEYS_MAX_KEYS; // Page up
    case 0x7C:
        return KEY_RIGHT;
    case 0x3D:
        return KEY_RALT;
    case 0x3E:
        return KEY_RCONTROL;
    case 0x3C:
        return KEY_RSHIFT;
    case 0x36:
        return KEY_RSUPER;
    case 0x31:
        return KEY_SPACE;
    case 0x30:
        return KEY_TAB;
    case 0x7E:
        return KEY_UP;

    case 0x45:
        return KEY_ADD;
    case 0x41:
        return KEY_DECIMAL;
    case 0x4B:
        return KEY_DIVIDE;
    case 0x4C:
        return KEY_ENTER;
    case 0x51:
        return KEY_NUMPAD_EQUAL;
    case 0x43:
        return KEY_MULTIPLY;
    case 0x4E:
        return KEY_SUBTRACT;

    default:
        return KEYS_MAX_KEYS;
    }
}

// Bit masks for left and right versions of these keys.
#define MACOS_LSHIFT_MASK (1 << 1)
#define MACOS_RSHIFT_MASK (1 << 2)
#define MACOS_LCTRL_MASK (1 << 0)
#define MACOS_RCTRL_MASK (1 << 13)
#define MACOS_LCOMMAND_MASK (1 << 3)
#define MACOS_RCOMMAND_MASK (1 << 4)
#define MACOS_LALT_MASK (1 << 5)
#define MACOS_RALT_MASK (1 << 6)

static void handle_modifier_key(
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
                input_process_key(k_l_keycode, true);
            }
        }

        // Check right variant
        if (modifier_flags & r_mask) {
            if (!(state_ptr->modifier_key_states & r_mod)) {
                state_ptr->modifier_key_states |= r_mod;
                // Report the keypress
                input_process_key(k_r_keycode, true);
            }
        }
    } else {
        if (ns_keycode == ns_l_keycode) {
            if (state_ptr->modifier_key_states & l_mod) {
                state_ptr->modifier_key_states &= ~(l_mod);
                // Report the release.
                input_process_key(k_l_keycode, false);
            }
        }

        if (ns_keycode == ns_r_keycode) {
            if (state_ptr->modifier_key_states & r_mod) {
                state_ptr->modifier_key_states &= ~(r_mod);
                // Report the release.
                input_process_key(k_r_keycode, false);
            }
        }
    }
}

static void handle_modifier_keys(u32 ns_keycode, u32 modifier_flags) {
    // Shift
    handle_modifier_key(
        ns_keycode,
        NSEventModifierFlagShift,
        0x38,
        0x3C,
        KEY_LSHIFT,
        KEY_RSHIFT,
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
        KEY_LCONTROL,
        KEY_RCONTROL,
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
        KEY_LALT,
        KEY_RALT,
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
        KEY_LSUPER,
        KEY_RSUPER,
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
            input_process_key(KEY_CAPITAL, true);
        } else {
            // Report as a release. This notifies the system
            // that caps lock has been turned off.
            input_process_key(KEY_CAPITAL, false);
        }
    }
}

// #endif // TODO: put this back // nocheckin
