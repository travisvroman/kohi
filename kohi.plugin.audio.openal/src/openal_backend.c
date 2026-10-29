/**
 * @file openal_backend.c
 * @copyright Kohi Game Engine is Copyright (c) Travis Vroman 2021-2026
 */

#include "openal_backend.h"
#include "audio/audio_frontend.h"
#include "core/engine.h"
#include "defines.h"

// OpenAL
#if KPLATFORM_APPLE
#	include <OpenAL/al.h>
#	include <OpenAL/alc.h>
#else
#	include <al.h>
#	include <alc.h>
#endif

// Core
#include <containers/darray.h>
#include <defines.h>
#include <identifiers/khandle.h>
#include <logger.h>
#include <math/kmath.h>
#include <memory/kmemory.h>
#include <platform/platform.h>
#include <threads/kmutex.h>
#include <threads/kthread.h>

// #ifdef KPLATFORM_WINDOWS
// #    include <malloc.h>
// #else
// #    include <alloca.h>
// #endif

// Runtime
#include <audio/kaudio_types.h>
#include <core_audio_types.h>
#include <systems/job_system.h>
#include <utils/audio_utils.h>

// The number of buffers used for streaming music file data.
#define OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT 2

typedef ALCcontext *(*PFN_alcCreateContext)(ALCdevice *device, const ALCint *attrlist);
typedef ALCboolean (*PFN_alcMakeContextCurrent)(ALCcontext *context);
typedef void (*PFN_alcProcessContext)(ALCcontext *context);
typedef void (*PFN_alcSuspendContext)(ALCcontext *context);
typedef void (*PFN_alcDestroyContext)(ALCcontext *context);
typedef ALCcontext *(*PFN_alcGetCurrentContext)(void);
typedef ALCdevice *(*PFN_alcGetContextsDevice)(ALCcontext *context);
typedef ALCdevice *(*PFN_alcOpenDevice)(const ALCchar *devicename);
typedef ALCboolean (*PFN_alcCloseDevice)(ALCdevice *device);
/*
 * Error support.
 * Obtain the most recent Context error
 */
typedef ALCenum (*PFN_alcGetError)(ALCdevice *device);

/*
 * Renderer State management
 */
typedef void (*PFN_alEnable)(ALenum capability);
typedef void (*PFN_alDisable)(ALenum capability);
typedef ALboolean (*PFN_alIsEnabled)(ALenum capability);

/*
 * State retrieval
 */
typedef const ALchar *(*PFN_alGetString)(ALenum param);
typedef void (*PFN_alGetBooleanv)(ALenum param, ALboolean *data);
typedef void (*PFN_alGetIntegerv)(ALenum param, ALint *data);
typedef void (*PFN_alGetFloatv)(ALenum param, ALfloat *data);
typedef void (*PFN_alGetDoublev)(ALenum param, ALdouble *data);
typedef ALboolean (*PFN_alGetBoolean)(ALenum param);
typedef ALint (*PFN_alGetInteger)(ALenum param);
typedef ALfloat (*PFN_alGetFloat)(ALenum param);
typedef ALdouble (*PFN_alGetDouble)(ALenum param);

/*
 * Error support.
 * Obtain the most recent error generated in the AL state machine.
 */
typedef ALenum (*PFN_alGetError)(void);

/*
 * Extension support.
 * Query for the presence of an extension, and obtain any appropriate
 * function pointers and enum values.
 */
typedef ALboolean (*PFN_alIsExtensionPresent)(const ALchar *extname);
typedef void *(*PFN_alGetProcAddress)(const ALchar *fname);
typedef ALenum (*PFN_alGetEnumValue)(const ALchar *ename);

/*
 * LISTENER
 * Listener represents the location and orientation of the
 * 'user' in 3D-space.
 *
 * Properties include: -
 *
 * Gain         AL_GAIN         ALfloat
 * Position     AL_POSITION     ALfloat[3]
 * Velocity     AL_VELOCITY     ALfloat[3]
 * Orientation  AL_ORIENTATION  ALfloat[6] (Forward then Up vectors)
 */

/*
 * Set Listener parameters
 */
