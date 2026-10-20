#pragma once

#include "kui_system.h"
#include "kui_types.h"

#include <systems/font_system.h>

/**
 * @brief A header showing the selected item that opens a list of at most max_rows items below
 * it. The control's own size is the header's; the open list is drawn over what is below. Clicking
 * an item, or anywhere outside, closes it. With keyboard navigation, enter opens it, up and down
 * choose, enter confirms and escape closes without choosing.
 */
KAPI kui_control kui_dropdown_create (kui_state *state, const char *name, f32 width, u32 max_rows, font_type type, kname font_name, u16 font_size);
KAPI void kui_dropdown_destroy (kui_state *state, kui_control *self);

KAPI void kui_dropdown_item_add (kui_state *state, kui_control self, const char *text);
/** @brief Selects an item (-1 for none), closing the list. Calls the selected callback if the selection changed. */
KAPI void kui_dropdown_selected_set (kui_state *state, kui_control self, i32 index);
KAPI i32 kui_dropdown_selected_get (kui_state *state, kui_control self);
KAPI b8 kui_dropdown_is_open (kui_state *state, kui_control self);
KAPI void kui_dropdown_open_set (kui_state *state, kui_control self, b8 open);
KAPI void kui_dropdown_set_on_selected (kui_state *state, kui_control self, PFN_list_event_callback callback);

KAPI b8 kui_dropdown_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_dropdown_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
