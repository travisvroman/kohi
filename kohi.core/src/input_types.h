/**
 * @file input_types.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief Defines core types used for input.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include "defines.h"

typedef enum input_device_type {
	INPUT_DEVICE_TYPE_UNKNOWN,
	INPUT_DEVICE_TYPE_KEYBOARD,
	INPUT_DEVICE_TYPE_MOUSE,
	INPUT_DEVICE_TYPE_HID,
	INPUT_DEVICE_TYPE_GAMEPAD,
} input_device_type;

/**
 * @brief Represents available mouse buttons.
 */
typedef enum mouse_buttons {
	/** @brief The left mouse button */
	MOUSE_BUTTON_LEFT,
	/** @brief The right mouse button */
	MOUSE_BUTTON_RIGHT,
	/** @brief The middle mouse button (typically the wheel) */
	MOUSE_BUTTON_MIDDLE,
	MOUSE_BUTTON_MAX
} mouse_buttons;

/**
 * @brief Represents available keyboard keys.
 */
typedef enum keys {
	/** @brief The backspace key. */
	K_KEY_BACKSPACE = 0x08,
	/** @brief The enter key. */
	K_KEY_ENTER = 0x0D,
	/** @brief The tab key. */
	K_KEY_TAB = 0x09,
	/** @brief The shift key. */
	K_KEY_SHIFT = 0x10,
	/** @brief The Control/Ctrl key. */
	K_KEY_CONTROL = 0x11,

	/** @brief The pause key. */
	K_KEY_PAUSE = 0x13,
	/** @brief The Caps Lock key. */
	K_KEY_CAPITAL = 0x14,

	/** @brief The Escape key. */
	K_KEY_ESCAPE = 0x1B,

	K_KEY_CONVERT = 0x1C,
	K_KEY_NONCONVERT = 0x1D,
	K_KEY_ACCEPT = 0x1E,
	K_KEY_MODECHANGE = 0x1F,

	/** @brief The spacebar key. */
	K_KEY_SPACE = 0x20,
	/** @brief The page up key. */
	K_KEY_PAGEUP = 0x21,
	/** @brief The page down key. */
	K_KEY_PAGEDOWN = 0x22,
	/** @brief The end key. */
	K_KEY_END = 0x23,
	/** @brief The home key. */
	K_KEY_HOME = 0x24,
	/** @brief The left arrow key. */
	K_KEY_LEFT = 0x25,
	/** @brief The up arrow key. */
	K_KEY_UP = 0x26,
	/** @brief The right arrow key. */
	K_KEY_RIGHT = 0x27,
	/** @brief The down arrow key. */
	K_KEY_DOWN = 0x28,
	K_KEY_SELECT = 0x29,
	K_KEY_PRINT = 0x2A,
	K_KEY_EXECUTE = 0x2B,
	/** @brief The Print Screen key. */
	K_KEY_PRINTSCREEN = 0x2C,
	/** @brief The insert key. */
	K_KEY_INSERT = 0x2D,
	/** @brief The delete key. */
	K_KEY_DELETE = 0x2E,
	K_KEY_HELP = 0x2F,

	/** @brief The 0 key */
	K_KEY_0 = 0x30,
	/** @brief The 1 key */
	K_KEY_1 = 0x31,
	/** @brief The 2 key */
	K_KEY_2 = 0x32,
	/** @brief The 3 key */
	K_KEY_3 = 0x33,
	/** @brief The 4 key */
	K_KEY_4 = 0x34,
	/** @brief The 5 key */
	K_KEY_5 = 0x35,
	/** @brief The 6 key */
	K_KEY_6 = 0x36,
	/** @brief The 7 key */
	K_KEY_7 = 0x37,
	/** @brief The 8 key */
	K_KEY_8 = 0x38,
	/** @brief The 9 key */
	K_KEY_9 = 0x39,

	/** @brief The A key. */
	K_KEY_A = 0x41,
	/** @brief The B key. */
	K_KEY_B = 0x42,
	/** @brief The C key. */
	K_KEY_C = 0x43,
	/** @brief The D key. */
	K_KEY_D = 0x44,
	/** @brief The E key. */
	K_KEY_E = 0x45,
	/** @brief The F key. */
	K_KEY_F = 0x46,
	/** @brief The G key. */
	K_KEY_G = 0x47,
	/** @brief The H key. */
	K_KEY_H = 0x48,
	/** @brief The I key. */
	K_KEY_I = 0x49,
	/** @brief The J key. */
	K_KEY_J = 0x4A,
	/** @brief The K key. */
	K_KEY_K = 0x4B,
	/** @brief The L key. */
	K_KEY_L = 0x4C,
	/** @brief The M key. */
	K_KEY_M = 0x4D,
	/** @brief The N key. */
	K_KEY_N = 0x4E,
	/** @brief The O key. */
	K_KEY_O = 0x4F,
	/** @brief The P key. */
	K_KEY_P = 0x50,
	/** @brief The Q key. */
	K_KEY_Q = 0x51,
	/** @brief The R key. */
	K_KEY_R = 0x52,
	/** @brief The S key. */
	K_KEY_S = 0x53,
	/** @brief The T key. */
	K_KEY_T = 0x54,
	/** @brief The U key. */
	K_KEY_U = 0x55,
	/** @brief The V key. */
	K_KEY_V = 0x56,
	/** @brief The W key. */
	K_KEY_W = 0x57,
	/** @brief The X key. */
	K_KEY_X = 0x58,
	/** @brief The Y key. */
	K_KEY_Y = 0x59,
	/** @brief The Z key. */
	K_KEY_Z = 0x5A,

	/** @brief The left Windows/Super key. */
	K_KEY_LSUPER = 0x5B,
	/** @brief The right Windows/Super key. */
	K_KEY_RSUPER = 0x5C,
	/** @brief The applicatons key. */
	K_KEY_APPS = 0x5D,

	/** @brief The sleep key. */
	K_KEY_SLEEP = 0x5F,

	/** @brief The numberpad 0 key. */
	K_KEY_NUMPAD0 = 0x60,
	/** @brief The numberpad 1 key. */
	K_KEY_NUMPAD1 = 0x61,
	/** @brief The numberpad 2 key. */
	K_KEY_NUMPAD2 = 0x62,
	/** @brief The numberpad 3 key. */
	K_KEY_NUMPAD3 = 0x63,
	/** @brief The numberpad 4 key. */
	K_KEY_NUMPAD4 = 0x64,
	/** @brief The numberpad 5 key. */
	K_KEY_NUMPAD5 = 0x65,
	/** @brief The numberpad 6 key. */
	K_KEY_NUMPAD6 = 0x66,
	/** @brief The numberpad 7 key. */
	K_KEY_NUMPAD7 = 0x67,
	/** @brief The numberpad 8 key. */
	K_KEY_NUMPAD8 = 0x68,
	/** @brief The numberpad 9 key. */
	K_KEY_NUMPAD9 = 0x69,
	/** @brief The numberpad multiply key. */
	K_KEY_MULTIPLY = 0x6A,
	/** @brief The numberpad add key. */
	K_KEY_ADD = 0x6B,
	/** @brief The numberpad separator key. */
	K_KEY_SEPARATOR = 0x6C,
	/** @brief The numberpad subtract key. */
	K_KEY_SUBTRACT = 0x6D,
	/** @brief The numberpad decimal key. */
	K_KEY_DECIMAL = 0x6E,
	/** @brief The numberpad divide key. */
	K_KEY_DIVIDE = 0x6F,

	/** @brief The F1 key. */
	K_KEY_F1 = 0x70,
	/** @brief The F2 key. */
	K_KEY_F2 = 0x71,
	/** @brief The F3 key. */
	K_KEY_F3 = 0x72,
	/** @brief The F4 key. */
	K_KEY_F4 = 0x73,
	/** @brief The F5 key. */
	K_KEY_F5 = 0x74,
	/** @brief The F6 key. */
	K_KEY_F6 = 0x75,
	/** @brief The F7 key. */
	K_KEY_F7 = 0x76,
	/** @brief The F8 key. */
	K_KEY_F8 = 0x77,
	/** @brief The F9 key. */
	K_KEY_F9 = 0x78,
	/** @brief The F10 key. */
	K_KEY_F10 = 0x79,
	/** @brief The F11 key. */
	K_KEY_F11 = 0x7A,
	/** @brief The F12 key. */
	K_KEY_F12 = 0x7B,
	/** @brief The F13 key. */
	K_KEY_F13 = 0x7C,
	/** @brief The F14 key. */
	K_KEY_F14 = 0x7D,
	/** @brief The F15 key. */
	K_KEY_F15 = 0x7E,
	/** @brief The F16 key. */
	K_KEY_F16 = 0x7F,
	/** @brief The F17 key. */
	K_KEY_F17 = 0x80,
	/** @brief The F18 key. */
	K_KEY_F18 = 0x81,
	/** @brief The F19 key. */
	K_KEY_F19 = 0x82,
	/** @brief The F20 key. */
	K_KEY_F20 = 0x83,
	/** @brief The F21 key. */
	K_KEY_F21 = 0x84,
	/** @brief The F22 key. */
	K_KEY_F22 = 0x85,
	/** @brief The F23 key. */
	K_KEY_F23 = 0x86,
	/** @brief The F24 key. */
	K_KEY_F24 = 0x87,

	/** @brief The number lock key. */
	K_KEY_NUMLOCK = 0x90,

	/** @brief The scroll lock key. */
	K_KEY_SCROLL = 0x91,

	/** @brief The numberpad equal key. */
	K_KEY_NUMPAD_EQUAL = 0x92,

	/** @brief The left shift key. */
	K_KEY_LSHIFT = 0xA0,
	/** @brief The right shift key. */
	K_KEY_RSHIFT = 0xA1,
	/** @brief The left control key. */
	K_KEY_LCONTROL = 0xA2,
	/** @brief The right control key. */
	K_KEY_RCONTROL = 0xA3,
	/** @brief The left alt key. */
	K_KEY_LALT = 0xA4,
	/** @brief The right alt key. */
	K_KEY_RALT = 0xA5,

	/** @brief The semicolon key. */
	K_KEY_SEMICOLON = 0x3B,

	/** @brief The apostrophe/single-quote key */
	K_KEY_APOSTROPHE = 0xDE,
	/** @brief An alias for K_KEY_APOSTROPHE, apostrophe/single-quote key */
	K_KEY_QUOTE = K_KEY_APOSTROPHE,
	/** @brief The equal/plus key. */
	K_KEY_EQUAL = 0xBB,
	/** @brief The comma key. */
	K_KEY_COMMA = 0xBC,
	/** @brief The minus key. */
	K_KEY_MINUS = 0xBD,
	/** @brief The period key. */
	K_KEY_PERIOD = 0xBE,
	/** @brief The slash key. */
	K_KEY_SLASH = 0xBF,

	/** @brief The grave key. */
	K_KEY_GRAVE = 0xC0,

	/** @brief The left (square) bracket key e.g. [{ */
	K_KEY_LBRACKET = 0xDB,
	/** @brief The pipe/backslash key */
	K_KEY_PIPE = 0xDC,
	/** @brief An alias for the pipe/backslash key */
	K_KEY_BACKSLASH = K_KEY_PIPE,
	/** @brief The right (square) bracket key e.g. ]} */
	K_KEY_RBRACKET = 0xDD,

	KEYS_MAX_KEYS = 0xFF
} keys;

