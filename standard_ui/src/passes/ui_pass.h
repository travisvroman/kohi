#ifndef _UI_PASS_H_
#define _UI_PASS_H_

#include "defines.h"
#include "standard_ui_system.h"

struct rendergraph_pass;
struct frame_data;

struct geometry_render_data;
struct ui_text;

typedef struct ui_pass_extended_data {
    u32 geometry_count;
    struct geometry_render_data* geometries;
    u32 ui_text_count;
    struct ui_text** texts;
    standard_ui_render_data sui_render_data;
} ui_pass_extended_data;

b8 ui_pass_create(struct rendergraph_pass* self);
b8 ui_pass_initialize(struct rendergraph_pass* self);
b8 ui_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
void ui_pass_destroy(struct rendergraph_pass* self);

#endif
