#include "platform/platform.h"

#if defined(KPLATFORM_APPLE)

#include "core/logger.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kthread.h"
#include "core/kmutex.h"

#include "containers/darray.h"

#include <mach/mach_time.h>
#include <crt_externs.h>

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <pthread.h>
#include <errno.h>        // For error reporting

// For surface creation
#define VK_USE_PLATFORM_METAL_EXT
#include <vulkan/vulkan.h>
#include "renderer/vulkan/vulkan_types.inl"

@class ApplicationDelegate;
@class WindowDelegate;
@class ContentView;
 
typedef struct platform_state {
    ApplicationDelegate* app_delegate;
    WindowDelegate* wnd_delegate;
    NSWindow* window;
    ContentView* view;
    CAMetalLayer* layer;
    VkSurfaceKHR surface;
    b8 quit_flagged;
    u8  modifier_key_states;
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
keys translate_keycode(u32 ns_keycode);
// Modifier key handling
void handle_modifier_keys(u32 ns_keycode, u32 modifier_flags);

@interface WindowDelegate : NSObject <NSWindowDelegate> {
    platform_state* state;
}

- (instancetype)initWithState:(platform_state*)init_state;

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

- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    return YES;
}

- (void)mouseDown:(NSEvent *)event {
    input_process_button(BUTTON_LEFT, true);
}

- (void)mouseDragged:(NSEvent *)event {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)mouseUp:(NSEvent *)event {
    input_process_button(BUTTON_LEFT, false);
}

- (void)mouseMoved:(NSEvent *)event {
    const NSPoint pos = [event locationInWindow];
    
    input_process_mouse_move((i16)pos.x, (i16)pos.y);
}

- (void)rightMouseDown:(NSEvent *)event {
    input_process_button(BUTTON_RIGHT, true);
}

- (void)rightMouseDragged:(NSEvent *)event  {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)rightMouseUp:(NSEvent *)event {
    input_process_button(BUTTON_RIGHT, false);
}

- (void)otherMouseDown:(NSEvent *)event {
    // Interpreted as middle click
    input_process_button(BUTTON_MIDDLE, true);
}

- (void)otherMouseDragged:(NSEvent *)event {
    // Equivalent to moving the mouse for now
    [self mouseMoved:event];
}

- (void)otherMouseUp:(NSEvent *)event {
    // Interpreted as middle click
    input_process_button(BUTTON_MIDDLE, false);
}

// Handle modifier keys since they are only registered via modifier flags being set/unset.
- (void) flagsChanged:(NSEvent *) event {
    handle_modifier_keys([event keyCode], [event modifierFlags]);
}

- (void)keyDown:(NSEvent *)event {
    keys key = translate_keycode((u32)[event keyCode]);

    input_process_key(key, true);

    // [self interpretKeyEvents:@[event]];
}

- (void)keyUp:(NSEvent *)event {
    keys key = translate_keycode((u32)[event keyCode]);

    input_process_key(key, false);
}

