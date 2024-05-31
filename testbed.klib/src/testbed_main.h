#pragma once
#include <defines.h>

struct application;
struct frame_data;
struct kwindow;

KAPI u64 application_state_size(void);

KAPI b8 application_boot(struct application* game_inst);

KAPI b8 application_initialize(struct application* game_inst);

KAPI b8 application_update(struct application* game_inst, struct frame_data* p_frame_data);

KAPI b8 application_prepare_frame(struct application* app_inst, struct frame_data* p_frame_data);

KAPI b8 application_render_frame(struct application* game_inst, struct frame_data* p_frame_data);

KAPI void application_on_resize(struct application* game_inst, const struct kwindow* window);

KAPI void application_shutdown(struct application* game_inst);

KAPI void application_lib_on_unload(struct application* game_inst);

KAPI void application_lib_on_load(struct application* game_inst);
