/**
 * @file test_manager.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief The kohi.core test manager.
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#pragma once

#include <defines.h>

#define BYPASS 2

typedef u8 (*PFN_test)(void);

void test_manager_init (void);

void test_manager_register_test (PFN_test, char *desc);

void test_manager_run_tests (void);