typedef void (*PFN_alListenerf)(ALenum param, ALfloat value);
typedef void (*PFN_alListener3f)(ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (*PFN_alListenerfv)(ALenum param, const ALfloat *values);
typedef void (*PFN_alListeneri)(ALenum param, ALint value);
typedef void (*PFN_alListener3i)(ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (*PFN_alListeneriv)(ALenum param, const ALint *values);

/*
 * Get Listener parameters
 */
typedef void (*PFN_alGetListenerf)(ALenum param, ALfloat *value);
typedef void (*PFN_alGetListener3f)(ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (*PFN_alGetListenerfv)(ALenum param, ALfloat *values);
typedef void (*PFN_alGetListeneri)(ALenum param, ALint *value);
typedef void (*PFN_alGetListener3i)(ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (*PFN_alGetListeneriv)(ALenum param, ALint *values);

/**
 * SOURCE
 * Sources represent individual sound objects in 3D-space.
 * Sources take the PCM data provided in the specified Buffer,
 * apply Source-specific modifications, and then
 * submit them to be mixed according to spatial arrangement etc.
 *
 * Properties include: -
 *
 * Gain                              AL_GAIN                 ALfloat
 * Min Gain                          AL_MIN_GAIN             ALfloat
 * Max Gain                          AL_MAX_GAIN             ALfloat
 * Position                          AL_POSITION             ALfloat[3]
 * Velocity                          AL_VELOCITY             ALfloat[3]
 * Direction                         AL_DIRECTION            ALfloat[3]
 * Head Relative Mode                AL_SOURCE_RELATIVE      ALint (AL_TRUE or AL_FALSE)
 * Reference Distance                AL_REFERENCE_DISTANCE   ALfloat
 * Max Distance                      AL_MAX_DISTANCE         ALfloat
 * RollOff Factor                    AL_ROLLOFF_FACTOR       ALfloat
 * Inner Angle                       AL_CONE_INNER_ANGLE     ALint or ALfloat
 * Outer Angle                       AL_CONE_OUTER_ANGLE     ALint or ALfloat
 * Cone Outer Gain                   AL_CONE_OUTER_GAIN      ALint or ALfloat
 * Pitch                             AL_PITCH                ALfloat
 * Looping                           AL_LOOPING              ALint (AL_TRUE or AL_FALSE)
 * MS Offset                         AL_MSEC_OFFSET          ALint or ALfloat
 * Byte Offset                       AL_BYTE_OFFSET          ALint or ALfloat
 * Sample Offset                     AL_SAMPLE_OFFSET        ALint or ALfloat
 * Attached Buffer                   AL_BUFFER               ALint
 * State (Query only)                AL_SOURCE_STATE         ALint
 * Buffers Queued (Query only)       AL_BUFFERS_QUEUED       ALint
 * Buffers Processed (Query only)    AL_BUFFERS_PROCESSED    ALint
 */

/* Create Source objects */
typedef void (*PFN_alGenSources)(ALsizei n, ALuint *sources);

/* Delete Source objects */
typedef void (*PFN_alDeleteSources)(ALsizei n, const ALuint *sources);

/* Verify a handle is a valid Source */
typedef ALboolean (*PFN_alIsSource)(ALuint sid);

/*
 * Set Source parameters
 */
typedef void (*PFN_alSourcef)(ALuint sid, ALenum param, ALfloat value);
typedef void (*PFN_alSource3f)(ALuint sid, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (*PFN_alSourcefv)(ALuint sid, ALenum param, const ALfloat *values);
typedef void (*PFN_alSourcei)(ALuint sid, ALenum param, ALint value);
typedef void (*PFN_alSource3i)(ALuint sid, ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (*PFN_alSourceiv)(ALuint sid, ALenum param, const ALint *values);

/*
 * Get Source parameters
 */
typedef void (*PFN_alGetSourcef)(ALuint sid, ALenum param, ALfloat *value);
typedef void (*PFN_alGetSource3f)(ALuint sid, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (*PFN_alGetSourcefv)(ALuint sid, ALenum param, ALfloat *values);
typedef void (*PFN_alGetSourcei)(ALuint sid, ALenum param, ALint *value);
typedef void (*PFN_alGetSource3i)(ALuint sid, ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (*PFN_alGetSourceiv)(ALuint sid, ALenum param, ALint *values);

/*
 * Source vector based playback calls
 */

/* Play, replay, or resume (if paused) a list of Sources */
typedef void (*PFN_alSourcePlayv)(ALsizei ns, const ALuint *sids);

/* Stop a list of Sources */
typedef void (*PFN_alSourceStopv)(ALsizei ns, const ALuint *sids);

/* Rewind a list of Sources */
typedef void (*PFN_alSourceRewindv)(ALsizei ns, const ALuint *sids);

/* Pause a list of Sources */
typedef void (*PFN_alSourcePausev)(ALsizei ns, const ALuint *sids);

/*
 * Source based playback calls
 */

/* Play, replay, or resume a Source */
typedef void (*PFN_alSourcePlay)(ALuint sid);

/* Stop a Source */
typedef void (*PFN_alSourceStop)(ALuint sid);

/* Rewind a Source (set playback postiton to beginning) */
typedef void (*PFN_alSourceRewind)(ALuint sid);

/* Pause a Source */
typedef void (*PFN_alSourcePause)(ALuint sid);

/*
 * Source Queuing
 */
typedef void (*PFN_alSourceQueueBuffers)(ALuint sid, ALsizei numEntries, const ALuint *bids);
typedef void (*PFN_alSourceUnqueueBuffers)(ALuint sid, ALsizei numEntries, ALuint *bids);

/**
 * BUFFER
 * Buffer objects are storage space for sample data.
 * Buffers are referred to by Sources. One Buffer can be used
 * by multiple Sources.
 *
 * Properties include: -
 *
 * Frequency (Query only)    AL_FREQUENCY      ALint
 * Size (Query only)         AL_SIZE           ALint
 * Bits (Query only)         AL_BITS           ALint
 * Channels (Query only)     AL_CHANNELS       ALint
 */

/* Create Buffer objects */
typedef void (*PFN_alGenBuffers)(ALsizei n, ALuint *buffers);

/* Delete Buffer objects */
typedef void (*PFN_alDeleteBuffers)(ALsizei n, const ALuint *buffers);

/* Verify a handle is a valid Buffer */
typedef ALboolean (*PFN_alIsBuffer)(ALuint bid);

/* Specify the data to be copied into a buffer */
typedef void (*PFN_alBufferData)(ALuint bid, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq);

/*
 * Set Buffer parameters
 */
typedef void (*PFN_alBufferf)(ALuint bid, ALenum param, ALfloat value);
typedef void (*PFN_alBuffer3f)(ALuint bid, ALenum param, ALfloat value1, ALfloat value2, ALfloat value3);
typedef void (*PFN_alBufferfv)(ALuint bid, ALenum param, const ALfloat *values);
typedef void (*PFN_alBufferi)(ALuint bid, ALenum param, ALint value);
typedef void (*PFN_alBuffer3i)(ALuint bid, ALenum param, ALint value1, ALint value2, ALint value3);
typedef void (*PFN_alBufferiv)(ALuint bid, ALenum param, const ALint *values);

/*
 * Get Buffer parameters
 */
typedef void (*PFN_alGetBufferf)(ALuint bid, ALenum param, ALfloat *value);
typedef void (*PFN_alGetBuffer3f)(ALuint bid, ALenum param, ALfloat *value1, ALfloat *value2, ALfloat *value3);
typedef void (*PFN_alGetBufferfv)(ALuint bid, ALenum param, ALfloat *values);
typedef void (*PFN_alGetBufferi)(ALuint bid, ALenum param, ALint *value);
typedef void (*PFN_alGetBuffer3i)(ALuint bid, ALenum param, ALint *value1, ALint *value2, ALint *value3);
typedef void (*PFN_alGetBufferiv)(ALuint bid, ALenum param, ALint *values);

/*
 * Global Parameters
 */
typedef void (*PFN_alDopplerFactor)(ALfloat value);
typedef void (*PFN_alDopplerVelocity)(ALfloat value);
typedef void (*PFN_alSpeedOfSound)(ALfloat value);
typedef void (*PFN_alDistanceModel)(ALenum distanceModel);

// This corresponds to audio data by index on the frontend.
typedef struct kaudio_internal_data {
	// The openal sound format (i.e. 16-bit mono/stereo)
	u32 format;
	// The current buffer being used to play sound effect types.
	ALuint buffer;
	// A secondary buffer used to play downmixed mono where the original asset was stereo.
	ALuint mono_buffer;
	// The internal buffers used for streaming data from larger files.
	ALuint streaming_buffers[OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT];
	// Indicates if the music file should loop.
	b8 is_looping;
	// Indicates if the internal asset should be streamed or all loaded at once.
	b8 is_stream;

	// Used to track samples in streaming type files.
	u32 total_samples_left;

	// The number of channels (i.e. 1 for mono or 2 for stereo)
	i32 channels;
	// The sample rate of the sound/music (i.e. 44100)
	u32 sample_rate;

	u32 total_sample_count;

	u64 pcm_data_size;
	/** Pulse-code modulation buffer, or raw data to be fed into a buffer. */
	i16 *pcm_data;
	i16 *mono_pcm_data;
	u64 downmixed_size;
} kaudio_internal_data;

// Sources are used to play sounds, potentially at a space in 3D.
typedef struct kaudio_plugin_source {
	// Internal OpenAL source.
	ALCuint id;

	// Worker thread for this source.
	kthread thread;

	kmutex data_mutex; // everything from here down should be accessed/changed during lock.
	// Currently playing audio data. Is INVALID_KAUDIO if not in use.
	kaudio current;
	// The current audio space.
	kaudio_space current_audio_space;

	// The current instance id being played.
	// For informational purposes only when completion
	// event is fired.
	u16 current_instance_id;

	// The state of the source on the previous state.
	ALint previous_state;

	b8 trigger_play;
	b8 trigger_exit;
} kaudio_plugin_source;

#define OAL_DECL(name) PFN_##name k##name
#define OAL_FUNCTION(name) state->k##name = (PFN_##name)platform_dynamic_library_load_function(#name, &state->openal_lib)

// The internal state for this audio backend.
typedef struct kaudio_backend_state {
	dynamic_library openal_lib;
	OAL_DECL (alcCreateContext);
	OAL_DECL (alcMakeContextCurrent);
	OAL_DECL (alcProcessContext);
	OAL_DECL (alcSuspendContext);
	OAL_DECL (alcDestroyContext);
	OAL_DECL (alcGetCurrentContext);
	OAL_DECL (alcGetContextsDevice);
	OAL_DECL (alcOpenDevice);
	OAL_DECL (alcCloseDevice);
	OAL_DECL (alcGetError);
	OAL_DECL (alEnable);
	OAL_DECL (alDisable);
	OAL_DECL (alIsEnabled);
	OAL_DECL (alGetString);
	OAL_DECL (alGetBooleanv);
	OAL_DECL (alGetIntegerv);
	OAL_DECL (alGetFloatv);
	OAL_DECL (alGetDoublev);
	OAL_DECL (alGetBoolean);
	OAL_DECL (alGetInteger);
	OAL_DECL (alGetFloat);
	OAL_DECL (alGetDouble);
	OAL_DECL (alGetError);
	OAL_DECL (alIsExtensionPresent);
	OAL_DECL (alGetProcAddress);
	OAL_DECL (alGetEnumValue);
	OAL_DECL (alListenerf);
	OAL_DECL (alListener3f);
	OAL_DECL (alListenerfv);
	OAL_DECL (alListeneri);
	OAL_DECL (alListener3i);
	OAL_DECL (alListeneriv);
	OAL_DECL (alGetListenerf);
	OAL_DECL (alGetListener3f);
	OAL_DECL (alGetListenerfv);
	OAL_DECL (alGetListeneri);
	OAL_DECL (alGetListener3i);
	OAL_DECL (alGetListeneriv);
	OAL_DECL (alGenSources);
	OAL_DECL (alDeleteSources);
	OAL_DECL (alIsSource);
	OAL_DECL (alSourcef);
	OAL_DECL (alSource3f);
	OAL_DECL (alSourcefv);
	OAL_DECL (alSourcei);
	OAL_DECL (alSource3i);
	OAL_DECL (alSourceiv);
	OAL_DECL (alGetSourcef);
	OAL_DECL (alGetSource3f);
	OAL_DECL (alGetSourcefv);
	OAL_DECL (alGetSourcei);
	OAL_DECL (alGetSource3i);
	OAL_DECL (alGetSourceiv);
	OAL_DECL (alSourcePlayv);
	OAL_DECL (alSourceStopv);
	OAL_DECL (alSourceRewindv);
	OAL_DECL (alSourcePausev);
	OAL_DECL (alSourcePlay);
	OAL_DECL (alSourceStop);
	OAL_DECL (alSourceRewind);
	OAL_DECL (alSourcePause);
	OAL_DECL (alSourceQueueBuffers);
	OAL_DECL (alSourceUnqueueBuffers);
	OAL_DECL (alGenBuffers);
	OAL_DECL (alDeleteBuffers);
	OAL_DECL (alIsBuffer);
	OAL_DECL (alBufferData);
	OAL_DECL (alBufferf);
	OAL_DECL (alBuffer3f);
	OAL_DECL (alBufferfv);
	OAL_DECL (alBufferi);
	OAL_DECL (alBuffer3i);
	OAL_DECL (alBufferiv);
	OAL_DECL (alGetBufferf);
	OAL_DECL (alGetBuffer3f);
	OAL_DECL (alGetBufferfv);
	OAL_DECL (alGetBufferi);
	OAL_DECL (alGetBuffer3i);
	OAL_DECL (alGetBufferiv);
	OAL_DECL (alDopplerFactor);
	OAL_DECL (alDopplerVelocity);
	OAL_DECL (alSpeedOfSound);
	OAL_DECL (alDistanceModel);

	/** @brief The maximum number of buffers available. Default: 256 */
	u32 max_buffers;
	/**
	 * @brief The maximum number of sources available.
	 *
	 * These map to "channels" on the frontend.
	 * Default: 8
	 * */
	u32 max_sources;

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

	// The selected audio device.
	ALCdevice *device;
	// The current audio context.
	ALCcontext *context;
	// A pool of buffers to be used for all kinds of audio/music playback.
	ALuint *buffers;
	// The total number of buffers available.
	u32 buffer_count;

	// A collection of available sources. config.max_sources has the count of this.
	kaudio_plugin_source *sources;

	// An array to keep free/available buffer ids.
	u32 *free_buffers;

	// The max number of audios that can be loaded at any one time. Synced with frontend.
	u32 max_count;

	// Internal data array aligning with that of the frontend.
	kaudio_internal_data *datas;
} kaudio_backend_state;

typedef struct ksource_work_thread_params {
	kaudio_backend_interface *backend;
	kaudio_plugin_source *source;
} ksource_work_thread_params;

static b8 openal_backend_channel_create (kaudio_backend_interface *backend, kaudio_plugin_source *out_source);
static void openal_backend_channel_destroy (kaudio_backend_interface *backend, kaudio_plugin_source *source);
static u32 openal_backend_find_free_buffer (kaudio_backend_interface *backend);

static b8 stream_data (kaudio_backend_interface *backend, ALuint buffer, kaudio_space audio_space, kaudio audio);
static b8 openal_backend_stream_update (kaudio_backend_interface *plugin, kaudio_plugin_source *source);
static b8 source_work_thread (void *params);
static b8 source_set_defaults (kaudio_backend_interface *backend, kaudio_plugin_source *source, b8 reset_use);
static b8 openal_backend_channel_create (kaudio_backend_interface *backend, kaudio_plugin_source *out_source);
static void openal_backend_channel_destroy (kaudio_backend_interface *backend, kaudio_plugin_source *source);
static void openal_backend_find_playing_sources (kaudio_backend_interface *backend, u32 playing[], u32 *count);
static void clear_buffer (kaudio_backend_interface *backend, u32 *buf_ptr, u32 amount);
static u32 openal_backend_find_free_buffer (kaudio_backend_interface *backend);
static const char *openal_backend_error_str (ALCenum err);
static b8 openal_backend_check_error (kaudio_backend_state *state);
static b8 channel_id_valid (kaudio_backend_state *state, u8 channel_id);

b8 openal_backend_initialize (kaudio_backend_interface *backend, const kaudio_backend_config *config) {
	if (backend) {
		backend->internal_state = kallocate(sizeof(kaudio_backend_state), MEMORY_TAG_AUDIO);
		kaudio_backend_state *state = backend->internal_state;

		// TODO: other platforms.
#if KPLATFORM_APPLE
		const char *lib_path = "/System/Library/Frameworks/OpenAL.framework/OpenAL";
#elif KPLATFORM_LINUX
		const char *lib_path = "libopenal.so.1";
#elif KPLATFORM_WINDOWS
		const char *lib_path = "OpenAL32";
#endif

		if (!platform_dynamic_library_load(lib_path, &state->openal_lib)) {
			KERROR("Failed to load OpenAL library. OpenAL audio plugin cannot be initialized.");
			return false;
		} else {
			KINFO("Successfully loaded OpenAL lib '%s'.", lib_path);
		}

		OAL_FUNCTION(alcCreateContext);
		OAL_FUNCTION(alcMakeContextCurrent);
		OAL_FUNCTION(alcProcessContext);
		OAL_FUNCTION(alcSuspendContext);
		OAL_FUNCTION(alcDestroyContext);
		OAL_FUNCTION(alcGetCurrentContext);
		OAL_FUNCTION(alcGetContextsDevice);
		OAL_FUNCTION(alcOpenDevice);
		OAL_FUNCTION(alcCloseDevice);
		OAL_FUNCTION(alcGetError);
		OAL_FUNCTION(alEnable);
		OAL_FUNCTION(alDisable);
		OAL_FUNCTION(alIsEnabled);
		OAL_FUNCTION(alGetString);
		OAL_FUNCTION(alGetBooleanv);
		OAL_FUNCTION(alGetIntegerv);
		OAL_FUNCTION(alGetFloatv);
		OAL_FUNCTION(alGetDoublev);
		OAL_FUNCTION(alGetBoolean);
		OAL_FUNCTION(alGetInteger);
		OAL_FUNCTION(alGetFloat);
		OAL_FUNCTION(alGetDouble);
		OAL_FUNCTION(alGetError);
		OAL_FUNCTION(alIsExtensionPresent);
		OAL_FUNCTION(alGetProcAddress);
		OAL_FUNCTION(alGetEnumValue);
		OAL_FUNCTION(alListenerf);
		OAL_FUNCTION(alListener3f);
		OAL_FUNCTION(alListenerfv);
		OAL_FUNCTION(alListeneri);
		OAL_FUNCTION(alListener3i);
		OAL_FUNCTION(alListeneriv);
		OAL_FUNCTION(alGetListenerf);
		OAL_FUNCTION(alGetListener3f);
		OAL_FUNCTION(alGetListenerfv);
		OAL_FUNCTION(alGetListeneri);
		OAL_FUNCTION(alGetListener3i);
		OAL_FUNCTION(alGetListeneriv);
		OAL_FUNCTION(alGenSources);
		OAL_FUNCTION(alDeleteSources);
		OAL_FUNCTION(alIsSource);
		OAL_FUNCTION(alSourcef);
		OAL_FUNCTION(alSource3f);
		OAL_FUNCTION(alSourcefv);
		OAL_FUNCTION(alSourcei);
		OAL_FUNCTION(alSource3i);
		OAL_FUNCTION(alSourceiv);
		OAL_FUNCTION(alGetSourcef);
		OAL_FUNCTION(alGetSource3f);
		OAL_FUNCTION(alGetSourcefv);
		OAL_FUNCTION(alGetSourcei);
		OAL_FUNCTION(alGetSource3i);
		OAL_FUNCTION(alGetSourceiv);
		OAL_FUNCTION(alSourcePlayv);
		OAL_FUNCTION(alSourceStopv);
		OAL_FUNCTION(alSourceRewindv);
		OAL_FUNCTION(alSourcePausev);
		OAL_FUNCTION(alSourcePlay);
		OAL_FUNCTION(alSourceStop);
		OAL_FUNCTION(alSourceRewind);
		OAL_FUNCTION(alSourcePause);
		OAL_FUNCTION(alSourceQueueBuffers);
		OAL_FUNCTION(alSourceUnqueueBuffers);
		OAL_FUNCTION(alGenBuffers);
		OAL_FUNCTION(alDeleteBuffers);
		OAL_FUNCTION(alIsBuffer);
		OAL_FUNCTION(alBufferData);
		OAL_FUNCTION(alBufferf);
		OAL_FUNCTION(alBuffer3f);
		OAL_FUNCTION(alBufferfv);
		OAL_FUNCTION(alBufferi);
		OAL_FUNCTION(alBuffer3i);
		OAL_FUNCTION(alBufferiv);
		OAL_FUNCTION(alGetBufferf);
		OAL_FUNCTION(alGetBuffer3f);
		OAL_FUNCTION(alGetBufferfv);
		OAL_FUNCTION(alGetBufferi);
		OAL_FUNCTION(alGetBuffer3i);
		OAL_FUNCTION(alGetBufferiv);
		OAL_FUNCTION(alDopplerFactor);
		OAL_FUNCTION(alDopplerVelocity);
		OAL_FUNCTION(alSpeedOfSound);
		OAL_FUNCTION(alDistanceModel);

		// Copy over the relevant frontend config properties.
		state->max_sources = config->audio_channel_count; // MAX_AUDIO_CHANNELS;
		state->chunk_size = config->chunk_size;
		state->frequency = config->frequency;
		state->channel_count = config->channel_count;
		state->max_count = config->max_count;
		state->datas = KALLOC_TYPE_CARRAY(kaudio_internal_data, state->max_count);

		state->buffer_count = 256; // FIXME: load from config.

		if (state->max_sources < 1) {
			KWARN("Audio plugin config.max_sources was configured as 0. Defaulting to 8.");
			state->max_sources = 8;
		}

		// Get the default device. TODO: enumerate devices and pick via ALC_ENUMERATION_EXT?
		state->device = state->kalcOpenDevice(0);
		// NOTE: Don't check for errors before context is created and made current.
		// openal_backend_check_error(state);
		if (!state->device) {
			KERROR("Unable to obtain OpenAL device. Plugin initialize failed.");
			return false;
		} else {
			KINFO("OpenAL Device acquired.");
		}

		// Get context and make it current.
		state->context = state->kalcCreateContext(state->device, 0);
		// NOTE: Don't check for errors before context is created and made current.
		// openal_backend_check_error();
		if (!state->kalcMakeContextCurrent(state->context)) {
			openal_backend_check_error(state);
		}

		// Disable the built-in attenuation models as the frontend handles this.
		state->kalDistanceModel(AL_NONE);

		// Configure the listener with some defaults.
		openal_backend_listener_position_set(backend, vec3_zero());
		openal_backend_listener_orientation_set(backend, vec3_forward(), vec3_up());

		// NOTE: zeroing out velocity
		state->kalListener3f(AL_VELOCITY, 0, 0, 0);
		openal_backend_check_error(state);

		state->sources = kallocate(sizeof(kaudio_plugin_source) * state->max_sources, MEMORY_TAG_AUDIO);
		// Create all sources.
		for (u32 i = 0; i < state->max_sources; ++i) {
			if (!openal_backend_channel_create(backend, &state->sources[i])) {
				KERROR("Unable to create audio source in OpenAL plugin.");
				return false;
			}
		}

		// Buffers
		// TODO: Should make a pool for this.
		state->buffers = kallocate(sizeof(u32) * state->buffer_count, MEMORY_TAG_ARRAY);
		state->kalGenBuffers(state->buffer_count, state->buffers);
		openal_backend_check_error(state);

		state->free_buffers = darray_create(u32);

		// Make sure all buffers are marked as free. Note that the array of buffers retrieved above must be used
		// directly, as there is no guarantee as to what the buffer ids will be. On one Windows installation, this
		// started at id 9. Yep. 9. Makes no sense, I'll tell ya hwhat. But there it is.
		for (u32 i = 0; i < state->buffer_count; ++i) {
			darray_push(state->free_buffers, &state->buffers[i]);
		}

		// NOTE: source generation, which is basically a sound emitter.
		KINFO("OpenAL plugin intialized.");

		return true;
	}

	KERROR("openal_backend_initialize requires a valid pointer to a backend.");
	return false;
}

void openal_backend_shutdown (kaudio_backend_interface *backend) {
	if (backend) {
		if (backend->internal_state) {
			// Destroy sources.
			for (u32 i = 0; i < backend->internal_state->max_sources; ++i) {
				openal_backend_channel_destroy(backend, &backend->internal_state->sources[i]);
			}

			kaudio_backend_state *state = backend->internal_state;

			// Give the threads a bit to stop.
			platform_sleep(100);

			kfree(backend->internal_state->sources);
			backend->internal_state->sources = KNULL;

			kfree(backend->internal_state->buffers);
			backend->internal_state->buffers = KNULL;

			kfree(backend->internal_state->datas);
			backend->internal_state->datas = KNULL;

			darray_destroy(backend->internal_state->free_buffers);
			backend->internal_state->free_buffers = KNULL;

			state->kalcDestroyContext(backend->internal_state->context);
			backend->internal_state->context = KNULL;

			state->kalcCloseDevice(backend->internal_state->device);
			backend->internal_state->device = KNULL;

			platform_dynamic_library_unload(&backend->internal_state->openal_lib);

			kfree(backend->internal_state);
			backend->internal_state = 0;
		}

		kzero_memory(backend, sizeof(kaudio_backend_interface));
	}
}

b8 openal_backend_update (kaudio_backend_interface *backend, struct frame_data *p_frame_data) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	for (u32 i = 0; i < state->max_sources; ++i) {
		kaudio_plugin_source *source = &state->sources[i];
		// For non-looping sounds, notify audio system of sound completion.
		ALint source_state;
		state->kalGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
		if (source_state == AL_STOPPED && source->previous_state == AL_PLAYING) {
			KTRACE("Playing -> Stopped");
			// Audio is newly stopped. Notify completion if not looping.
			if (!state->datas[source->current].is_looping) {
				_kaudio_system_play_completed(engine_systems_get()->audio_system, source->current, source->current_instance_id);
			}
		}

		source->previous_state = source_state;
	}

	return true;
}

b8 openal_backend_load (struct kaudio_backend_interface *backend, i32 channels, u32 sample_rate, u32 total_sample_count, u64 pcm_data_size, i16 *pcm_data, b8 is_stream, kaudio audio) {
	kaudio_backend_state *state = backend->internal_state;

	// Get the internal data.
	kaudio_internal_data *data = &state->datas[audio];
	data->is_stream = is_stream;
	data->channels = channels;
	data->sample_rate = sample_rate;
	data->total_sample_count = total_sample_count;
	data->pcm_data_size = pcm_data_size;
	data->pcm_data = kallocate(pcm_data_size, MEMORY_TAG_AUDIO);
	kcopy_memory(data->pcm_data, pcm_data, pcm_data_size);

	data->format = AL_FORMAT_MONO16;
	if (data->channels == 2) {
		data->format = AL_FORMAT_STEREO16;
		// TODO: maybe do this on the frontend?
		// If the asset is stereo, get a downmixed version of the audio so it can be used
		// as a "3D" sound if need be.
		data->mono_pcm_data = kaudio_downmix_stereo_to_mono(data->pcm_data, data->total_sample_count);
		data->downmixed_size = (data->total_sample_count / 2) * sizeof(i16);
	} else {
		// Asset was already mono, just point to the pcm data.
		data->mono_pcm_data = data->pcm_data;
		data->downmixed_size = 0; // Set to zero to indicate this shouldn't be freed separately.
	}

	data->total_samples_left = total_sample_count;

	if (is_stream) {
		// Streams need buffers to be used back to back.
		for (u32 i = 0; i < OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT; ++i) {
			data->streaming_buffers[i] = openal_backend_find_free_buffer(backend);
			if (data->streaming_buffers[i] == INVALID_ID) {
				KERROR("Unable to load streaming audio due to no buffers being available.");
				return 0;
			}

			// Streams loop by default.
			data->is_looping = true;
		}
		openal_backend_check_error(state);
	} else {
		// Non-streams only need one buffer.
		data->buffer = openal_backend_find_free_buffer(backend);
		if (data->buffer == INVALID_ID) {
			KERROR("Unable to open audio file due to no buffers being available.");
			return false;
		}
		openal_backend_check_error(state);

		if (data->total_samples_left > 0) {
			// Load the whole thing into the buffer.
			state->kalBufferData(data->buffer, data->format, (i16 *)data->pcm_data, data->total_samples_left * data->channels, data->sample_rate);
			openal_backend_check_error(state);
		}

		// However, if the asset is stereo, a second buffer is required for the mono data.
		data->mono_buffer = openal_backend_find_free_buffer(backend);
		if (data->mono_buffer == INVALID_ID) {
			KERROR("Unable to open audio file due to no buffers being available.");
			return false;
		}
		openal_backend_check_error(state);
		if (data->total_samples_left > 0) {
			// Load the whole thing into the buffer.
			state->kalBufferData(data->mono_buffer, AL_FORMAT_MONO16, (i16 *)data->mono_pcm_data, data->downmixed_size ? data->downmixed_size : data->total_samples_left, data->sample_rate);
			openal_backend_check_error(state);
		}

		// Non-streams do not loop by default.
		data->is_looping = false;
	}

	return true;
}

void openal_backend_unload (struct kaudio_backend_interface *backend, kaudio audio) {
	kaudio_backend_state *state = backend->internal_state;

	// Get the internal data.
	kaudio_internal_data *data = &state->datas[audio];

	if (data->pcm_data && data->pcm_data_size) {
		kfree(data->pcm_data);
		data->pcm_data = KNULL;
		data->pcm_data_size = 0;

		if (data->mono_pcm_data == data->pcm_data) {
			data->mono_pcm_data = KNULL;
			data->downmixed_size = 0;
		}
	}

	if (data->mono_pcm_data && data->downmixed_size) {
		kfree(data->mono_pcm_data);
		data->mono_pcm_data = KNULL;
		data->downmixed_size = 0;
	}

	clear_buffer(backend, &data->buffer, 0);

	// FIXME: Mark entry as available for use
}

b8 openal_backend_listener_position_set (kaudio_backend_interface *backend, vec3 position) {
	if (!backend) {
		KERROR("openal_backend_listener_position_set requires a valid pointer to a plugin.");
		return false;
	}

	backend->internal_state->kalListener3f(AL_POSITION, position.x, position.y, position.z);
	openal_backend_check_error(backend->internal_state);

	return true;
}

b8 openal_backend_listener_orientation_set (kaudio_backend_interface *backend, vec3 forward, vec3 up) {
	if (!backend) {
		KERROR("openal_backend_listener_orientation_set requires a valid pointer to a plugin.");
		return false;
	}

	ALfloat listener_orientation[] = {forward.x, forward.y, forward.z, up.x, up.y, up.z};
	backend->internal_state->kalListenerfv(AL_ORIENTATION, listener_orientation);
	return openal_backend_check_error(backend->internal_state);
}

b8 openal_backend_channel_gain_set (kaudio_backend_interface *backend, u8 channel_id, f32 gain) {
	if (!backend) {
		return false;
	}
	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];
		state->kalSourcef(source->id, AL_GAIN, gain);
		return openal_backend_check_error(state);
	}

	KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
	return false;
}

b8 openal_backend_channel_pitch_set (kaudio_backend_interface *backend, u8 channel_id, f32 pitch) {
	if (!backend) {
		return false;
	}
	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];
		state->kalSourcef(source->id, AL_PITCH, pitch);
		return openal_backend_check_error(state);
	}

	KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
	return false;
}

b8 openal_backend_channel_position_set (kaudio_backend_interface *backend, u8 channel_id, vec3 position) {
	if (!backend) {
		return false;
	}
	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];
		state->kalSource3f(source->id, AL_POSITION, position.x, position.y, position.z);
		return openal_backend_check_error(state);
	}

	KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
	return false;
}

b8 openal_backend_channel_looping_set (kaudio_backend_interface *backend, u8 channel_id, b8 looping) {
	if (!backend) {
		return false;
	}
	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];
		state->kalSourcei(source->id, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
		return openal_backend_check_error(state);
	}

	KERROR("Plugin pointer invalid or source id is invalid: %u.", channel_id);
	return false;
}

