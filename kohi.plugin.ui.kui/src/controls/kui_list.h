#pragma once

#include "kui_system.h"
#include "kui_types.h"

#include <systems/font_system.h>

/**
 * @brief A list of text items. It is `width` units wide and shows visible_rows rows of
 * font_size + 8 units each. Click an item, or move the selection with up and down once the list
 * is focused by keyboard navigation (enter calls the activated callback). The mouse wheel scrolls
 * a list with more items than rows.
 */
KAPI kui_control kui_list_create (kui_state *state, const char *name, f32 width, u32 visible_rows, font_type type, kname font_name, u16 font_size);
KAPI void kui_list_destroy (kui_state *state, kui_control *self);

/** @brief Adds an item at the end. The text is copied. */
KAPI void kui_list_item_add (kui_state *state, kui_control self, const char *text);
/** @brief Removes every item and the selection. */
KAPI void kui_list_clear (kui_state *state, kui_control self);
KAPI u32 kui_list_count_get (kui_state *state, kui_control self);
KAPI const char *kui_list_item_get (kui_state *state, kui_control self, u32 index);

/** @brief Selects an item, or none with -1. Out-of-range values are ignored. Calls the selected callback if the selection changed. */
KAPI void kui_list_selected_set (kui_state *state, kui_control self, i32 index);
KAPI i32 kui_list_selected_get (kui_state *state, kui_control self);

KAPI void kui_list_set_on_selected (kui_state *state, kui_control self, PFN_list_event_callback callback);
/** @brief Called when an item is activated: clicked, or accepted by navigation. */
KAPI void kui_list_set_on_activated (kui_state *state, kui_control self, PFN_list_event_callback callback);

KAPI b8 kui_list_update (kui_state *state, kui_control self, struct frame_data *p_frame_data);
KAPI b8 kui_list_render (kui_state *state, kui_control self, struct frame_data *p_frame_data, kui_render_data *render_data);
