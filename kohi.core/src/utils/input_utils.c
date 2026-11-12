#include "input_utils.h"
#include "input_types.h"

static const char *gamepad_get_axis_desc_microsoft (u16 product_id, gamepad_axis axis);
static const char *gamepad_get_axis_desc_sony (u16 product_id, gamepad_axis axis);
static const char *gamepad_get_axis_desc_nintendo (u16 product_id, gamepad_axis axis);
static const char *gamepad_get_axis_desc_default (u16 product_id, gamepad_axis axis);
static const char *gamepad_get_button_desc_microsoft (u16 product_id, gamepad_button button);
static const char *gamepad_get_button_desc_sony (u16 product_id, gamepad_button button);
static const char *gamepad_get_button_desc_nintendo (u16 product_id, gamepad_button button);
static const char *gamepad_get_button_desc_default (u16 product_id, gamepad_button button);

const char *util_gamepad_get_axis_desc (u16 vendor_id, u16 product_id, gamepad_axis axis) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_axis_desc_microsoft(product_id, axis);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_axis_desc_sony(product_id, axis);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_axis_desc_nintendo(product_id, axis);
	default:
		return gamepad_get_axis_desc_default(product_id, axis);
	}
}

const char *util_gamepad_get_button_desc (u16 vendor_id, u16 product_id, gamepad_button button) {
	switch (vendor_id) {
	case USB_VENDOR_ID_MICROSOFT:
		return gamepad_get_button_desc_microsoft(product_id, button);
	case USB_VENDOR_ID_SONY:
		return gamepad_get_button_desc_sony(product_id, button);
	case USB_VENDOR_ID_NINTENDO:
		return gamepad_get_button_desc_nintendo(product_id, button);
	default:
		return gamepad_get_button_desc_default(product_id, button);
	}
}

const char *gamepad_get_axis_desc_microsoft (u16 product_id, gamepad_axis axis) {
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
		switch (axis) {
		case GAMEPAD_AXIS_LX:
			return "XboxLThumbX";
		case GAMEPAD_AXIS_LY:
			return "XboxLThumbY";
		case GAMEPAD_AXIS_LZ:
			return "XboxLTrigger";
		case GAMEPAD_AXIS_RX:
			return "XboxRThumbX";
		case GAMEPAD_AXIS_RY:
			return "XboxRThumbY";
		case GAMEPAD_AXIS_RZ:
			return "XboxRTrigger";
		case GAMEPAD_AXIS_LRZ:
			return "XboxL&RTriggers";
		case GAMEPAD_AXIS_DPAD_LR:
			return "XboxDpadLR";
		case GAMEPAD_AXIS_DPAD_UD:
			return "XboxDpadUD";
		case GAMEPAD_AXIS_IGNORE:
			return "XboxAxisIgnored";
		case GAMEPAD_AXIS_UNKNOWN:
			return "XboxAxisUnknown";
		case GAMEPAD_AXIS_ADDL0:
			return "XboxAddlAxis0";
		case GAMEPAD_AXIS_ADDL1:
			return "XboxAddlAxis1";
		case GAMEPAD_AXIS_ADDL2:
			return "XboxAddlAxis2";
		case GAMEPAD_AXIS_ADDL3:
			return "XboxAddlAxis3";
		case GAMEPAD_AXIS_ADDL4:
			return "XboxAddlAxis4";
		case GAMEPAD_AXIS_ADDL5:
			return "XboxAddlAxis5";
		}
	}
}

const char *gamepad_get_axis_desc_sony (u16 product_id, gamepad_axis axis) {
	switch (product_id) {
	case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
	default:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
	case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
	case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
		switch (axis) {
		case GAMEPAD_AXIS_LX:
			return "PSLThumbX";
		case GAMEPAD_AXIS_LY:
			return "PSLThumbY";
		case GAMEPAD_AXIS_LZ:
			return "PSL2";
		case GAMEPAD_AXIS_RX:
			return "PSRThumbX";
		case GAMEPAD_AXIS_RY:
			return "PSRThumbY";
		case GAMEPAD_AXIS_RZ:
			return "PSR2";
		case GAMEPAD_AXIS_LRZ:
			return "PSL2&R2";
		case GAMEPAD_AXIS_IGNORE:
			return "PSAxisIgnored";
		case GAMEPAD_AXIS_UNKNOWN:
			return "PSAxisUnknown";
		case GAMEPAD_AXIS_DPAD_LR:
			return "PSDpadLR";
		case GAMEPAD_AXIS_DPAD_UD:
			return "PSDpadUD";
		case GAMEPAD_AXIS_ADDL0:
			return "PSAddlAxis0";
		case GAMEPAD_AXIS_ADDL1:
			return "PSAddlAxis1";
		case GAMEPAD_AXIS_ADDL2:
			return "PSAddlAxis2";
		case GAMEPAD_AXIS_ADDL3:
			return "PSAddlAxis3";
		case GAMEPAD_AXIS_ADDL4:
			return "PSAddlAxis4";
		case GAMEPAD_AXIS_ADDL5:
			return "PSAddlAxis5";
		}
	}
}