b8 openal_backend_channel_play (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];
		kmutex_lock(&source->data_mutex);
		if (source->current) {
			source->trigger_play = true;
		}
		kmutex_unlock(&source->data_mutex);
	}

	return true;
}

b8 openal_backend_channel_play_audio (kaudio_backend_interface *backend, kaudio audio, u16 instance_id, kaudio_space audio_space, u8 channel_id) {
	if (!channel_id_valid(backend->internal_state, channel_id)) {
		return false;
	}

	KTRACE("Play on channel %d", channel_id);
	kaudio_backend_state *state = backend->internal_state;
	kaudio_internal_data *data = &state->datas[audio];

	// Assign the sound's buffer to the source.
	kaudio_plugin_source *source = &state->sources[channel_id];
	kmutex_lock(&source->data_mutex);
	source->current_audio_space = audio_space;

	if (data->is_stream) {
		// Load data into all buffers initially.
		b8 result = true;
		for (u32 i = 0; i < OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT; ++i) {
			if (!stream_data(backend, data->streaming_buffers[i], audio_space, audio)) {
				KERROR("Failed to stream data to buffer &u in music file. File load failed.", i);
				result = false;
				break;
			}
		}
		// Queue up new buffers.
		state->kalSourceQueueBuffers(source->id, OPENAL_BACKEND_STREAM_MAX_BUFFER_COUNT, data->streaming_buffers);
		openal_backend_check_error(state);
		if (!result) {
			KERROR("Failed to stream audio data. See logs for details.");
			return false;
		}
	} else {

		// If a sound is currently playing here, clip it off by stopping first.
		state->kalSourceStop(source->id);
		openal_backend_check_error(state);
		state->kalSourcei(source->id, AL_BUFFER, 0);
		openal_backend_check_error(state);

		// Unqueue any existing buffers that are queued.
		// Processed buffers.
		ALint processed_buffer_count = 0;
		state->kalGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);
		openal_backend_check_error(state);
		for (i32 i = 0; i < processed_buffer_count; ++i) {
			ALuint buffer_id = 0;
			state->kalSourceUnqueueBuffers(source->id, 1, &buffer_id);
			KTRACE("Play - unqueued processed buffer %u", buffer_id);
			openal_backend_check_error(state);
		}

		// Queued buffers.
		ALint queued_buffer_count = 0;
		state->kalGetSourcei(source->id, AL_BUFFERS_QUEUED, &queued_buffer_count);
		openal_backend_check_error(state);
		for (i32 i = 0; i < queued_buffer_count; ++i) {
			ALuint buffer_id = INVALID_ID;
			state->kalSourceUnqueueBuffers(source->id, 1, &buffer_id);
			KTRACE("Play - unqueued queued buffer %u", buffer_id);
			openal_backend_check_error(state);
		}

		// Queue up sound buffer.
		ALuint *bids = 0;
		if (data->channels == 2 && audio_space == KAUDIO_SPACE_3D) {
			// If stereo sound but wanting to play 3d, use the mono buffer.
			state->kalSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
			openal_backend_check_error(state);
			bids = &data->mono_buffer;
		} else {
			if (data->channels == 2) {
				// stereo, but using 2d sound. Play as normal.
				state->kalSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
				openal_backend_check_error(state);
				bids = &data->buffer;
			} else if (audio_space == KAUDIO_SPACE_3D) {
				// Mono sound, but want to play 3D. Play as normal.
				state->kalSourcei(source->id, AL_SOURCE_RELATIVE, AL_FALSE);
				openal_backend_check_error(state);
				bids = &data->buffer;
			} else {
				// Mono sound, but play 2D. Set source relative, making the source
				// align with the listener, effectively making the sound 2d.
				state->kalSourcei(source->id, AL_SOURCE_RELATIVE, AL_TRUE);
				openal_backend_check_error(state);
				bids = &data->buffer;
			}
		}
		state->kalSourcei(source->id, AL_BUFFER, 0);
		openal_backend_check_error(state);

		ALint source_state;
		state->kalGetSourcei(source->id, AL_SOURCE_STATE, &source_state);

		state->kalSourceQueueBuffers(source->id, 1, bids);
		openal_backend_check_error(state);
	}

	// Assign current, set flags, play, etc.
	source->current = audio;
	source->current_instance_id = instance_id;
	state->kalSourcePlay(source->id);
	kmutex_unlock(&source->data_mutex);

	return true;
}

