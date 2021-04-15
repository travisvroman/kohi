#pragma once

#include <defines.h>
#include <game_types.h>

typedef struct game_state {
    f32 delta_time;
} game_state;

b8 game_initialize(game* game_inst);

b8 game_update(game* game_inst, f32 delta_time);

b8 game_render(game* game_inst, f32 delta_time);

void game_on_resize(game* game_inst, u32 width, u32 height);