const char *gamepad_get_axis_desc_nintendo (u16 product_id, gamepad_axis axis) {
	switch (product_id) {
	default:
		switch (axis) {
		case GAMEPAD_AXIS_LX:
			return "NintendoLThumbX";
		case GAMEPAD_AXIS_LY:
			return "NintendoLThumbY";
		case GAMEPAD_AXIS_LZ:
			return "NintendoZL";
		case GAMEPAD_AXIS_RX:
			return "NintendoRThumbX";
		case GAMEPAD_AXIS_RY:
			return "NintendoRThumbY";
		case GAMEPAD_AXIS_RZ:
			return "NintendoZR";
		case GAMEPAD_AXIS_LRZ:
			return "NintendoZL&ZR";
		case GAMEPAD_AXIS_IGNORE:
			return "NintendoAxisIgnored";
		case GAMEPAD_AXIS_UNKNOWN:
			return "NintendoAxisUnknown";
		case GAMEPAD_AXIS_DPAD_LR:
			return "NintendoDpadLR";
		case GAMEPAD_AXIS_DPAD_UD:
			return "NintendoDpadUD";
		case GAMEPAD_AXIS_ADDL0:
			return "NintendoAddlAxis0";
		case GAMEPAD_AXIS_ADDL1:
			return "NintendoAddlAxis1";
		case GAMEPAD_AXIS_ADDL2:
			return "NintendoAddlAxis2";
		case GAMEPAD_AXIS_ADDL3:
			return "NintendoAddlAxis3";
		case GAMEPAD_AXIS_ADDL4:
			return "NintendoAddlAxis4";
		case GAMEPAD_AXIS_ADDL5:
			return "NintendoAddlAxis5";
		}
	}
}

const char *gamepad_get_axis_desc_default (u16 product_id, gamepad_axis axis) {
	switch (product_id) {
	default:
		switch (axis) {
		case GAMEPAD_AXIS_LX:
			return "GenericLThumbX";
		case GAMEPAD_AXIS_LY:
			return "GenericLThumbY";
		case GAMEPAD_AXIS_LZ:
			return "GenericL2";
		case GAMEPAD_AXIS_RX:
			return "GenericLThumbX";
		case GAMEPAD_AXIS_RY:
			return "GenericRThumbY";
		case GAMEPAD_AXIS_RZ:
			return "GenericR2";
		case GAMEPAD_AXIS_LRZ:
			return "GenericL2&R2";
		case GAMEPAD_AXIS_IGNORE:
			return "GenericAxisIgnored";
		case GAMEPAD_AXIS_UNKNOWN:
			return "GenericAxisUnknown";
		case GAMEPAD_AXIS_DPAD_LR:
			return "GenericDpadLR";
		case GAMEPAD_AXIS_DPAD_UD:
			return "GenericDpadUD";
		case GAMEPAD_AXIS_ADDL0:
			return "GenericAddlAxis0";
		case GAMEPAD_AXIS_ADDL1:
			return "GenericAddlAxis1";
		case GAMEPAD_AXIS_ADDL2:
			return "GenericAddlAxis2";
		case GAMEPAD_AXIS_ADDL3:
			return "GenericAddlAxis3";
		case GAMEPAD_AXIS_ADDL4:
			return "GenericAddlAxis4";
		case GAMEPAD_AXIS_ADDL5:
			return "GenericAddlAxis5";
		}
	}
}

