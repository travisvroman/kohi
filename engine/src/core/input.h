#pragma once

#include "defines.h"

typedef enum buttons {
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_MIDDLE,
    BUTTON_MAX_BUTTONS
} buttons;

#define DEFINE_KEY(name, code) KEY_##name = code

typedef enum keys {
#if KPLATFORM_WINDOWS
    DEFINE_KEY(BACKSPACE, 0x08),
    DEFINE_KEY(ENTER, 0x0D),
    DEFINE_KEY(TAB, 0x09),
    DEFINE_KEY(SHIFT, 0x10),
    DEFINE_KEY(CONTROL, 0x11),

    DEFINE_KEY(PAUSE, 0x13),
    DEFINE_KEY(CAPITAL, 0x14),

    DEFINE_KEY(ESCAPE, 0x1B),

    DEFINE_KEY(CONVERT, 0x1C),
    DEFINE_KEY(NONCONVERT, 0x1D),
    DEFINE_KEY(ACCEPT, 0x1E),
    DEFINE_KEY(MODECHANGE, 0x1F),

    DEFINE_KEY(SPACE, 0x20),
    DEFINE_KEY(PRIOR, 0x21),
    DEFINE_KEY(NEXT, 0x22),
    DEFINE_KEY(END, 0x23),
    DEFINE_KEY(HOME, 0x24),
    DEFINE_KEY(LEFT, 0x25),
    DEFINE_KEY(UP, 0x26),
    DEFINE_KEY(RIGHT, 0x27),
    DEFINE_KEY(DOWN, 0x28),
    DEFINE_KEY(SELECT, 0x29),
    DEFINE_KEY(PRINT, 0x2A),
    DEFINE_KEY(EXECUTE, 0x2B),
    DEFINE_KEY(SNAPSHOT, 0x2C),
    DEFINE_KEY(INSERT, 0x2D),
    DEFINE_KEY(DELETE, 0x2E),
    DEFINE_KEY(HELP, 0x2F),

    DEFINE_KEY(A, 0x41),
    DEFINE_KEY(B, 0x42),
    DEFINE_KEY(C, 0x43),
    DEFINE_KEY(D, 0x44),
    DEFINE_KEY(E, 0x45),
    DEFINE_KEY(F, 0x46),
    DEFINE_KEY(G, 0x47),
    DEFINE_KEY(H, 0x48),
    DEFINE_KEY(I, 0x49),
    DEFINE_KEY(J, 0x4A),
    DEFINE_KEY(K, 0x4B),
    DEFINE_KEY(L, 0x4C),
    DEFINE_KEY(M, 0x4D),
    DEFINE_KEY(N, 0x4E),
    DEFINE_KEY(O, 0x4F),
    DEFINE_KEY(P, 0x50),
    DEFINE_KEY(Q, 0x51),
    DEFINE_KEY(R, 0x52),
    DEFINE_KEY(S, 0x53),
    DEFINE_KEY(T, 0x54),
    DEFINE_KEY(U, 0x55),
    DEFINE_KEY(V, 0x56),
    DEFINE_KEY(W, 0x57),
    DEFINE_KEY(X, 0x58),
    DEFINE_KEY(Y, 0x59),
    DEFINE_KEY(Z, 0x5A),

    DEFINE_KEY(LWIN, 0x5B),
    DEFINE_KEY(RWIN, 0x5C),
    DEFINE_KEY(APPS, 0x5D),

    DEFINE_KEY(SLEEP, 0x5F),

    DEFINE_KEY(NUMPAD0, 0x60),
    DEFINE_KEY(NUMPAD1, 0x61),
    DEFINE_KEY(NUMPAD2, 0x62),
    DEFINE_KEY(NUMPAD3, 0x63),
    DEFINE_KEY(NUMPAD4, 0x64),
    DEFINE_KEY(NUMPAD5, 0x65),
    DEFINE_KEY(NUMPAD6, 0x66),
    DEFINE_KEY(NUMPAD7, 0x67),
    DEFINE_KEY(NUMPAD8, 0x68),
    DEFINE_KEY(NUMPAD9, 0x69),
    DEFINE_KEY(MULTIPLY, 0x6A),
    DEFINE_KEY(ADD, 0x6B),
    DEFINE_KEY(SEPARATOR, 0x6C),
    DEFINE_KEY(SUBTRACT, 0x6D),
    DEFINE_KEY(DECIMAL, 0x6E),
    DEFINE_KEY(DIVIDE, 0x6F),
    DEFINE_KEY(F1, 0x70),
    DEFINE_KEY(F2, 0x71),
    DEFINE_KEY(F3, 0x72),
    DEFINE_KEY(F4, 0x73),
    DEFINE_KEY(F5, 0x74),
    DEFINE_KEY(F6, 0x75),
    DEFINE_KEY(F7, 0x76),
    DEFINE_KEY(F8, 0x77),
    DEFINE_KEY(F9, 0x78),
    DEFINE_KEY(F10, 0x79),
    DEFINE_KEY(F11, 0x7A),
    DEFINE_KEY(F12, 0x7B),
    DEFINE_KEY(F13, 0x7C),
    DEFINE_KEY(F14, 0x7D),
    DEFINE_KEY(F15, 0x7E),
    DEFINE_KEY(F16, 0x7F),
    DEFINE_KEY(F17, 0x80),
    DEFINE_KEY(F18, 0x81),
    DEFINE_KEY(F19, 0x82),
    DEFINE_KEY(F20, 0x83),
    DEFINE_KEY(F21, 0x84),
    DEFINE_KEY(F22, 0x85),
    DEFINE_KEY(F23, 0x86),
    DEFINE_KEY(F24, 0x87),

    DEFINE_KEY(NUMLOCK, 0x90),
    DEFINE_KEY(SCROLL, 0x91),

    DEFINE_KEY(NUMPAD_EQUAL, 0x92),

    DEFINE_KEY(LSHIFT, 0xA0),
    DEFINE_KEY(RSHIFT, 0xA1),
    DEFINE_KEY(LCONTROL, 0xA2),
    DEFINE_KEY(RCONTROL, 0xA3),
    DEFINE_KEY(LALT, 0xA4),
    DEFINE_KEY(RALT, 0xA5),

    DEFINE_KEY(SEMICOLON, 0xBA),
    DEFINE_KEY(PLUS, 0xBB),
    DEFINE_KEY(COMMA, 0xBC),
    DEFINE_KEY(MINUS, 0xBD),
    DEFINE_KEY(PERIOD, 0xBE),
    DEFINE_KEY(SLASH, 0xBF),
    DEFINE_KEY(GRAVE, 0xC0),
#elif KPLATFORM_LINUX
    // these are the keysyms but with different keys for 'A' and 'a'
    DEFINE_KEY(SPACE, 0x0020), // 00032
    DEFINE_KEY(HASH, 0x0023), // 00035
    DEFINE_KEY(APOS, 0x0027), // 00039
    DEFINE_KEY(COMMA, 0x002c), // 00044
    DEFINE_KEY(MINUS, 0x002d), // 00045
    DEFINE_KEY(PERIOD, 0x002e), // 00046
    DEFINE_KEY(SLASH, 0x002f), // 00047
    DEFINE_KEY(0, 0x0030), // 00048
    DEFINE_KEY(1, 0x0031), // 00049
    DEFINE_KEY(2, 0x0032), // 00050
    DEFINE_KEY(3, 0x0033), // 00051
    DEFINE_KEY(4, 0x0034), // 00052
    DEFINE_KEY(5, 0x0035), // 00053
    DEFINE_KEY(6, 0x0036), // 00054
    DEFINE_KEY(7, 0x0037), // 00055
    DEFINE_KEY(8, 0x0038), // 00056
    DEFINE_KEY(9, 0x0039), // 00057
    DEFINE_KEY(SEMICOLON, 0x003b), // 00059
    DEFINE_KEY(EQUAL, 0x003d), // 00061
    DEFINE_KEY(A, 0x0041), // 00065
    DEFINE_KEY(B, 0x0042), // 00066
    DEFINE_KEY(C, 0x0043), // 00067
    DEFINE_KEY(D, 0x0044), // 00068
    DEFINE_KEY(E, 0x0045), // 00069
    DEFINE_KEY(F, 0x0046), // 00070
    DEFINE_KEY(G, 0x0047), // 00071
    DEFINE_KEY(H, 0x0048), // 00072
    DEFINE_KEY(I, 0x0049), // 00073
    DEFINE_KEY(J, 0x004a), // 00074
    DEFINE_KEY(K, 0x004b), // 00075
    DEFINE_KEY(L, 0x004c), // 00076
    DEFINE_KEY(M, 0x004d), // 00077
    DEFINE_KEY(N, 0x004e), // 00078
    DEFINE_KEY(O, 0x004f), // 00079
    DEFINE_KEY(P, 0x0050), // 00080
    DEFINE_KEY(Q, 0x0051), // 00081
    DEFINE_KEY(R, 0x0052), // 00082
    DEFINE_KEY(S, 0x0053), // 00083
    DEFINE_KEY(T, 0x0054), // 00084
    DEFINE_KEY(U, 0x0055), // 00085
    DEFINE_KEY(V, 0x0056), // 00086
    DEFINE_KEY(W, 0x0057), // 00087
    DEFINE_KEY(X, 0x0058), // 00088
    DEFINE_KEY(Y, 0x0059), // 00089
    DEFINE_KEY(Z, 0x005a), // 00090
    DEFINE_KEY(LBRACKET, 0x005b), // 00091
    DEFINE_KEY(BSLASH, 0x005c), // 00092
    DEFINE_KEY(RBRACKET, 0x005d), // 00093
    DEFINE_KEY(GRAVE, 0x0060), // 00094
    DEFINE_KEY(a, 0x0061), // 00095
    DEFINE_KEY(b, 0x0062), // 00096
    DEFINE_KEY(c, 0x0063), // 00097
    DEFINE_KEY(d, 0x0064), // 00098
    DEFINE_KEY(e, 0x0065), // 00099
    DEFINE_KEY(f, 0x0066), // 00100
    DEFINE_KEY(g, 0x0067), // 00101
    DEFINE_KEY(h, 0x0068), // 00102
    DEFINE_KEY(i, 0x0069), // 00103
    DEFINE_KEY(j, 0x006a), // 00104
    DEFINE_KEY(k, 0x006b), // 00105
    DEFINE_KEY(l, 0x006c), // 00106
    DEFINE_KEY(m, 0x006d), // 00107
    DEFINE_KEY(n, 0x006e), // 00108
    DEFINE_KEY(o, 0x006f), // 00109
    DEFINE_KEY(p, 0x0070), // 00110
    DEFINE_KEY(q, 0x0071), // 00111
    DEFINE_KEY(r, 0x0072), // 00112
    DEFINE_KEY(s, 0x0073), // 00113
    DEFINE_KEY(t, 0x0074), // 00114
    DEFINE_KEY(u, 0x0075), // 00115
    DEFINE_KEY(v, 0x0076), // 00116
    DEFINE_KEY(w, 0x0077), // 00117
    DEFINE_KEY(x, 0x0078), // 00118
    DEFINE_KEY(y, 0x0079), // 00119
    DEFINE_KEY(z, 0x007a), // 00120
    DEFINE_KEY(BACKSPACE, 0xff08), //65288
    DEFINE_KEY(TAB, 0xff09), // 65289
    DEFINE_KEY(ENTER, 0xff0d), // 65293
    DEFINE_KEY(PAUSE, 0xff13), // 65299
    DEFINE_KEY(SCROLL, 0xff14), // 65300
    DEFINE_KEY(ESCAPE, 0xff1b), // 65307
    DEFINE_KEY(HOME, 0xff50), //65360
    DEFINE_KEY(LEFT, 0xff51), // 65361
    DEFINE_KEY(UP, 0xff52), // 65362
    DEFINE_KEY(RIGHT, 0xff53), // 65363
    DEFINE_KEY(DOWN, 0xff54), // 65364
    DEFINE_KEY(PAGEUP, 0xff55), // 65365
    DEFINE_KEY(PAGEDOWN, 0xff56), // 65366
    DEFINE_KEY(END, 0xff57), // 65367
    DEFINE_KEY(PRINT, 0xff61), // 65377
    DEFINE_KEY(INSERT, 0xff63), // 65379
    DEFINE_KEY(NUMLOCK, 0xff7f), // 65407
    DEFINE_KEY(KP_ENTER, 0xff8d), // 65421
    DEFINE_KEY(KP_7, 0xff95), // 65429
    DEFINE_KEY(KP_4, 0xff96), // 65430
    DEFINE_KEY(KP_8, 0xff97), // 65431
    DEFINE_KEY(KP_6, 0xff98), // 65432
    DEFINE_KEY(KP_2, 0xff99), // 65433
    DEFINE_KEY(KP_9, 0xff9a), // 65434
    DEFINE_KEY(KP_3, 0xff9b), // 65435
    DEFINE_KEY(KP_1, 0xff9c), // 65436
    DEFINE_KEY(KP_5, 0xff9d), // 65437
    DEFINE_KEY(KP_0, 0xff9e), // 65438
    DEFINE_KEY(KP_DECIMAL, 0xff9f ), // 65439
    DEFINE_KEY(KP_MULTIPLY, 0xffaa), // 65450
    DEFINE_KEY(KP_ADD, 0xffab), // 65451
    DEFINE_KEY(KP_SUBTRACT, 0xffad), // 65453
    DEFINE_KEY(KP_DIVIDE, 0xffaf), // 65455
    DEFINE_KEY(F1, 0xffbe), // 65470
    DEFINE_KEY(F2, 0xffbf), // 65471
    DEFINE_KEY(F3, 0xffc0), // 65472
    DEFINE_KEY(F4, 0xffc1), // 65473
    DEFINE_KEY(F5, 0xffc2), // 65474
    DEFINE_KEY(F6, 0xffc3), // 65475
    DEFINE_KEY(F7, 0xffc4), // 65476
    DEFINE_KEY(F8, 0xffc5), // 65477
    DEFINE_KEY(F9, 0xffc6), // 65478
    DEFINE_KEY(F10, 0xffc7), // 65479
    DEFINE_KEY(F11, 0xffc8), // 65480
    DEFINE_KEY(F12, 0xffc9), // 65481
    DEFINE_KEY(LSHIFT, 0xffe1), // 65505
    DEFINE_KEY(RSHIFT, 0xffe2), // 65506
    DEFINE_KEY(LCONTROL, 0xffe3), // 65507
    DEFINE_KEY(RCONTROL, 0xffe4), // 65508
    DEFINE_KEY(CAPSLOCK, 0xffe5), // 65509
    DEFINE_KEY(LALT, 0xffe9), // 65513
    DEFINE_KEY(RALT, 0xfe03), // 65027
    DEFINE_KEY(DELETE, 0xffff), // 65535
#endif
    KEYS_MAX_KEYS
} keys;