b8 openal_backend_channel_stop (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		kaudio_plugin_source *source = &state->sources[channel_id];

		state->kalSourceStop(source->id);

		// Detach all buffers.
		state->kalSourcei(source->id, AL_BUFFER, 0);
		openal_backend_check_error(state);

		// Rewind.
		state->kalSourceRewind(source->id);

		source->current = 0;

		return true;
	}
	return false;
}

b8 openal_backend_channel_pause (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		// Trigger a pause if the source is currently playing.
		kaudio_plugin_source *source = &state->sources[channel_id];
		ALint source_state;
		state->kalGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
		if (source_state == AL_PLAYING) {
			state->kalSourcePause(source->id);
		}

		return true;
	}
	return false;
}

b8 openal_backend_channel_resume (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		// Trigger a resume if the source is currently paused.
		kaudio_plugin_source *source = &state->sources[channel_id];
		ALint source_state;
		state->kalGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
		if (source_state == AL_PAUSED) {
			state->kalSourcePlay(source->id);
		}

		return true;
	}
	return false;
}

b8 openal_backend_channel_is_playing (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		ALint source_state;
		state->kalGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
		return source_state == AL_PLAYING;
	}
	return false;
}

b8 openal_backend_channel_is_paused (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		ALint source_state;
		state->kalGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
		return source_state == AL_PAUSED;
	}
	return false;
}

