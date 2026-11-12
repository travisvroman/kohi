#pragma once

#include "input_types.h"

KAPI const char *util_gamepad_get_axis_desc (u16 vendor_id, u16 product_id, gamepad_axis axis);
KAPI const char *util_gamepad_get_button_desc (u16 vendor_id, u16 product_id, gamepad_button button);