- (void)scrollWheel:(NSEvent *)event {
    input_process_mouse_wheel((i8)[event scrollingDeltaY]);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange {}

- (void)unmarkText {}

// Defines a constant for empty ranges in NSTextInputClient
static const NSRange kEmptyRange = { NSNotFound, 0 };

- (NSRange)selectedRange {return kEmptyRange;}

- (NSRange)markedRange {return kEmptyRange;}

- (BOOL)hasMarkedText {return false;}

- (nullable NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {return nil;}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText {return [NSArray array];}

- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange {return NSMakeRect(0, 0, 0, 0);}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {return 0;}

@end // ContentView

@interface ApplicationDelegate : NSObject <NSApplicationDelegate> {}

@end // ApplicationDelegate

@implementation ApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
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

- (instancetype)initWithState:(platform_state*)init_state {
    self = [super init];
    
    if (self != nil) {
        state = init_state;
        state_ptr->quit_flagged = false;
    }
    
    return self;
}

- (BOOL)windowShouldClose:(id)sender {
    state_ptr->quit_flagged = true;

    event_context data = {};
    event_fire(EVENT_CODE_APPLICATION_QUIT, 0, data);

    return YES;
}

- (void)windowDidResize:(NSNotification *)notification {
    event_context context;
    CGSize viewSize = state_ptr->view.bounds.size;
    NSSize newDrawableSize = [state_ptr->view convertSizeToBacking:viewSize];
    state_ptr->layer.drawableSize = newDrawableSize;
    state_ptr->layer.contentsScale = state_ptr->view.window.backingScaleFactor;

    context.data.u16[0] = (u16)newDrawableSize.width;
    context.data.u16[1] = (u16)newDrawableSize.height;
    event_fire(EVENT_CODE_RESIZED, 0, context);
}

- (void)windowDidMiniaturize:(NSNotification *)notification {
    // Send a size of 0, which tells the application it was minimized.
    event_context context;
    context.data.u16[0] = 0;
    context.data.u16[1] = 0;
    event_fire(EVENT_CODE_RESIZED, 0, context);

    [state_ptr->window miniaturize:nil];
}

- (void)windowDidDeminiaturize:(NSNotification *)notification {
    event_context context;
    CGSize viewSize = state_ptr->view.bounds.size;
    NSSize newDrawableSize = [state_ptr->view convertSizeToBacking:viewSize];
    state_ptr->layer.drawableSize = newDrawableSize;
    state_ptr->layer.contentsScale = state_ptr->view.window.backingScaleFactor;

    context.data.u16[0] = (u16)newDrawableSize.width;
    context.data.u16[1] = (u16)newDrawableSize.height;
    event_fire(EVENT_CODE_RESIZED, 0, context);

    [state_ptr->window deminiaturize:nil];
}

@end // WindowDelegate

b8 platform_system_startup(
    u64* memory_requirement,
    void* state,
    const char *application_name,
    i32 x,
    i32 y,
    i32 width,
    i32 height) {
    *memory_requirement = sizeof(platform_state);
    if (state == 0) {
        return true;
    }

    state_ptr = state;

    @autoreleasepool {

    [NSApplication sharedApplication];

    // App delegate creation
    state_ptr->app_delegate = [[ApplicationDelegate alloc] init];
    if (!state_ptr->app_delegate) {
        KERROR("Failed to create application delegate")
        return false;
    }
    [NSApp setDelegate:state_ptr->app_delegate];

    // Window delegate creation
    state_ptr->wnd_delegate = [[WindowDelegate alloc] initWithState:state];
    if (!state_ptr->wnd_delegate) {
        KERROR("Failed to create window delegate")
        return false;
    }

    // Window creation
    state_ptr->window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(x, y, width, height)
        styleMask:NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered
        defer:NO];
    if (!state_ptr->window) {
        KERROR("Failed to create window");
        return false;
    }

    // View creation
    state_ptr->view = [[ContentView alloc] initWithWindow:state_ptr->window];
    [state_ptr->view setWantsLayer:YES];

    // Layer creation
    state_ptr->layer = [CAMetalLayer layer];
    if (!state_ptr->layer) {
        KERROR("Failed to create layer for view");
    }


    // Setting window properties
    [state_ptr->window setLevel:NSNormalWindowLevel];
    [state_ptr->window setContentView:state_ptr->view];
    [state_ptr->window makeFirstResponder:state_ptr->view];
    [state_ptr->window setTitle:@(application_name)];
    [state_ptr->window setDelegate:state_ptr->wnd_delegate];
    [state_ptr->window setAcceptsMouseMovedEvents:YES];
    [state_ptr->window setRestorable:NO];

    if (![[NSRunningApplication currentApplication] isFinishedLaunching])
        [NSApp run];

    // Making the app a proper UI app since we're unbundled
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Putting window in front on launch
    [NSApp activateIgnoringOtherApps:YES];
    [state_ptr->window makeKeyAndOrderFront:nil];

    // Handle content scaling for various fidelity displays (i.e. Retina)
    state_ptr->layer.bounds = state_ptr->view.bounds;
    // It's important to set the drawableSize to the actual backing pixels. When rendering
    // full-screen, we can skip the macOS compositor if the size matches the display size.
    state_ptr->layer.drawableSize = [state_ptr->view convertSizeToBacking:state_ptr->view.bounds.size];

    // In its implementation of vkGetPhysicalDeviceSurfaceCapabilitiesKHR, MoltenVK takes into
    // consideration both the size (in points) of the bounds, and the contentsScale of the
    // CAMetalLayer from which the Vulkan surface was created.
    // See also https://github.com/KhronosGroup/MoltenVK/issues/428
    state_ptr->layer.contentsScale = state_ptr->view.window.backingScaleFactor;
    KDEBUG("contentScale: %f", state_ptr->layer.contentsScale);

    [state_ptr->view setLayer:state_ptr->layer];

    // This is set to NO by default, but is also important to ensure we can bypass the compositor
    // in full-screen mode
    // See "Direct to Display" http://metalkit.org/2017/06/30/introducing-metal-2.html.
    state_ptr->layer.opaque = YES;

    // Fire off a resize event to make sure the framebuffer is the right size.
    // Again, this should be the actual backing framebuffer size (taking into account pixel density).
    event_context context;
    context.data.u16[0] = (u16)state_ptr->layer.drawableSize.width;
    context.data.u16[1] = (u16)state_ptr->layer.drawableSize.height;
    event_fire(EVENT_CODE_RESIZED, 0, context);

    return true;

    } // autoreleasepool
}

void platform_system_shutdown(void* platform_state) {
    if (state_ptr) {
    @autoreleasepool {

        [state_ptr->window orderOut:nil];

        [state_ptr->window setDelegate:nil];
        [state_ptr->wnd_delegate release];

        [state_ptr->view release];
        state_ptr->view = nil;

        [state_ptr->window close];
        state_ptr->window = nil;

        [NSApp setDelegate:nil];
        [state_ptr->app_delegate release];
        state_ptr->app_delegate = nil;

        } // autoreleasepool
    }
    state_ptr = 0;
}

b8 platform_pump_messages(platform_state *plat_state) {
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

        return !state_ptr->quit_flagged;
    }
    return true;
}