// Total number of gamepads supported. Any more connected than this
// will be ignored.
#define MAX_GAMEPADS 4

#define GAMEPAD_MAX_AXES 64
#define GAMEPAD_MAX_BUTTONS 64

typedef enum usb_vendor_id {
	USB_VENDOR_ID_MICROSOFT = 0x045E,
	USB_VENDOR_ID_SONY = 0x054C,
	USB_VENDOR_ID_NINTENDO = 0x057E,

} usb_vendor_id;

typedef enum gamepad_product_id {
	// Microsoft Xbox Controller
	GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER = 0x0202,
	// Microsoft Xbox Controller S
	GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_0 = 0x0285,
	// Microsoft Xbox Controller S
	GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_1 = 0x0289,
	// Microsoft Xbox 360 Controller
	GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER = 0x028E,
	// Microsoft Xbox 360 Wireless Controller via Plug & Charge Cable
	GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER_PACC = 0x028F,
	// Microsoft Xbox One Controller
	GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER = 0x02D1,
	// Microsoft Xbox One Controller (Firmware 2015)
	GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_2015 = 0x02DD,
	// Microsoft Xbox One Wireless Controller
	GAMEPAD_PRODUCT_ID_XBOX_ONE_WIRELESS_CONTROLLER = 0x02E0,
	// Microsoft Xbox One Elite Controller
	GAMEPAD_PRODUCT_ID_XBOX_ONE_ELITE_CONTROLLER = 0x02E3,
	// Microsoft Xbox One Controller
	GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_1 = 0x02EA,
	// Microsoft Xbox One S Controller [Bluetooth]
	GAMEPAD_PRODUCT_ID_XBOX_ONE_S_CONTROLLER = 0x02FD,
	// Microsoft Xbox One/Series X|S Xinput-compatible Controller
	GAMEPAD_PRODUCT_ID_XBOX_ONE_SXS_CONTROLLER = 0x02FF,
	// Microsoft Xbox Elite Series 2 Controller (Model 1797)
	GAMEPAD_PRODUCT_ID_XBOX_ELITE_SERIES_2_CONTROLLER = 0x0B00,
	// Microsoft Xbox Controller
	GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_1 = 0x0B12,

	// Sony Playstation Classic Controller
	GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER = 0x0CDA,
	// Sony Playstation 3 Controller
	GAMEPAD_PRODUCT_ID_PS3_CONTROLLER = 0x0268,
	// Sony DualShock 4 [CUH-ZCT1x] (Playstation 4 Controller)
	GAMEPAD_PRODUCT_ID_DUALSHOCK4_1 = 0x05C4,
	// Sony DualShock 4 [CUH-ZCT2x] (Playstation 4 Controller)
	GAMEPAD_PRODUCT_ID_DUALSHOCK4_2 = 0x09CC,
	// Sony Dualshock4 Wireless Adaptor
	GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER = 0x0BA0,
	// Sony DualSense Wireless Controller (Playstation 5 Controller)
	GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER = 0x0CE6,
	// Sony DualSense Wireless Edge Controller (Playstation 5 Controller)
	GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER = 0x0DF2,

	// Broadcom BCM2045A Bluetooth Radio [Nintendo Wii/Wii U]
	GAMEPAD_PRODUCT_ID_NINTENDO_WII_WIIU = 0x305,
	// Nintendo Wii Remote Controller RVL-003
	GAMEPAD_PRODUCT_ID_WII_REMOTE_RVL_003 = 0x306,
	// Nintendo Wii U GameCube Controller Adapter
	GAMEPAD_PRODUCT_ID_WIIU_GAMECUBE_CONTROLLER_ADAPTER = 0x337,
	// Nintendo DRH GamePad Host [Nintendo Wii U]
	GAMEPAD_PRODUCT_ID_WIIU_GAMEPAD = 0x341,
	// Nintendo Joy-Con L
	GAMEPAD_PRODUCT_ID_JOYCON_L = 0x2006,
	// Nintendo Joy-Con R
	GAMEPAD_PRODUCT_ID_JOYCON_R = 0x2007,
	// Nintendo Switch Pro Controller
	GAMEPAD_PRODUCT_ID_SWITCH_PRO = 0x2009

} gamepad_product_id;

