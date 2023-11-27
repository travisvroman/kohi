#ifndef _UI_PASS_H_
#define _UI_PASS_H_

#include "defines.h"
#include "standard_ui_system.h"

struct rendergraph_pass;
struct frame_data;

struct geometry_render_data;

typedef struct ui_pass_extended_data {
    standard_ui_render_data sui_render_data;
} ui_pass_extended_data;

KAPI b8 ui_pass_create(struct rendergraph_pass* self);
KAPI b8 ui_pass_initialize(struct rendergraph_pass* self);
KAPI b8 ui_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
KAPI void ui_pass_destroy(struct rendergraph_pass* self);

#endif
