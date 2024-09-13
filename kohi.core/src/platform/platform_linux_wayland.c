// Own include.
#include "platform.h"

#if KPLATFORM_LINUX_WAYLAND
// Internal includies.
#include "platform_linux_wayland_xdg.h"
#include "logger.h"
#include "memory/kmemory.h"
#include "input_types.h"
#include "debug/kassert.h"
#include "strings/kstring.h"
#include "containers/darray.h"

// External includies.
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
    
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define WL_CHECK(expr)                                         \
if(!(expr))                                                    \
{                                                              \
    KFATAL("WAYLAND: Error executing %s in file %s in line %d",\
           #expr, __FILE_NAME__, __LINE__);                    \
    return FALSE;                                              \
}

// TODO: Edit later.
typedef struct platform_state {
    struct wl_display*    display;
    struct wl_registry*   registry;
    struct wl_compositor* compositor;
    struct wl_surface*    surface;
    struct xdg_wm_base*   xbase;
    struct xdg_surface*   xsurface;
    struct xdg_toplevel*  xtoplevel;
    struct wl_seat*       seat;
    struct wl_keyboard*   keyboard;
    struct wl_pointer*    pointer;
    struct xkb_context*   xkbcontext;
    struct xkb_keymap*    xkbkeymap;
    struct xkb_state*     xkbstate;

    i32 width;
    i32 height;
    b8 resized;

    // darray
    linux_file_watch* watches;

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

  /*  Keyboard translate linux keycode to virtual keycode. See linux/input-event-codes.h.
      @param keycode - Linux key code.
      @return Virtual key code.
  */
  keyboard_key kb_translate_keycode(u32 keycode)
  {
      // 0x7f - context menu
      static const keyboard_key code[KEYS_MAX] = {
          [0x01] = KEY_ESCAPE,       [0x02] = KEY_1,            [0x03] = KEY_2,            [0x04] = KEY_3,
          [0x05] = KEY_4,            [0x06] = KEY_5,            [0x07] = KEY_6,            [0x08] = KEY_7,
          [0x09] = KEY_8,            [0x0A] = KEY_9,            [0x0B] = KEY_0,            [0x0C] = KEY_MINUS,
          [0x0D] = KEY_EQUAL,        [0x0E] = KEY_BACKSPACE,    [0x0F] = KEY_TAB,          [0x10] = KEY_Q,
          [0x11] = KEY_W,            [0x12] = KEY_E,            [0x13] = KEY_R,            [0x14] = KEY_T,
          [0x15] = KEY_Y,            [0x16] = KEY_U,            [0x17] = KEY_I,            [0x18] = KEY_O,
          [0x19] = KEY_P,            [0x1A] = KEY_LBRACKET,     [0x1B] = KEY_RBRACKET,     [0x1C] = KEY_ENTER,
          [0x1D] = KEY_LCONTROL,     [0x1E] = KEY_A,            [0x1F] = KEY_S,            [0x20] = KEY_D,
          [0x21] = KEY_F,            [0x22] = KEY_G,            [0x23] = KEY_H,            [0x24] = KEY_J,
          [0x25] = KEY_K,            [0x26] = KEY_L,            [0x27] = KEY_SEMICOLON,    [0x28] = KEY_APOSTROPHE,
          [0x29] = KEY_GRAVE,        [0x2A] = KEY_LSHIFT,       [0x2B] = KEY_BACKSLASH,    [0x2C] = KEY_Z,
          [0x2D] = KEY_X,            [0x2E] = KEY_C,            [0x2F] = KEY_V,            [0x30] = KEY_B,
          [0x31] = KEY_N,            [0x32] = KEY_M,            [0x33] = KEY_COMMA,        [0x34] = KEY_DOT,
          [0x35] = KEY_SLASH,        [0x36] = KEY_RSHIFT,       [0x37] = KEY_UNKNOWN,      [0x38] = KEY_LALT,
          [0x39] = KEY_SPACE,        [0x3A] = KEY_CAPSLOCK,     [0x3B] = KEY_F1,           [0x3C] = KEY_F2,
          [0x3D] = KEY_F3,           [0x3E] = KEY_F4,           [0x3F] = KEY_F5,           [0x40] = KEY_F6,
          [0x41] = KEY_F7,           [0x42] = KEY_F8,           [0x43] = KEY_F9,           [0x44] = KEY_F10,
          [0x45] = KEY_NUMLOCK,      [0x46] = KEY_SCROLLOCK,    [0x47] = KEY_NUMPAD7,      [0x48] = KEY_NUMPAD8,
          [0x49] = KEY_NUMPAD9,      [0x4A] = KEY_SUBTRACT,     [0x4B] = KEY_NUMPAD4,      [0x4C] = KEY_NUMPAD5,
          [0x4D] = KEY_NUMPAD6,      [0x4E] = KEY_ADD,          [0x4F] = KEY_NUMPAD1,      [0x50] = KEY_NUMPAD2,
          [0x51] = KEY_NUMPAD3,      [0x52] = KEY_NUMPAD0,      [0x53] = KEY_DECIMAL,      [0x57] = KEY_F11,
          [0x58] = KEY_F12,          [0x60] = KEY_UNKNOWN,      [0x61] = KEY_RCONTROL,     [0x62] = KEY_DIVIDE,
          [0x63] = KEY_PRINTSCREEN,  [0x64] = KEY_RALT,         [0x65] = KEY_UNKNOWN,      [0x66] = KEY_HOME,
          [0x67] = KEY_UP,           [0x68] = KEY_PAGEUP,       [0x69] = KEY_LEFT,         [0x6A] = KEY_RIGHT,
          [0x6B] = KEY_END,          [0x6C] = KEY_DOWN,         [0x6D] = KEY_PAGEDOWN,     [0x6E] = KEY_INSERT,
          [0x6F] = KEY_DELETE,       [0x75] = KEY_NUMPAD_EQUAL, [0x76] = KEY_UNKNOWN,      [0x77] = KEY_PAUSE,
          [0x7D] = KEY_LSUPER,       [0x7E] = KEY_RSUPER,       [0x7F] = KEY_UNKNOWN,      
          //...
          [0xB7] = KEY_F13,          [0xB8] = KEY_F14,          [0xB9] = KEY_F15,          [0xBA] = KEY_F16,
          [0xBB] = KEY_F17,          [0xBC] = KEY_F18,          [0xBD] = KEY_F19,          [0xBE] = KEY_F20,
          [0xBF] = KEY_F21,          [0xC0] = KEY_F22,          [0xC1] = KEY_F23,          [0xC2] = KEY_F24,
          //...
          [0xD2] = KEY_PRINT
      };

      return code[keycode];
  }

  /*  Pointer translate linux button code to virtual button code. See linux/input-event-codes.h.
      @param button - Linux button code.
      @return Virtual button code.
  */
  u16 pt_translate_code(u32 button)
  {
      static const u16 code[] = { [0x110] = BTN_LEFT, [0x111] = BTN_RIGHT, [0x112] = BTN_MIDDLE };
      return code[button];
  }

  // Keyboard event 'keymap'.
  static void kb_keymap(void* data, struct wl_keyboard* kb, u32 format, i32 fd, u32 size)
  {
      char* str_config;

      KASSERT_MSG(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, "Keyboard format XKB_V1 is not available.");
      KASSERT((str_config = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)) != MAP_FAILED);

      platform_state* inst = (platform_state*)data;
      struct xkb_keymap* kb_keymap = xkb_keymap_new_from_string(inst->xkbcontext, str_config, 
                                                                XKB_KEYMAP_FORMAT_TEXT_V1, 
                                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
      munmap(str_config, size);
      close(fd);

      struct xkb_state* kb_state = xkb_state_new(kb_keymap);
  
      xkb_keymap_unref(inst->xkbkeymap);
      xkb_state_unref(inst->xkbstate);
  
      inst->xkbkeymap = kb_keymap;
      inst->xkbstate  = kb_state;
  }

  // Keyboard event 'enter'. The event occurs when the user enter to a windowed application.
  static void kb_enter(void* data, struct wl_keyboard* kb, u32 serial, struct wl_surface* surface, struct wl_array* keys)
  {
  }

  // Keyboard event 'leave'. The event occurs when the user leave a windowed application.
  static void kb_leave(void* data, struct wl_keyboard* kb, u32 serial, struct wl_surface* surface)
  {
  }

  // Keyboard event 'key'. The event occurs when a keyboard key was pressed or released.
  static void kb_key(void* data, struct wl_keyboard* kb, u32 serial, u32 time, u32 key, u32 state)
  {
      // u32 keycode = key + 8;
      // xkb_keysym_t sym = xkb_state_key_get_one_sym(((platform_state*)data)->xkbstate, keycode);

      keyboard_key vkey = kb_translate_keycode(key);
      input_keyboard_process_key(vkey, (state == WL_KEYBOARD_KEY_STATE_PRESSED) ? TRUE : FALSE );
  }

  // keyboard event 'mod'. The event occurs when a special key on the keyboard is pressed or released.
  static void kb_mod(void* data, struct wl_keyboard* kb, u32 serial, u32 pressed, u32 latched, u32 locked, u32 group)
  {
      // xkb_state_update_mask(((platform_state*)data)->xkbstate, pressed, latched, locked, 0, 0, group);
  }

  static void kb_info(void* data, struct wl_keyboard* kb, i32 rate, i32 delay)
  {
      // platform_state* inst = (platform_state*)data;
      // inst->rate  = rate;
      // inst->delay = delay;
  }

  // Pointer event 'enter'. The event occurs when the user enter to a windowed application.
  static void pt_enter(void* data, struct wl_pointer* pt, u32 serial, struct wl_surface* surface, i32 x, i32 y)
  {
  }

  // Pointer event 'leave'. The event occurs when the user leave a windowed application.
  static void pt_leave(void* data, struct wl_pointer *pt, u32 serial, struct wl_surface* surface)
  {
  }

  // Pointer event 'move'. ...
  static void pt_motion(void* data, struct wl_pointer* pt, u32 time, i32 x, i32 y)
  {
      x = wl_fixed_to_int(x);
      y = wl_fixed_to_int(y);
      input_mouse_process_move(x, y);
  }

  // Pointer event 'button'. ...
  static void pt_button(void* data, struct wl_pointer* pt, u32 serial, u32 time, u32 button, u32 state)
  {
      mouse_button btn = pt_translate_code(button);
      input_mouse_process_button(btn, state == WL_POINTER_BUTTON_STATE_PRESSED ? TRUE : FALSE);
  }

  // Pointer event 'axis'. The event occurs when the mouse wheel is rotated.
  static void pt_axis(void* data, struct wl_pointer* pt, u32 time, u32 axis, i32 value)
  {
      // NOTE: axis may be 'WL_POINTER_AXIS_VERTICAL_SCROLL' or 'WL_POINTER_AXIS_HORIZONTAL_SCROLL'.
      input_mouse_process_wheel(value);
  }

  // Listeners: Pointer (pt) and Keyboard (kb).
  static const struct wl_pointer_listener  pt_listener = { pt_enter, pt_leave, pt_motion, pt_button, pt_axis };
  static const struct wl_keyboard_listener kb_listener = { kb_keymap, kb_enter, kb_leave, kb_key, kb_mod, kb_info };

  // Detects input devices.
  static void seat_capabilies(void* data, struct wl_seat* seat, u32 capabilities)
  {
      platform_state* inst = (platform_state*)data;

      b8 have_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
      b8 have_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;

      // Below the mouse pointer update.
      if(have_pointer && inst->pointer == NULL)
      {
          inst->pointer = wl_seat_get_pointer(seat);
          wl_pointer_add_listener(inst->pointer, &pt_listener, data);
      }
      else if(!have_pointer && inst->pointer != NULL)
      {
          wl_pointer_release(inst->pointer);
          inst->pointer = NULL;
      }

      // Below the keyboard pointer update.
      if(have_keyboard && inst->keyboard == NULL)
      {
          inst->keyboard = wl_seat_get_keyboard(seat);
          wl_keyboard_add_listener(inst->keyboard, &kb_listener, data);
      }
      else if(!have_keyboard && inst->keyboard != NULL)
      {
          wl_keyboard_release(inst->keyboard);
          inst->keyboard = NULL;
      }
  }

  // Listener seat only for version 1.
  static const struct wl_seat_listener seat_listener = { seat_capabilies };

  // Toplevel event 'configure'. Passes the current window dimensions.
  static void xtoplevel_configure(void* data, struct xdg_toplevel* xtoplevel, i32 width, i32 height, struct wl_array* states)
  {
      platform_state* state = (platform_state*)data;

      if(width != 0 && height != 0)
      {
          state->resized = TRUE;
          state->width = width;
          state->height = height;
      }
  }

  // Toplevel event 'close'.
  static void xtoplevel_close(void* data, struct xdg_toplevel* xtoplevel)
  {
      event_context context = {};
      event_send(EVENT_CODE_APPLICATION_QUIT, data, context);
  }

  // Surface event 'configure'.
  static void xsurface_configure(void* data, struct xdg_surface* xsurface, u32 serial)
  {
      platform_state* state = (platform_state*)data;

      xdg_surface_ack_configure(xsurface, serial);

      if(state->resized)
      {
          event_context context = {};
          context.data.u16[0] = (u16)state->width;
          context.data.u16[1] = (u16)state->height;
          event_send(EVENT_CODE_RESIZED, NULL, context);
          state->resized = FALSE;
      }
  }

  // Shell event 'ping'. Checks if the application window is accessible.
  static void xbase_ping(void* data, struct xdg_wm_base* xbase, u32 serial)
  {
      xdg_wm_base_pong(xbase, serial);
  }

  // Shell listeners below.
  static const struct xdg_toplevel_listener xtoplevel_listener = { xtoplevel_configure, xtoplevel_close };
  static const struct xdg_surface_listener  xsurface_listener  = { xsurface_configure };
  static const struct xdg_wm_base_listener  xbase_listener     = { xbase_ping };

  // Registry event 'add listener'.
  static void registry_add(void* data, struct wl_registry* registry, u32 id, const char* iface, u32 ver)
  {
      platform_state* inst = (platform_state*)data;

      if(strcmp(iface, wl_compositor_interface.name) == 0)
      {
          inst->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
      }
      else if(strcmp(iface, xdg_wm_base_interface.name) == 0)
      {
          inst->xbase = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
          xdg_wm_base_add_listener(inst->xbase, &xbase_listener, NULL);
      }
      else if(strcmp(iface, wl_seat_interface.name) == 0)
      {
          inst->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
          wl_seat_add_listener(inst->seat, &seat_listener, inst);
      }
  }

  // Registry event 'remove (losing)'.
  static void registry_remove(void* data, struct wl_registry* registry, u32 id)
  {
  }

  // Registry listener.
  static const struct wl_registry_listener registry_listener = { registry_add, registry_remove };



b8 platform_system_startup(u64* memory_requirement, struct platform_state* state, platform_system_config* config);

void platform_system_shutdown(struct platform_state* state);

b8 platform_window_create(const kwindow_config* config, struct kwindow* window, b8 show_immediately)
{
    WL_CHECK(window->platform_state = kmallocate_t(MEMORY_TAG_SYSTEM, platform_state));
    kzero_memory_t(window->platform_state, platform_state);

    // TODO: create string_duplicate macros!
    window->title = (const char*)string_duplicate(config->title);
    window->width = config->width;
    window->height = config->height;
    window->resizing = FALSE;

    platform_state* inst = window->platform_state;

    // TEST!
    inst->width = config->width;
    inst->height = config->height;
    inst->resized = FALSE;

    // 1. Initialize wayland client.
    WL_CHECK(inst->display = wl_display_connect(NULL));
    
    WL_CHECK(inst->registry = wl_display_get_registry(inst->display));
    wl_registry_add_listener(inst->registry, &registry_listener, inst);
    wl_display_roundtrip(inst->display);

    // 2. Initialize compositor, window.
    WL_CHECK(inst->compositor);
    WL_CHECK(inst->xkbcontext = xkb_context_new(XKB_CONTEXT_NO_FLAGS));

    WL_CHECK(inst->surface = wl_compositor_create_surface(inst->compositor));
    
    WL_CHECK(inst->xbase);
    WL_CHECK(inst->xsurface = xdg_wm_base_get_xdg_surface(inst->xbase, inst->surface));
    xdg_surface_add_listener(inst->xsurface, &xsurface_listener, inst);

    WL_CHECK(inst->xtoplevel = xdg_surface_get_toplevel(inst->xsurface));
    xdg_toplevel_add_listener(inst->xtoplevel, &xtoplevel_listener, inst);

    xdg_toplevel_set_title(inst->xtoplevel, window->title);
    xdg_toplevel_set_app_id(inst->xtoplevel, window->title);

    // Full screen!
    // xdg_toplevel_set_fullscreen(inst->xtoplevel, NULL);
    
    wl_surface_commit(inst->surface);
    wl_display_roundtrip(inst->display);
    wl_surface_commit(inst->surface);

    KTRACE("Platform window created.");
    
    return TRUE;
}


void platform_window_destroy(struct kwindow* window)
{
    if(window->platform_state)
    {
        KTRACE("Destroying Platform window...");
        platform_state* inst = window->platform_state;
        if(inst->xkbkeymap)  { xkb_keymap_unref(inst->xkbkeymap); inst->xkbkeymap = NULL;        }
        if(inst->xkbstate)   { xkb_state_unref(inst->xkbstate); inst->xkbstate = NULL;           }
        if(inst->xkbcontext) { xkb_context_unref(inst->xkbcontext); inst->xkbcontext = NULL;     }
        if(inst->keyboard)   { wl_keyboard_destroy(inst->keyboard); inst->keyboard = NULL;       }
        if(inst->pointer)    { wl_pointer_destroy(inst->pointer); inst->pointer = NULL;          }
        if(inst->seat)       { wl_seat_destroy(inst->seat); inst->seat = NULL;                   }
        if(inst->xtoplevel)  { xdg_toplevel_destroy(inst->xtoplevel); inst->xtoplevel = NULL;    }
        if(inst->xsurface)   { xdg_surface_destroy(inst->xsurface); inst->xsurface = NULL;       }
        if(inst->xbase)      { xdg_wm_base_destroy(inst->xbase); inst->xbase = NULL;             }
        if(inst->surface)    { wl_surface_destroy(inst->surface); inst->surface = NULL;          }
        if(inst->compositor) { wl_compositor_destroy(inst->compositor); inst->compositor = NULL; }
        if(inst->registry)   { wl_registry_destroy(inst->registry); inst->registry = NULL;       }
        if(inst->display)    { wl_display_disconnect(inst->display); inst->display = NULL;       }
        kmfree(window->title); window->title = NULL;
        kmfree(window->platform_state); window->platform_state = NULL;
    }

}

b8 platform_window_show(struct kwindow* window);

b8 platform_window_hide(struct kwindow* window);

const char* platform_window_title_get(const struct kwindow* window);

b8 platform_window_title_set(struct kwindow* window, const char* title);

// TODO: static struct?
b8 platform_pump_messages(void)
{
    return wl_display_roundtrip(window->platform_state->display) != -1;
}

void* platform_allocate(u64 size, b8 aligned);

void platform_free(void* block, b8 aligned);

void* platform_zero_memory(void* block, u64 size);

void* platform_copy_memory(void* dest, const void* source, u64 size);

void* platform_move_memory(void* dest, const void* source, u64 size);

void* platform_set_memory(void* dest, i32 value, u64 size);

void platform_console_write(struct platform_state* platform, log_level level, const char* message);

f64 platform_get_absolute_time(void);

void platform_sleep(u64 ms);

i32 platform_get_processor_count(void);

void platform_get_handle_info(u64* out_size, void* memory);

f32 platform_device_pixel_ratio(const struct kwindow* window);

b8 platform_dynamic_library_load(const char* name, dynamic_library* out_library);

b8 platform_dynamic_library_unload(dynamic_library* library);

void* platform_dynamic_library_load_function(const char* name, dynamic_library* library);

const char* platform_dynamic_library_extension(void);

const char* platform_dynamic_library_prefix(void);

platform_error_code platform_copy_file(const char* source, const char* dest, b8 overwrite_if_exists);

void platform_register_watcher_deleted_callback(platform_filewatcher_file_deleted_callback callback);

void platform_register_watcher_written_callback(platform_filewatcher_file_written_callback callback);

void platform_register_window_closed_callback(platform_window_closed_callback callback);

void platform_register_window_resized_callback(platform_window_resized_callback callback);

void platform_register_process_key(platform_process_key callback);

void platform_register_process_mouse_button_callback(platform_process_mouse_button callback);

void platform_register_process_mouse_move_callback(platform_process_mouse_move callback);

void platform_register_process_mouse_wheel_callback(platform_process_mouse_wheel callback);

b8 platform_watch_file(const char* file_path, u32* out_watch_id);

b8 platform_unwatch_file(u32 watch_id);

#endif
