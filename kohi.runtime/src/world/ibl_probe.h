/**
 * @file ibl_probe.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <core_render_types.h>
#include <math/math_types.h>
#include <strings/kname.h>

typedef struct ibl_probe {
	kname cubemap_name;
	ktexture ibl_cube_texture;
	vec3 position;
} ibl_probe;

KAPI b8 ibl_probe_create (kname cubemap_name, vec3 position, ibl_probe *out_probe);
KAPI void ibl_probe_destroy (ibl_probe *probe);

KAPI b8 ibl_probe_load (ibl_probe *probe);
KAPI void ibl_probe_unload (ibl_probe *probe);