b8 openal_backend_channel_is_stopped (kaudio_backend_interface *backend, u8 channel_id) {
	if (!backend) {
		return false;
	}

	kaudio_backend_state *state = backend->internal_state;
	if (channel_id_valid(state, channel_id)) {
		ALint source_state;
		state->kalGetSourcei(state->sources[channel_id].id, AL_SOURCE_STATE, &source_state);
		return source_state == AL_STOPPED || source_state == AL_INITIAL;
	}
	return false;
}

static b8 stream_data (kaudio_backend_interface *backend, ALuint buffer, kaudio_space audio_space, kaudio audio) {
	kaudio_backend_state *state = backend->internal_state;

	kaudio_internal_data *data = &state->datas[audio];

	// Figure out how many samples can be taken.
	// TODO: This might be _way_ too much between chunk size and samples (maybe samples left * channels?)
	u64 sample_count = KMIN(data->total_samples_left, state->chunk_size);

	// 0 means the end of the file has been reached, and either the stream stops or needs to start over.
	if (sample_count == 0) {
		KTRACE("End of file reached. Returning false.");
		return false;
	}
	openal_backend_check_error(state);
	// Load the data into the buffer. Just a pointer into the pcm_data at an offset.
	u64 pos = data->total_sample_count - data->total_samples_left;

	// Figure out the format and source.
	i16 *data_source = 0;
	ALenum format = data->format;
	if (data->channels == 2) {
		if (audio_space == KAUDIO_SPACE_3D) {
			// Use the mono data.
			format = AL_FORMAT_MONO16;
			data_source = data->mono_pcm_data;
		} else {
			// Stereo is fine
			format = AL_FORMAT_STEREO16;
			data_source = data->pcm_data;
		}
	} else if (data->channels == 1) {
		// Only mono data exists to use, so use it.
		// The front-end should handle positioning the channel so it comes across in stereo (2d)
		format = AL_FORMAT_MONO16;
		data_source = data->mono_pcm_data;
	} else {
		KFATAL("Unsupported channel count %u", data->channels);
	}
	// Get the data offset.
	i16 *streamed_data = data_source + pos;
	if (streamed_data) {
		state->kalBufferData(buffer, format, streamed_data, sample_count * sizeof(ALshort), data->sample_rate);
		openal_backend_check_error(state);
	} else {
		KERROR("Error streaming data. Check logs for more info.");
		return false;
	}

	// Update the samples remaining.
	data->total_samples_left -= sample_count;

	return true;
}