void* platform_allocate(u64 size, b8 aligned) {
    return malloc(size);
}

void platform_free(void *block, b8 aligned) {
    free(block);
}

void* platform_zero_memory(void *block, u64 size) {
    return memset(block, 0, size);
}

void* platform_copy_memory(void *dest, const void *source, u64 size) {
    return memcpy(dest, source, size);
}

void* platform_set_memory(void *dest, i32 value, u64 size) {
    return memset(dest, value, size);
}

void platform_console_write(const char *message, u8 colour) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}

void platform_console_write_error(const char *message, u8 colour) {
    // FATAL,ERROR,WARN,INFO,DEBUG,TRACE
    const char* colour_strings[] = {"0;41", "1;31", "1;33", "1;32", "1;34", "1;30"};
    printf("\033[%sm%s\033[0m", colour_strings[colour], message);
}

f64 platform_get_absolute_time() {
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

i32 platform_get_processor_count() {
    return [[NSProcessInfo processInfo] processorCount];
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
        result = pthread_detach((pthread_t)out_thread->thread_id);
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

u64 get_thread_id() {
    return (u64)pthread_self();
}
// NOTE: End threads.


// NOTE: Begin mutexes
b8 kmutex_create(kmutex* out_mutex) {
    if (!out_mutex) {
        return false;
    }

    // Initialize
    pthread_mutex_t mutex;
    i32 result = pthread_mutex_init(&mutex, 0);
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



void platform_get_required_extension_names(const char ***names_darray) {
    darray_push(*names_darray, &"VK_EXT_metal_surface");
    // Required for macos
    darray_push(*names_darray, &VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
}

b8 platform_create_vulkan_surface(vulkan_context *context) {
    if (!state_ptr) {
        return false;
    }

    VkMetalSurfaceCreateInfoEXT create_info = {VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    create_info.pLayer = state_ptr->layer;

    VkResult result = vkCreateMetalSurfaceEXT(
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

keys translate_keycode(u32 ns_keycode) {
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

void handle_modifier_key(
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
    if(modifier_flags & ns_key_mask){
        // Check left variant
        if(modifier_flags & l_mask) {
            if(!(state_ptr->modifier_key_states & l_mod)) {
                state_ptr->modifier_key_states |= l_mod;
                // Report the keypress
                input_process_key(k_l_keycode, true);
            }
        }

        // Check right variant
        if(modifier_flags & r_mask) {
            if(!(state_ptr->modifier_key_states & r_mod)) {
                state_ptr->modifier_key_states |= r_mod;
                // Report the keypress
                input_process_key(k_r_keycode, true);
            }
        } 
    } else {
        if(ns_keycode == ns_l_keycode) {
            if(state_ptr->modifier_key_states & l_mod) {
                state_ptr->modifier_key_states &= ~(l_mod);
                // Report the release.
                input_process_key(k_l_keycode, false);
            }
        }

        if(ns_keycode == ns_r_keycode) {
            if(state_ptr->modifier_key_states & r_mod) {
                state_ptr->modifier_key_states &= ~(r_mod);
                // Report the release.
                input_process_key(k_r_keycode, false);
            }
        }
    }
}

void handle_modifier_keys(u32 ns_keycode, u32 modifier_flags) {
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
    if(ns_keycode == 0x39) {
        if(modifier_flags & NSEventModifierFlagCapsLock) {
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

#endif // SLN_PLATFORM_MACOS