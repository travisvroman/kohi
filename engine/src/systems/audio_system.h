#include "audio/audio_types.h"
#include "defines.h"

/**
 * The maximum number of individually-controlled channels of audio available, each
 * with separate volume control. These are all nested under a master audio volume.
 */
#define MAX_AUDIO_CHANNELS 16

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
     * The size to chunk streamed audio data in.
     */
    u32 chunk_size;

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
KAPI b8 audio_system_initialize(u64* memory_requirement, void* state, void* config);

/**
 * @brief Shuts down the audio system.
 *
 * @param state The state block of memory.
 */
KAPI void audio_system_shutdown(void* state);

/**
 * @brief Updates the audio system. Should happen once an update cycle.
 */
KAPI b8 audio_system_update(void* state, struct frame_data* p_frame_data);

/**
 * Sets the orientation of the listener. Typically linked to the current camera in the world.
 * @param position The position of the listener.
 * @param forward The listener's forward vector.
 * @param up The listener's up vector.
 * @return True on success; otherwise false.
 */
KAPI b8 audio_system_listener_orientation_set(vec3 position, vec3 forward, vec3 up);

/**
 * @brief Attempts to load a sound chunk at the given path. Returns a pointer
 * to a loaded sound. This dynamically allocates memory, so make sure to
 * call audio_system_close() on it when done.
 * @param path The full path to the asset to be loaded.
 * @return A pointer to an audio_sound one success; otherwise null/0.
 */
KAPI struct audio_file* audio_system_chunk_load(const char* path);

/**
 * @brief Attempts to load a audio stream file at the given path. Returns a pointer
 * to a loaded music. This dynamically allocates memory, so make sure to
 * call audio_system_close() on it when done.
 * @param path The full path to the asset to be loaded.
 * @return A pointer to an audio_music one success; otherwise null/0.
 */
KAPI struct audio_file* audio_system_stream_load(const char* path);

/**
 * @brief Closes the given sound, releasing all internal resources.
 * @param file A pointer to the sound file to be closed.
 */
KAPI void audio_system_close(struct audio_file* file);

/**
 * @brief Sets the master volume level. This affects all channels
 * overall.
 * @param volume The volume to set. Clamed to a range of [0.0-1.0].
 */
KAPI void audio_system_master_volume_set(f32 volume);

/**
 * @brief Queries the master volume.
 * @param volume A pointer to hold the volume.
 */
KAPI void audio_system_master_volume_query(f32* out_volume);

/**
 * @brief Sets the volume for the given channel id.
 * @param channel_id The id of the channel to adjust volume for.
 * @volume The volume to set. Clamped to a range of [0.0-1.0].
 * @return True on success; otherwise false.
 */
KAPI b8 audio_system_channel_volume_set(i8 channel_id, f32 volume);

/**
 * @brief Queries the given channel's volume volume.
 * @param channel_id The id of the channel to query. -1 cannot be used here.
 * @param volume A pointer to hold the volume.
 * @return True on success; otherwise false.
 */
KAPI b8 audio_system_channel_volume_query(i8 channel_id, f32* out_volume);

/**
 * Plays the provided sound on the channel with the given id. Note that this
 * is effectively "2d" sound, meant for sounds which don't exist in the world
 * and should be played globally (i.e. UI sound effects).
 * @param channel_id The id of the channel to play the sound on.
 * @param sound The sound to be played.
 * @param loop Indicates if the sound should loop.
 * @return True on success; otherwise false.
 */
KAPI b8 audio_system_channel_play(i8 channel_id, struct audio_file* file, b8 loop);

/**
 * Plays spatially-oriented 3d sound from the context of an audio_emitter. The
 * emitter should already have a loaded sound associated with it. In addition,
 * if the emitter is moving, it should be updated per frame with a call to
 * audio_system_emitter_update().
 * @param channel_id The id of the channel to play through.
 * @param emitter A pointer to an emitter to use for playback.
 * @return True on success; otherwise false.
 */
KAPI b8 audio_system_channel_emitter_play(i8 channel_id, struct audio_emitter* emitter);

/**
 * Stops the given channel id.
 * @param channel_id The id of the channel to be stopped. If -1 is passed, all channels are stopped.
 * @return True on success; otherwise false.
 */
KAPI void audio_system_channel_stop(i8 channel_id);
KAPI void audio_system_channel_pause(i8 channel_id);
KAPI void audio_system_channel_resume(i8 channel_id);