static b8 openal_backend_stream_update (kaudio_backend_interface *backend, kaudio_plugin_source *source) {
	kaudio_backend_state *state = backend->internal_state;

	// It's possible sometimes for this to not be playing, even with buffers queued up.
	// Make sure to handle this case.
	ALint source_state;
	state->kalGetSourcei(source->id, AL_SOURCE_STATE, &source_state);
	if (source_state != AL_PLAYING) {
		KTRACE("Stream update, play needed for source id: %u", source->id);
		state->kalSourcePlay(source->id);
	}

	// Check for processed buffers that can be popped off.
	ALint processed_buffer_count = 0;
	state->kalGetSourcei(source->id, AL_BUFFERS_PROCESSED, &processed_buffer_count);

	while (processed_buffer_count--) {
		ALuint buffer_id = 0;
		state->kalSourceUnqueueBuffers(source->id, 1, &buffer_id);

		// If this returns false, there was nothing further to read (i.e at the end of the file).
		if (!stream_data(backend, buffer_id, source->current_audio_space, source->current)) {
			KTRACE("stream_data returned false");
			b8 done = true;

			kaudio_internal_data *data = &state->datas[source->current];

			// If set to loop, start over at the beginning.
			if (data->is_looping) {
				KTRACE("Internal audio set to loop. Rewinding and starting over.");
				// Loop around.
				data->total_samples_left = data->total_sample_count;
				/* audio->rewind(audio); */
				done = !stream_data(backend, buffer_id, source->current_audio_space, source->current);
			}

			// If not set to loop, the sound is done playing.
			if (done) {
				KTRACE("Sound is done playing.");
				return false;
			}
		}
		// Queue up the next buffer.
		state->kalSourceQueueBuffers(source->id, 1, &buffer_id);
	}

	return true;
}

