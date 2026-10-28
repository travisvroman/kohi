/**
 * @file testbed_main.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The main interface to the Testbed project, which is a test 
 * driver for the Kohi Game Engine.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once
#include <defines.h>

struct application;
struct frame_data;
struct kwindow;

KAPI u64 application_state_size (void);

KAPI b8 application_boot (struct application *game_inst);

/**
 * @brief Application initialization routine.
 *
 * @param app A pointer to the application state.
 * @return True on success; otherwise false.
 */
KAPI b8 application_initialize (struct application *game_inst);

KAPI b8 application_update (struct application *game_inst, struct frame_data *p_frame_data);

KAPI b8 application_prepare_frame (struct application *app_inst, struct frame_data *p_frame_data);

KAPI b8 application_render_frame (struct application *game_inst, struct frame_data *p_frame_data);

KAPI void application_on_window_resize (struct application *game_inst, const struct kwindow *window);

KAPI void application_on_action (struct application *app_inst, u32 action_code);

KAPI void application_shutdown (struct application *game_inst);

KAPI void application_lib_on_unload (struct application *game_inst);

KAPI void application_lib_on_load (struct application *game_inst);
