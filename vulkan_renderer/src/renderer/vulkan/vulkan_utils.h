/**
 * @file vulkan_utils.h
 * @author Travis Vroman (travis@kohiengine.com)
 * @brief A collection of Vulkan-specific utility functions.
 * @version 1.0
 * @date 2022-01-11
 * 
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2022
 * 
 */

#pragma once
#include "vulkan_types.inl"

/**
 * @brief Returns the string representation of result.
 * 
 * @param result The result to get the string for.
 * @param get_extended Indicates whether to also return an extended result.
 * @returns The error code and/or extended error message in string form. Defaults to success for unknown result types.
 */
const char* vulkan_result_string(VkResult result, b8 get_extended);

/**
 * @brief Inticates if the passed result is a success or an error as defined by the Vulkan spec.
 * 
 * @returns True if success; otherwise false. Defaults to true for unknown result types.
 */
b8 vulkan_result_is_success(VkResult result);