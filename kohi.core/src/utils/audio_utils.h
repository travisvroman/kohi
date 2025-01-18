#pragma once

#include "core_audio_types.h"
#include "defines.h"

/**
 * @brief Parses the audio space from the given string. Defaults to 2D if not valid.
 *
 * @param str The string to parse.
 * @return The sound space.
 */
KAPI kaudio_space string_to_audio_space(const char* str);

/**
 * @brief Gets the string representation of the given sound space.
 * NOTE: string is constant and does not need to be freed.
 *
 * @param space The audio space to convert.
 * @return the string representation of the given audio space.
 */
KAPI const char* audio_space_to_string(kaudio_space space);

/**
 * @brief Parses the attenuation model from the given string. Defaults to linear if not valid.
 *
 * @param str The string to parse.
 * @return The attenuation model.
 */
KAPI kaudio_attenuation_model string_to_attenuation_model(const char* str);

/**
 * @brief Gets the string representation of the given attenuation model.
 * NOTE: string is constant and does not need to be freed.
 *
 * @param model The model to convert.
 * @return the string representation of the given attenuation model.
 */
KAPI const char* attenuation_model_to_string(kaudio_attenuation_model model);

/**
 * @brief Computes spatial gain based on position, radius and attenuation model parameters.
 *
 * @param distance The distance between the listener and the center point of the sound in the world.
 * @param inner_radius The inner radius around the sound's center point. A listener inside this radius experiences the volume at 100%.
 * @param outer_radius The outer radius around the sound's center point. A listener outside this radius experiences the volume at 0%.
 * @param falloff_factor The falloff factor used for exponential falloff. Ignored for other models.
 * @param model The model to use for the sound attenuation.
 *
 * @return The final gain of the sound based on the supplied parameters, before being mixed with volume.
 */
KAPI f32 calculate_spatial_gain(f32 distance, f32 inner_radius, f32 outer_radius, f32 falloff_factor, kaudio_attenuation_model model);

/**
 * @brief Downmixes the provided stereo data to mono data by averaging the left
 * and right channels and scaling it to fit within an i16.
 *
 * @param stereo_data The raw interleaved sample data in stereo format.
 * @param sample_count The total number of samples.
 *
 * @returns A dynamically-allocated array of downmixed mono data on success; otherwise 0/null.
 */
KAPI i16* kaudio_downmix_stereo_to_mono(const i16* stereo_data, u32 sample_count) ;
