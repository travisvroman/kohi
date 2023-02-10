#pragma once
#include <defines.h>

struct application;
struct render_packet;

KAPI u64 application_state_size();

KAPI b8 application_boot(struct application* game_inst);

KAPI b8 application_initialize(struct application* game_inst);

KAPI b8 application_update(struct application* game_inst, f32 delta_time);

KAPI b8 application_render(struct application* game_inst, struct render_packet* packet, f32 delta_time);

KAPI void application_on_resize(struct application* game_inst, u32 width, u32 height);

KAPI void application_shutdown(struct application* game_inst);

KAPI void application_lib_on_unload(struct application* game_inst);

KAPI void application_lib_on_load(struct application* game_inst);