const char *gamepad_get_button_desc_microsoft (u16 product_id, gamepad_button button) {
	switch (button) {
	case GAMEPAD_BUTTON_UNKNOWN:
		return "XboxButtonUnknown";
	case GAMEPAD_BUTTON_IGNORE:
		return "XboxButtonIgnored";
	case GAMEPAD_BUTTON_DPAD_LEFT:
		return "XboxDPadLeft";
	case GAMEPAD_BUTTON_DPAD_RIGHT:
		return "XboxDPadRight";
	case GAMEPAD_BUTTON_DPAD_UP:
		return "XboxDPadUp";
	case GAMEPAD_BUTTON_DPAD_DOWN:
		return "XboxDPadDown";
	case GAMEPAD_BUTTON_L1:
		return "XboxLBumper";
	case GAMEPAD_BUTTON_L2:
		return "XboxLTrigger";
	case GAMEPAD_BUTTON_L3:
		return "XboxL3";
	case GAMEPAD_BUTTON_R1:
		return "XboxRBumper";
	case GAMEPAD_BUTTON_R2:
		return "XboxRTrigger";
	case GAMEPAD_BUTTON_R3:
		return "XboxR3";
	case GAMEPAD_BUTTON_OEM_HOME:
		return "XboxXboxButton";
	case GAMEPAD_BUTTON_NORTH:
		return "XboxY";
	case GAMEPAD_BUTTON_SOUTH:
		return "XboxA";
	case GAMEPAD_BUTTON_EAST:
		return "XboxB";
	case GAMEPAD_BUTTON_WEST:
		return "XboxX";
	case GAMEPAD_BUTTON_SELECT:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_0:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_1:
		case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER_PACC:
		case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER:
			return "XboxBack";
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_2015:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_ELITE_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_1:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_S_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_SXS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ELITE_SERIES_2_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_1:
			return "XboxOneView";
		}
	case GAMEPAD_BUTTON_START:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_0:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_S_1:
		case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER_PACC:
		case GAMEPAD_PRODUCT_ID_XBOX_360_CONTROLLER:
			return "XboxStart";
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_2015:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_ELITE_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_CONTROLLER_1:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_S_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ONE_SXS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_ELITE_SERIES_2_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_XBOX_CONTROLLER_1:
			return "XboxOneMenu";
		}
	case GAMEPAD_BUTTON_ADDL0:
		return "XboxAddlButton0";
	case GAMEPAD_BUTTON_ADDL1:
		return "XboxAddlButton1";
	case GAMEPAD_BUTTON_ADDL2:
		return "XboxAddlButton2";
	case GAMEPAD_BUTTON_ADDL3:
		return "XboxAddlButton3";
	case GAMEPAD_BUTTON_ADDL4:
		return "XboxAddlButton4";
	case GAMEPAD_BUTTON_ADDL5:
		return "XboxAddlButton5";
	}
}

const char *gamepad_get_button_desc_sony (u16 product_id, gamepad_button button) {
	switch (button) {
	case GAMEPAD_BUTTON_UNKNOWN:
		return "PSButtonUnknown";
	case GAMEPAD_BUTTON_IGNORE:
		return "PSButtonIgnored";
	case GAMEPAD_BUTTON_DPAD_LEFT:
		return "PSDPadLeft";
	case GAMEPAD_BUTTON_DPAD_RIGHT:
		return "PSDPadRight";
	case GAMEPAD_BUTTON_DPAD_UP:
		return "PSDPadUp";
	case GAMEPAD_BUTTON_DPAD_DOWN:
		return "PSDPadDown";
	case GAMEPAD_BUTTON_L1:
		return "PSL1";
	case GAMEPAD_BUTTON_L2:
		return "PSL2";
	case GAMEPAD_BUTTON_L3:
		return "PSL3";
	case GAMEPAD_BUTTON_R1:
		return "PSR1";
	case GAMEPAD_BUTTON_R2:
		return "PSR2";
	case GAMEPAD_BUTTON_R3:
		return "PSR3";
	case GAMEPAD_BUTTON_OEM_HOME:
		return "PSPSButton";
	case GAMEPAD_BUTTON_NORTH:
		return "PSTriangle";
	case GAMEPAD_BUTTON_SOUTH:
		return "PSCross";
	case GAMEPAD_BUTTON_EAST:
		return "PSCircle";
	case GAMEPAD_BUTTON_WEST:
		return "PSSquare";
	case GAMEPAD_BUTTON_SELECT:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
			return "PSSelect";
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
			return "PS4Share";
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
			return "PS5Create";
		}
	case GAMEPAD_BUTTON_START:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
			return "PSStart";
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
			return "PS4Options";
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
			return "PS5Options";
		}
	case GAMEPAD_BUTTON_ADDL0:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
			return "PSAddlButton0";
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
			return "PS4Touchpad";
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
			return "PS5Touchpad";
		}
	case GAMEPAD_BUTTON_ADDL1:
		switch (product_id) {
		default:
		case GAMEPAD_PRODUCT_ID_PS_CLASSIC_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS3_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_1:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_2:
		case GAMEPAD_PRODUCT_ID_DUALSHOCK4_WIRELESS_ADAPTER:
			return "PSAddlButton1";
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_WIRELESS_CONTROLLER:
		case GAMEPAD_PRODUCT_ID_PS5_DUALSENSE_EDGE_WIRELESS_CONTROLLER:
			return "PS5Mute";
		}
	case GAMEPAD_BUTTON_ADDL2:
		return "PSAddlButton2";
	case GAMEPAD_BUTTON_ADDL3:
		return "PSAddlButton3";
	case GAMEPAD_BUTTON_ADDL4:
		return "PSAddlButton4";
	case GAMEPAD_BUTTON_ADDL5:
		return "PSAddlButton5";
	}
}

