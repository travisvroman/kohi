#pragma once
#include <defines.h>

struct application;
struct render_packet;
struct frame_data;

KAPI u64 application_state_size();

KAPI b8 application_boot(struct application* game_inst);

KAPI b8 application_initialize(struct application* game_inst);

KAPI b8 application_update(struct application* game_inst, const struct frame_data* p_frame_data);

KAPI b8 application_render(struct application* game_inst, struct render_packet* packet, const struct frame_data* p_frame_data);

KAPI void application_on_resize(struct application* game_inst, u32 width, u32 height);

KAPI void application_shutdown(struct application* game_inst);

KAPI void application_lib_on_unload(struct application* game_inst);

KAPI void application_lib_on_load(struct application* game_inst);