static b8 source_work_thread (void *params) {
	ksource_work_thread_params *typed_params = params;
	kaudio_backend_interface *backend = typed_params->backend;
	kaudio_backend_state *state = backend->internal_state;
	kaudio_plugin_source *source = typed_params->source;

	// Release this right away since it's no longer needed.
	kfree(params);

	/* KDEBUG("Audio source thread starting..."); */

	b8 do_break = false;
	while (!do_break) {
		if (!source->data_mutex.internal_data) {
			// This can happen during unexpected shutdown, and if so kill the thread.
			return true;
		}
		kmutex_lock(&source->data_mutex);
		if (source->trigger_exit) {
			do_break = true;
		}
		if (source->trigger_play) {
			state->kalSourcePlay(source->id);
			source->trigger_play = false;
		}
		kmutex_unlock(&source->data_mutex);

		if (source->current != INVALID_KAUDIO) {
			kaudio_internal_data *data = &state->datas[source->current];
			if (data->is_stream) {
				// If currently playing stream, try updating the stream.
				openal_backend_stream_update(backend, source);
			}
		}

		platform_sleep(2);
	}

	/* KDEBUG("Audio source thread shutting down."); */
	return true;
}

static b8 source_set_defaults (kaudio_backend_interface *backend, kaudio_plugin_source *source, b8 reset_use) {
	// Mark it as not in use.
	if (reset_use) {
		source->current = 0;
	}

	// Set some defaults. FIXME: Define these instead of having magic numbers.
	if (!openal_backend_channel_gain_set(backend, source->id - 1, 1.0f)) {
		KERROR("Failed to set source default gain.");
		return false;
	}
	if (!openal_backend_channel_pitch_set(backend, source->id - 1, 1.0f)) {
		KERROR("Failed to set source default pitch.");
		return false;
	}
	if (!openal_backend_channel_position_set(backend, source->id - 1, vec3_zero())) {
		KERROR("Failed to set source default position.");
		return false;
	}
	if (!openal_backend_channel_looping_set(backend, source->id - 1, false)) {
		KERROR("Failed to set source default looping.");
		return false;
	}

	return true;
}

