#ifndef _SKYBOX_PASS_H_
#define _SKYBOX_PASS_H_

#include "defines.h"

struct rendergraph_pass;
struct frame_data;

struct skybox;

typedef struct skybox_pass_extended_data {
    struct skybox* sb;
} skybox_pass_extended_data;

b8 skybox_pass_create(struct rendergraph_pass* self, void* config);
b8 skybox_pass_initialize(struct rendergraph_pass* self);
b8 skybox_pass_execute(struct rendergraph_pass* self, struct frame_data* p_frame_data);
void skybox_pass_destroy(struct rendergraph_pass* self);

#endif