const char *gamepad_get_button_desc_nintendo (u16 product_id, gamepad_button button) {
	switch (product_id) {
	default:
		switch (button) {
		case GAMEPAD_BUTTON_UNKNOWN:
			return "NintendoButtonUnknown";
		case GAMEPAD_BUTTON_IGNORE:
			return "NintendoButtonIgnored";
		case GAMEPAD_BUTTON_DPAD_LEFT:
			return "NintendoDPadLeft";
		case GAMEPAD_BUTTON_DPAD_RIGHT:
			return "NintendoDPadRight";
		case GAMEPAD_BUTTON_DPAD_UP:
			return "NintendoDPadUp";
		case GAMEPAD_BUTTON_DPAD_DOWN:
			return "NintendoDPadDown";
		case GAMEPAD_BUTTON_L1:
			return "NintendoL";
		case GAMEPAD_BUTTON_L2:
			return "NintendoZL";
		case GAMEPAD_BUTTON_L3:
			return "NintendoL3";
		case GAMEPAD_BUTTON_R1:
			return "NintendoR";
		case GAMEPAD_BUTTON_R2:
			return "NintendoZR";
		case GAMEPAD_BUTTON_R3:
			return "NintendoR3";
		case GAMEPAD_BUTTON_OEM_HOME:
			return "NintendoHomeButton";
		case GAMEPAD_BUTTON_NORTH:
			return "NintendoX";
		case GAMEPAD_BUTTON_SOUTH:
			return "NintendoB";
		case GAMEPAD_BUTTON_EAST:
			return "NintendoA";
		case GAMEPAD_BUTTON_WEST:
			return "NintendoY";
		case GAMEPAD_BUTTON_SELECT:
			return "NintendoSelect";
		case GAMEPAD_BUTTON_START:
			return "NintendoStart";
		case GAMEPAD_BUTTON_ADDL0:
			return "NintendoAddlButton0";
		case GAMEPAD_BUTTON_ADDL1:
			return "NintendoAddlButton1";
		case GAMEPAD_BUTTON_ADDL2:
			return "NintendoAddlButton2";
		case GAMEPAD_BUTTON_ADDL3:
			return "NintendoAddlButton3";
		case GAMEPAD_BUTTON_ADDL4:
			return "NintendoAddlButton4";
		case GAMEPAD_BUTTON_ADDL5:
			return "NintendoAddlButton5";
		}
	}
}

const char *gamepad_get_button_desc_default (u16 product_id, gamepad_button button) {
	switch (product_id) {
	default:
		switch (button) {
		case GAMEPAD_BUTTON_UNKNOWN:
			return "GenericButtonUnknown";
		case GAMEPAD_BUTTON_IGNORE:
			return "GenericButtonIgnored";
		case GAMEPAD_BUTTON_DPAD_LEFT:
			return "GenericDPadLeft";
		case GAMEPAD_BUTTON_DPAD_RIGHT:
			return "GenericDPadRight";
		case GAMEPAD_BUTTON_DPAD_UP:
			return "GenericDPadUp";
		case GAMEPAD_BUTTON_DPAD_DOWN:
			return "GenericDPadDown";
		case GAMEPAD_BUTTON_L1:
			return "GenericL1";
		case GAMEPAD_BUTTON_L2:
			return "GenericL2";
		case GAMEPAD_BUTTON_L3:
			return "GenericL3";
		case GAMEPAD_BUTTON_R1:
			return "GenericR1";
		case GAMEPAD_BUTTON_R2:
			return "GenericR2";
		case GAMEPAD_BUTTON_R3:
			return "GenericR3";
		case GAMEPAD_BUTTON_OEM_HOME:
			return "GenericHomeButton";

			// Generic is going to mimic

		case GAMEPAD_BUTTON_NORTH:
			return "Generic1";
		case GAMEPAD_BUTTON_SOUTH:
			return "Generic3";
		case GAMEPAD_BUTTON_EAST:
			return "Generic2";
		case GAMEPAD_BUTTON_WEST:
			return "Generic4";
		case GAMEPAD_BUTTON_SELECT:
			return "GenericSelect";
		case GAMEPAD_BUTTON_START:
			return "GenericStart";
		case GAMEPAD_BUTTON_ADDL0:
			return "GenericAddlButton0";
		case GAMEPAD_BUTTON_ADDL1:
			return "GenericAddlButton1";
		case GAMEPAD_BUTTON_ADDL2:
			return "GenericAddlButton2";
		case GAMEPAD_BUTTON_ADDL3:
			return "GenericAddlButton3";
		case GAMEPAD_BUTTON_ADDL4:
			return "GenericAddlButton4";
		case GAMEPAD_BUTTON_ADDL5:
			return "GenericAddlButton5";
		}
	}
}
