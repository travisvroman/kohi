#pragma once

#include "defines.h"

struct ui_text;

void debug_console_create();

b8 debug_console_load();
void debug_console_update();

struct ui_text* debug_console_get_text();
struct ui_text* debug_console_get_entry_text();

b8 debug_console_visible();
void debug_console_visible_set(b8 visible);

void debug_console_move_up();
void debug_console_move_down();
void debug_console_move_to_top();
void debug_console_move_to_bottom();
