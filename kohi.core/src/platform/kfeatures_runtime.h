/**
 * @file kfeatures_runtime.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief
 * @version 1.0
 * @date 2026-10-28
 *
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 *
 */

#include "platform/platform.h"

void detect_x86_features (kcpu_feature_flag_bits *flags);
void detect_arm_features (kcpu_feature_flag_bits *flags);