typedef enum gamepad_axis {
	GAMEPAD_AXIS_LX,
	GAMEPAD_AXIS_LY,
	GAMEPAD_AXIS_LZ,
	GAMEPAD_AXIS_RX,
	GAMEPAD_AXIS_RY,
	GAMEPAD_AXIS_RZ,

	// Axes for left and right Z are combined.
	GAMEPAD_AXIS_LRZ,

	GAMEPAD_AXIS_DPAD_LR,
	GAMEPAD_AXIS_DPAD_UD,

	// Axis whose value should be ignored.
	GAMEPAD_AXIS_IGNORE,
	// Unknown axis, but still obtain its value anyway.
	GAMEPAD_AXIS_UNKNOWN,

	GAMEPAD_AXIS_ADDL0,
	GAMEPAD_AXIS_ADDL1,
	GAMEPAD_AXIS_ADDL2,
	GAMEPAD_AXIS_ADDL3,
	GAMEPAD_AXIS_ADDL4,
	GAMEPAD_AXIS_ADDL5,

} gamepad_axis;

typedef enum gamepad_button {
	// Unknown button, but still obtain its value anyway.
	GAMEPAD_BUTTON_UNKNOWN = 0x00000000,
	GAMEPAD_BUTTON_DPAD_LEFT = 0x00000001,
	GAMEPAD_BUTTON_DPAD_RIGHT = 0x00000002,
	GAMEPAD_BUTTON_DPAD_UP = 0x00000004,
	GAMEPAD_BUTTON_DPAD_DOWN = 0x00000008,
	GAMEPAD_BUTTON_L1 = 0x00000010,
	GAMEPAD_BUTTON_L2 = 0x00000020,
	GAMEPAD_BUTTON_L3 = 0x00000040,
	GAMEPAD_BUTTON_R1 = 0x00000080,
	GAMEPAD_BUTTON_R2 = 0x00000100,
	GAMEPAD_BUTTON_R3 = 0x00000200,
	// Xbox button, PS button, etc.
	GAMEPAD_BUTTON_OEM_HOME = 0x00000400,
	// Gamepad button in the "north" position (i.e. Y on Xbox, Triangle on Playstation)
	GAMEPAD_BUTTON_NORTH = 0x00000800,
	// Gamepad button in the "south" position (i.e. A on Xbox, Cross on Playstation)
	GAMEPAD_BUTTON_SOUTH = 0x00001000,
	// Gamepad button in the "east" position (i.e. B on Xbox, Circle on Playstation)
	GAMEPAD_BUTTON_EAST = 0x00002000,
	// Gamepad button in the "west" position (i.e. X on Xbox, Square on Playstation)
	GAMEPAD_BUTTON_WEST = 0x00004000,
	// Back on xbox 360, select/share/"\|/" on Playstation
	GAMEPAD_BUTTON_SELECT = 0x00008000,
	// Start on xbox 360, start/options/"burgermenu" on Playstation
	GAMEPAD_BUTTON_START = 0x00010000,

	GAMEPAD_BUTTON_ADDL0 = 0x00020000,
	GAMEPAD_BUTTON_ADDL1 = 0x00040000,
	GAMEPAD_BUTTON_ADDL2 = 0x00080000,
	GAMEPAD_BUTTON_ADDL3 = 0x00100000,
	GAMEPAD_BUTTON_ADDL4 = 0x00200000,
	GAMEPAD_BUTTON_ADDL5 = 0x00400000,

	// Completely ignore the button input.
	GAMEPAD_BUTTON_IGNORE = 0x7FFFFFFF,

} gamepad_button;