static b8 openal_backend_channel_create (kaudio_backend_interface *backend, kaudio_plugin_source *out_source) {
	if (!backend || !out_source) {
		KERROR("openal_backend_channel_create requires valid pointers to a plugin and out_source.");
		return false;
	}

	backend->internal_state->kalGenSources((ALuint)1, &out_source->id);
	if (!openal_backend_check_error(backend->internal_state)) {
		KERROR("Failed to create source.");
		return false;
	}

	if (!source_set_defaults(backend, out_source, true)) {
		KERROR("Failed to set source defaults, and thus failed to create source.");
	}

	// Create the source worker thread's mutex.
	kmutex_create(&out_source->data_mutex);

	// Also create the worker thread itself for this source.
	ksource_work_thread_params *params = kallocate(sizeof(ksource_work_thread_params), MEMORY_TAG_AUDIO);
	params->source = out_source;
	params->backend = backend;
	kthread_create(source_work_thread, params, true, &out_source->thread);

	return true;
}

static void openal_backend_channel_destroy (kaudio_backend_interface *backend, kaudio_plugin_source *source) {
	if (backend && source) {
		backend->internal_state->kalDeleteSources(1, &source->id);
		kmutex_destroy(&source->data_mutex);
		kthread_destroy(&source->thread);
		kzero_memory(source, sizeof(kaudio_plugin_source));
		source->id = INVALID_ID;
	}
}

static void openal_backend_find_playing_sources (kaudio_backend_interface *backend, u32 playing[], u32 *count) {
	if (!backend || !count) {
		return;
	}

	kaudio_backend_state *state = backend->internal_state;

	ALint source_state = 0;
	for (u32 i = 0; i < state->max_sources; ++i) {
		state->kalGetSourcei(state->sources[i - 1].id, AL_SOURCE_STATE, &source_state);
		if (source_state == AL_PLAYING) {
			playing[(*count)] = state->sources[i - 1].id;
			(*count)++;
		}
	}
}

static void clear_buffer (kaudio_backend_interface *backend, u32 *buf_ptr, u32 amount) {
	if (backend) {

		kaudio_backend_state *state = backend->internal_state;

		for (u32 a = 0; a < amount; ++a) {
			for (u32 i = 0; i < state->buffer_count; ++i) {
				if (buf_ptr[a] == state->buffers[i]) {
					darray_push(state->free_buffers, &i);
					return;
				}
			}
		}
	}
	KWARN("Buffer could not be cleared.");
}

static u32 openal_backend_find_free_buffer (kaudio_backend_interface *backend) {
	if (backend) {

		kaudio_backend_state *state = backend->internal_state;
		u32 free_count = darray_length(state->free_buffers);

		// If there are no free buffers, attempt to free one first.
		if (free_count == 0) {
			KINFO("openal_backend_find_free_buffer() - no free buffers, attempting to free an existing one.");
			if (!openal_backend_check_error(state)) {
				return false;
			}

			u32 playing_source_count = 0;
			u32 *playing_sources = kallocate(sizeof(u32) * state->max_sources, MEMORY_TAG_ARRAY);
			openal_backend_find_playing_sources(backend, playing_sources, &playing_source_count);
			// Avoid a crash when calling alGetSourcei while checking for freeable buffers. Resumed below.
			for (u32 i = 0; i < playing_source_count; ++i) {
				state->kalSourcePause(state->sources[i - 1].id);
				openal_backend_check_error(state);
			}

			ALint to_be_freed = 0;
			ALuint buffers_freed = 0;
			for (u32 i = 0; i < state->buffer_count; ++i) {
				// Get number of buffers to be freed for this source.
				state->kalGetSourcei(state->sources[i - 1].id, AL_BUFFERS_PROCESSED, &to_be_freed);
				openal_backend_check_error(state);
				if (to_be_freed > 0) {
					// If there are buffers to be freed, free them.
					openal_backend_check_error(state);
					state->kalSourceUnqueueBuffers(state->sources[i - 1].id, to_be_freed, &buffers_freed);
					openal_backend_check_error(state);

					clear_buffer(backend, &buffers_freed, to_be_freed);
				}
			}

			// Resume the paused sources.
			for (u32 i = 0; i < playing_source_count; ++i) {
				kaudio_plugin_source *source = &state->sources[i - 1];
				state->kalSourcePlay(source->id);
				openal_backend_check_error(state);
			}
			kfree(playing_sources);
		}

		// Check free count again, this time there must be at least one or there is an error condition.
		free_count = darray_length(state->free_buffers);
		if (free_count < 1) {
			KERROR("Could not find or clear a buffer. This means too many things are being played at once.");
			return INVALID_ID;
		}

		// Nab the first one off the top
		u32 out_buffer_id;
		darray_pop_at(state->free_buffers, 0, &out_buffer_id);

		free_count = darray_length(state->free_buffers);
		KTRACE("Found free buffer id %u", out_buffer_id);
		KDEBUG("There are now %u free buffers remaining.", free_count);
		return out_buffer_id;
	}

	return INVALID_ID;
}

static const char *openal_backend_error_str (ALCenum err) {
	switch (err) {
	case AL_INVALID_VALUE:
		return "AL_INVALID_VALUE";
	case AL_INVALID_NAME:
		return "AL_INVALID_NAME or ALC_INVALID_DEVICE";
	case AL_INVALID_OPERATION:
		return "AL_INVALID_OPERATION";
	case AL_NO_ERROR:
		return "AL_NO_ERROR";
	case AL_OUT_OF_MEMORY:
		return "AL_OUT_OF_MEMORY or could not find audio device";
	default:
		return "Unknown/unhandled error";
	}
}

static b8 openal_backend_check_error (kaudio_backend_state *state) {
	ALCenum error = state->kalGetError();
	if (error != AL_NO_ERROR) {
		KERROR("OpenAL error %u: '%s'", error, openal_backend_error_str(error));
		return false;
	}
	return true;
}

static b8 channel_id_valid (kaudio_backend_state *state, u8 channel_id) {
	return state && channel_id < state->max_sources;
}
