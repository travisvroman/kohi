#include "audio/audio_types.h"
#include "defines.h"

/**
 * The maximum number of individually-controlled channels of audio available, each
 * with separate volume control. These are all nested under a master audio volume.
 */
#define MAX_AUDIO_CHANNELS 8

struct frame_data;

typedef struct audio_system_config {
    /** The audio plugin to use with this system. Must already by setup at this point. */
    audio_plugin plugin;
    /** @brief The frequency to output audio at. */
    u32 frequency;
    /**
     * @brief The number of audio channels to support (i.e. 2 for stereo, 1 for mono).
     * not to be confused with audio_channel_count below.
     */
    u32 channel_count;

    /**
     * @brief The number of separately-controlled channels used for mixing purposes. Each channel
     * can have its volume independently controlled. Not to be confused with channel_count above.
     */
    u32 audio_channel_count;
} audio_system_config;

/**
 * @brief Initializes the audio system.
 * Should be called twice; once to get the memory requirement (passing state=0), and a second
 * time passing an allocated block of memory to actually initialize the system.
 *
 * @param memory_requirement A pointer to hold the memory requirement as it is calculated.
 * @param state A block of memory to hold the state or, if gathering the memory requirement, 0.
 * @param config The configuration (audio_system_config) for this system.
 * @return True on success; otherwise false.
 */
b8 audio_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the audio system.
 *
 * @param state The state block of memory.
 */
void audio_system_shutdown(void* state);

/**
 * @brief Updates the audio system. Should happen once an update cycle.
 */
b8 audio_system_update(void* state, struct frame_data* p_frame_data);

b8 audio_system_listener_orientation_set(vec3 position, vec3 forward, vec3 up);

/**
 * @brief Attempts to load a sound at the given path. Returns a pointer
 * to a loaded sound. This dynamically allocates memory, so make sure to
 * call audio_system_sound_close() on it when done.
 * @param path The full path to the asset to be loaded.
 * @return A pointer to an audio_sound one success; otherwise null/0.
 */
struct audio_sound* audio_system_sound_load(const char* path);

/**
 * @brief Closes the given sound, releasing all internal resources.
 * @param sound A pointer to the sound to be closed.
 */
void audio_system_sound_close(struct audio_sound* sound);

/**
 * @brief Sets the volume for the given channel id.
 * @param channel_id The id of the channel to adjust volume for.
 * @volume The volume to set. Clamped to a range of [0.0-1.0].
 * @return True on success; otherwise false.
 */
b8 audio_system_channel_volume_set(u8 channel_id, f32 volume);

/**
 * Plays the provided sound on the channel with the given id.
 * @param channel_id The id of the channel to play the sound on.
 * @param sound The sound to be played.
 * @return True on success; otherwise false.
 */
b8 audio_system_channel_play(u8 channel_id, struct audio_sound* sound);

b8 audio_system_emitter_play(f32 master_volume, struct audio_emitter* emitter);

b8 audio_system_emitter_stop(struct audio_emitter* emitter);