/**
 * @brief Initializes the input system. Call twice; once to obtain memory requirement (passing
 * state = 0), then a second time passing allocated memory to state.
 * 
 * @param memory_requirement The required size of the state memory.
 * @param state Either 0 or the allocated block of state memory.
 */
void input_system_initialize(u64* memory_requirement, void* state);
void input_system_shutdown(void* state);
void input_update(f64 delta_time);

// keyboard input
KAPI b8 input_is_key_down(keys key);
KAPI b8 input_is_key_up(keys key);
KAPI b8 input_was_key_down(keys key);
KAPI b8 input_was_key_up(keys key);

void input_process_key(keys key, b8 pressed);

// mouse input
KAPI b8 input_is_button_down(buttons button);
KAPI b8 input_is_button_up(buttons button);
KAPI b8 input_was_button_down(buttons button);
KAPI b8 input_was_button_up(buttons button);
KAPI void input_get_mouse_position(i32* x, i32* y);
KAPI void input_get_previous_mouse_position(i32* x, i32* y);

void input_process_button(buttons button, b8 pressed);
void input_process_mouse_move(i16 x, i16 y);
void input_process_mouse_wheel(i8 z_delta);

/* // this should use xcb_keycode_t instead of xcb_keysym_t
//  but are not properly tested in various locales
DEFINE_KEY(KeyEscape, 9),
DEFINE_KEY(Key_1, 10),
DEFINE_KEY(Key_2, 11),
DEFINE_KEY(Key_3, 12),
DEFINE_KEY(Key_4, 13),
DEFINE_KEY(Key_5, 14),
DEFINE_KEY(Key_6, 15),
DEFINE_KEY(Key_7, 16),
DEFINE_KEY(Key_8, 17),
DEFINE_KEY(Key_9, 18),
DEFINE_KEY(Key_0, 19),
DEFINE_KEY(KeyMinus, 20),
DEFINE_KEY(KeyEqual, 21),
DEFINE_KEY(KeyBackspace, 22),
DEFINE_KEY(KeyTab, 23),
DEFINE_KEY(Key_Q, 24),
DEFINE_KEY(Key_W, 25),
DEFINE_KEY(Key_E, 26),
DEFINE_KEY(Key_R, 27),
DEFINE_KEY(Key_T, 28),
DEFINE_KEY(Key_Y, 29),
DEFINE_KEY(Key_U, 30),
DEFINE_KEY(Key_I, 31),
DEFINE_KEY(Key_O, 32),
DEFINE_KEY(Key_P, 33),
DEFINE_KEY(KeyLeftBracket, 34),
DEFINE_KEY(KeyRightBracket, 35),
DEFINE_KEY(KeyEnter, 36),
DEFINE_KEY(KeyLeftControl, 37),
DEFINE_KEY(Key_A, 38),
DEFINE_KEY(Key_S, 39),
DEFINE_KEY(Key_D, 40),
DEFINE_KEY(Key_F, 41),
DEFINE_KEY(Key_G, 42),
DEFINE_KEY(Key_H, 43),
DEFINE_KEY(Key_J, 44),
DEFINE_KEY(Key_K, 45),
DEFINE_KEY(Key_L, 46),
DEFINE_KEY(KeySemiColon, 47),
DEFINE_KEY(KeyApostrophe, 48),
DEFINE_KEY(KeyGraveAccent, 49),
DEFINE_KEY(KeyLeftShift, 50),
DEFINE_KEY(KeyHashTag, 51),
DEFINE_KEY(Key_Z, 52),
DEFINE_KEY(Key_X, 53),
DEFINE_KEY(Key_C, 54),
DEFINE_KEY(Key_V, 55),
DEFINE_KEY(Key_B, 56),
DEFINE_KEY(Key_N, 57),
DEFINE_KEY(Key_M, 58),
DEFINE_KEY(KeyComma, 59),
DEFINE_KEY(KeyPeriod, 60),
DEFINE_KEY(KeySlash, 61),
DEFINE_KEY(KeyRightShift, 62),
DEFINE_KEY(KeyKPMultiply, 63),
DEFINE_KEY(KeyLeftAlt, 64),
DEFINE_KEY(KeySpace, 65),
DEFINE_KEY(KeyCapsLock, 66),
DEFINE_KEY(KeyF1, 67),
DEFINE_KEY(KeyF2, 68),
DEFINE_KEY(KeyF3, 69),
DEFINE_KEY(KeyF4, 70),
DEFINE_KEY(KeyF5, 71),
DEFINE_KEY(KeyF6, 72),
DEFINE_KEY(KeyF7, 73),
DEFINE_KEY(KeyF8, 74),
DEFINE_KEY(KeyF9, 75),
DEFINE_KEY(KeyF10, 76),
DEFINE_KEY(KeyNumLock, 77),
DEFINE_KEY(KeyScrollLock, 78),
DEFINE_KEY(KeyKP7, 79),
DEFINE_KEY(KeyKP8, 80),
DEFINE_KEY(KeyKP9, 81),
DEFINE_KEY(KeyKPSubtract, 82),
DEFINE_KEY(KeyKP4, 83),
DEFINE_KEY(KeyKP5, 84),
DEFINE_KEY(KeyKP6, 85),
DEFINE_KEY(KeyKPAdd, 86),
DEFINE_KEY(KeyKP1, 87),
DEFINE_KEY(KeyKP2, 88),
DEFINE_KEY(KeyKP3, 89),
DEFINE_KEY(KeyKP0, 90),
DEFINE_KEY(KeyKPDecimal, 91),
DEFINE_KEY(KeyBackSlash, 94),
DEFINE_KEY(KeyF11, 95),
DEFINE_KEY(KeyF12, 96),
DEFINE_KEY(KeyKPEnter, 104),
DEFINE_KEY(KeyRightControl, 105),
DEFINE_KEY(KeyKPDivide, 106),
DEFINE_KEY(KeyPrintScreen, 107),
DEFINE_KEY(KeyRightAlt, 108),
DEFINE_KEY(KeyHome, 110),
DEFINE_KEY(KeyUp, 111),
DEFINE_KEY(KeyPageUp, 112),
DEFINE_KEY(KeyLeft, 113),
DEFINE_KEY(KeyRight, 114),
DEFINE_KEY(KeyEnd, 115),
DEFINE_KEY(KeyDown, 116),
DEFINE_KEY(KeyPageDown=117),
DEFINE_KEY(KeyInsert, 118),
DEFINE_KEY(KeyDelete, 119),
DEFINE_KEY(KeyPause, 127),
*/