#include "engine.h"

#include <containers/darray.h>
#include <containers/registry.h>
#include <identifiers/khandle.h>
#include <identifiers/uuid.h>
#include <logger.h>
#include <memory/allocators/linear_allocator.h>
#include <memory/kmemory.h>
#include <platform/filesystem.h>
#include <platform/platform.h>
#include <platform/vfs.h>
#include <strings/kstring.h>
#include <time/kclock.h>

// Version reporting
#include "core_render_types.h"
#include "debug/kassert.h"
#include "defines.h"
#include "kohi.runtime_version.h"

#include "application/application_config.h"
#include "application/application_types.h"
#include "audio/audio_frontend.h"
#include "console.h"
#include "core/event.h"
#include "core/input.h"
#include "core/kvar.h"
#include "core/metrics.h"
#include "core/sim_clock.h"
#include "core/agent_channel.h"
#include "physics/kphysics_system.h"
#include "frame_data.h"
#include "plugins/plugin_types.h"
#include "renderer/kmaterial_renderer.h"
#include "renderer/renderer_frontend.h"

// systems
#include "strings/kname.h"
#include "strings/kstring_id.h"
#include "systems/asset_system.h"
#include "systems/font_system.h"
#include "systems/job_system.h"
#include "systems/kcamera_system.h"
#include "systems/kmaterial_system.h"
#include "systems/kmatrix_system.h"
#include "systems/kmodel_system.h"
#include "systems/kshader_system.h"
#include "systems/ktimeline_system.h"
#include "systems/ktransform_system.h"
#include "systems/light_system.h"
#include "systems/plugin_system.h"
#include "systems/texture_system.h"

struct kwindow;

typedef struct engine_state_t {
	application *app;
	b8 is_running;
	/** @brief Set when the run stopped because something failed, so engine_run returns false. */
	b8 failed;
	b8 is_suspended;
	kclock clock;
	f64 last_time;

	kclock startup_clock;

	// An allocator used for per-frame allocations, that is reset every frame.
	linear_allocator frame_allocator;

	frame_data p_frame_data;

	// Decides how many fixed simulation steps run each frame.
	ksim_clock sim_clock;

	// Platform console consumer.
	u8 platform_consumer_id;
	// Log file console consumer.
	u8 logfile_consumer_id;
	// Log file handle.
	file_handle log_file_handle;

	// Engine system states.
	engine_system_states systems;

	// External system state registry.
	kregistry external_systems_registry;

	kruntime_plugin *renderer_plugin;
	kruntime_plugin *audio_plugin;

	// darray List of created windows.
	kwindow *windows;

} engine_state_t;

static engine_state_t *engine_state;

// frame allocator functions.
static void *frame_allocator_allocate (u64 size) {
	if (!engine_state) {
		return 0;
	}

	return linear_allocator_allocate(&engine_state->frame_allocator, size);
}
static void frame_allocator_free (void *block, u64 size) {
	// NOTE: Linear allocator doesn't free, so this is a no-op
	/* if (engine_state) {
	} */
}

static void frame_allocator_free_all (void) {
	if (engine_state) {
		// Don't wipe the memory each time, to save on performance.
		linear_allocator_free_all(&engine_state->frame_allocator, false);
	}
}

static u64 frame_allocator_total_space (void) {
	return engine_state ? engine_state->frame_allocator.total_size : 0;
}
static u64 frame_allocator_allocated (void) {
	return engine_state ? engine_state->frame_allocator.allocated : 0;
}

// Event handlers
static b8 engine_on_event (u16 code, void *sender, void *listener_inst, event_context context);
static void engine_on_window_closed (const struct kwindow *window);
static void engine_on_window_resized (const struct kwindow *window);
static void engine_on_process_key (keys key, b8 pressed, b8 is_repeat);
static void engine_on_process_mouse_button (mouse_buttons button, b8 pressed);
static void engine_on_process_mouse_move (i16 x, i16 y);
static void engine_on_process_mouse_wheel (i8 z_delta);
static void engine_on_paste (kclipboard_context context);
static b8 engine_log_file_write (void *engine, log_level level, const char *message);
static b8 engine_platform_console_write (void *platform, log_level level, const char *message);
static b8 load_game_lib (application *app);
static void watched_file_updated (u32 watcher_id, const char *file_path, b8 is_binary, void *context);

static void on_memory_dump (console_command_context context) {
	char *mem_usage = get_memory_usage_str();
	KINFO(mem_usage);
	string_free(mem_usage);
}

static void on_sim_pause (console_command_context context) {
	engine_simulation_pause_set(true);
}

static void on_sim_resume (console_command_context context) {
	engine_simulation_pause_set(false);
}

static void on_sim_step (console_command_context context) {
	u32 count = 1;
	if (context.argument_count == 1 && !string_to_u32(context.arguments[0].value, &count)) {
		KERROR("sim_step: '%s' is not a step count. Usage: sim_step [count]", context.arguments[0].value);
		return;
	}
	if (!engine_simulation_paused()) {
		KWARN("sim_step: the simulation is running. Use sim_pause first.");
		return;
	}
	engine_simulation_steps_request(count);
}

static void on_sim_status (console_command_context context) {
	const ksim_clock *sim = &engine_state->sim_clock;
	KINFO("Simulation: %s mode, %s, step %llu, %.3f s simulated, step %.6f s, %u steps pending.",
		  ksim_clock_mode_name(sim->mode), sim->paused ? "paused" : "running",
		  sim->step_count, sim->sim_seconds, sim->step_seconds, sim->pending_steps);
}

b8 engine_create (application *app, const char *app_config_path, const char *game_lib_name) {
	KASSERT(app_config_path);

	// Startup clock, but created here until engine state exists.
	// Doing this here so that this begins timing as soon as possible within the application.
	// Technically there are a couple small operations that begin before this, but the timing
	// of those should be negligible.
	kclock temp_clock = {0};
	kclock_start(&temp_clock);

	if (app->engine_state) {
		KERROR("engine_create called more than once.");
		return false;
	}

	// Memory system must be the first thing to be stood up.
	memory_system_configuration memory_system_config = {};
	memory_system_config.total_alloc_size = GIBIBYTES(2);
	if (!memory_system_initialize(memory_system_config)) {
		KERROR("Failed to initialize memory system; shutting down.");
		return false;
	}

	// Seed the uuid generator.
	// TODO: A better seed here.
	uuid_seed(101);

	// Metrics
	metrics_initialize();

	// Stand up the engine state.
	app->engine_state = kallocate(sizeof(engine_state_t), MEMORY_TAG_ENGINE);
	engine_state = app->engine_state;
	engine_state->app = app;
	engine_state->is_running = false;
	engine_state->is_suspended = false;
	engine_state->startup_clock = temp_clock; // Set the already-started clock.

	// Setup a registry for external systems to register themselves to.
	kregistry_create(&engine_state->external_systems_registry);

	// Engine systems
	engine_system_states *systems = &engine_state->systems;

	// Get/parse application config. This happens before platform startup because the platform
	// needs to know whether to connect to a display server (see 'headless').
	const char *app_file_content = filesystem_read_entire_text_file(app_config_path);
	if (!app_file_content) {
		KFATAL("Failed to read app_config.kson file text. Application cannot start.");
		return false;
	}

	if (!application_config_parse_file_content(app_file_content, &app->app_config)) {
		KFATAL("Failed to parse application config. Cannot start.");
		return false;
	}
	string_free(app_file_content);

	// Platform initialization first. NOTE: NOT window creation - that should happen much later.
	{
		platform_system_config plat_config = {0};
		plat_config.application_name = app->app_config.name;
		plat_config.headless = app->app_config.headless;
		systems->platform_memory_requirement = 0;
		platform_system_startup(&systems->platform_memory_requirement, 0, &plat_config);
		systems->platform_system = kallocate(systems->platform_memory_requirement, MEMORY_TAG_ENGINE);
		if (!platform_system_startup(&systems->platform_memory_requirement, systems->platform_system, &plat_config)) {
			KERROR("Failed to initialize platform layer.");
			return false;
		}
	}

	// Event system needs to be setup as early as possible so other systems can register with it.
	{
		event_system_initialize(&systems->event_system_memory_requirement, 0, 0);
		systems->event_system = kallocate(systems->event_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!event_system_initialize(&systems->event_system_memory_requirement, systems->event_system, 0)) {
			KERROR("Failed to initialize event system.");
			return false;
		}

		// After event system, register input callbacks.
		platform_register_window_closed_callback(engine_on_window_closed);
		platform_register_window_resized_callback(engine_on_window_resized);
	}

	// Console system
	{
		console_initialize(&systems->console_memory_requirement, 0, 0);
		systems->console_system = kallocate(systems->console_memory_requirement, MEMORY_TAG_ENGINE);
		if (!console_initialize(&systems->console_memory_requirement, systems->console_system, 0)) {
			KERROR("Failed to initialize console.");
			return false;
		}

		// Platform should then register as a console consumer.
		console_consumer_register(systems->platform_system, engine_platform_console_write, &engine_state->platform_consumer_id);
		// Setup the engine as another console consumer, which now owns the "console.log" file.
		// Create new/wipe existing log file, then open it.
		const char *log_filename = "console.log";
		if (!filesystem_open(log_filename, FILE_MODE_WRITE, false, &engine_state->log_file_handle)) {
			KFATAL("Unable to open '%s' for writing.", log_filename);
			return false;
		}
		console_consumer_register(engine_state, engine_log_file_write, &engine_state->logfile_consumer_id);
	}

	// Gather and report hardware info
	{
		ksystem_info system_info = {0};
		platform_system_info_collect(&system_info);

		KINFO("SYSTEM_OS\t%s %s (%s kernel: %s, build: %s)", system_info.os_name, system_info.os_version, system_info.distro, system_info.kernel_version, system_info.os_build);

		KINFO("SYSTEM_CPU\t%s (%u CPUs) ~%.1fGHz", system_info.cpu_name, system_info.logical_cores, system_info.cpu_mhz / 1000.0f);
		KINFO("SYSTEM_CPU_CORES\t%u Physical, %u Logical", system_info.physical_cores, system_info.logical_cores);
		KINFO("SYSTEM_CPU_FEATURES\tSSE=%s SSE2=%s SSE3=%s SSSE3=%s SSE4.1=%s SSE4.2=%s AVX=%s AVX2=%s",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSE_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSE2_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSE3_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSSE3_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSE41_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_SSE42_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_AVX_BIT) ? "yes" : "no",
			  FLAG_GET(system_info.features, KCPU_FEATURE_FLAG_AVX2_BIT) ? "yes" : "no");

		char *ram_speed = 0;
		if (system_info.ram_speed_mhz) {
			ram_speed = string_format("%uMHz", system_info.ram_speed_mhz);
		} else {
			ram_speed = "Unknown";
		}
		KINFO("SYSTEM_MEMORY\t%.2f GB (%.2f GiB available) Speed: %s", system_info.ram_total_bytes / (f64)GIBIBYTES(1), system_info.ram_available_bytes / (f64)GIBIBYTES(1), ram_speed);
		if (system_info.ram_speed_mhz) {
			string_free(ram_speed);
		}

		// Storage
		for (u32 i = 0; i < system_info.storage_count; ++i) {
			kstorage_info *s = &system_info.storage[i];

			f32 total_space = 0;
			f32 free_space = 0;
			const char *total_unit = get_unit_for_size(s->total_bytes, &total_space);
			const char *free_unit = get_unit_for_size(s->free_bytes, &free_space);
			KINFO(
				"SYSTEM_STORAGE\t%s\t%s\tSYSTEM_TOTAL_DISC_SPACE\t%.3f%s\tSYSTEM_FREE_DISC_SPACE\t%.3f%s",
				s->mount_point,
				kdrive_type_to_string(s->type),
				total_space,
				total_unit,
				free_space,
				free_unit);
		}
	}

	KASSERT(console_command_register("memory_dump", 0, 0, 0, on_memory_dump));

	engine_state->sim_clock = ksim_clock_create(app->app_config.simulation_mode, app->app_config.fixed_step_hz, app->app_config.max_steps_per_frame);
	KASSERT(console_command_register("sim_pause", 0, 0, 0, on_sim_pause));
	KASSERT(console_command_register("sim_resume", 0, 0, 0, on_sim_resume));
	KASSERT(console_command_register("sim_step", 0, 1, 0, on_sim_step));
	KASSERT(console_command_register("sim_status", 0, 0, 0, on_sim_status));

	// Report runtime version
#if KOHI_RELEASE
	const char *build_type = "Release";
#elif KOHI_DEBUG
	const char *build_type = "Debug";
#else
	const char *build_type = "Unknown";
#endif
	KINFO("Kohi Runtime %s (%s)", KVERSION, build_type);

	// Create application
	{
		app->game_library_name = string_duplicate(game_lib_name);
		app->game_library_loaded_name = string_format("%s_loaded", app->game_library_name);

		// Application configuration.
		platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
		while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
			const char *prefix = platform_dynamic_library_prefix();
			const char *extension = platform_dynamic_library_extension();
			char *source_file = string_format("%s%s%s", prefix, app->game_library_name, extension);
			char *target_file = string_format("%s%s%s", prefix, app->game_library_loaded_name, extension);
			err_code = platform_copy_file(source_file, target_file, true);
			string_free(source_file);
			string_free(target_file);
			if (err_code == PLATFORM_ERROR_FILE_LOCKED) {
				platform_sleep(100);
			}
		}
		if (err_code != PLATFORM_ERROR_SUCCESS) {
			KERROR("File copy failed!");
			return false;
		}

		if (!load_game_lib(app)) {
			KERROR("Initial game lib load failed!");
			return false;
		}

		// Put a file watch on the game lib and hot-reload when it changes.
		const char *prefix = platform_dynamic_library_prefix();
		const char *extension = platform_dynamic_library_extension();
		char *path = string_format("%s%s%s", prefix, app->game_library_name, extension);

		if (!platform_watch_file(
				path,
				true,
				watched_file_updated,
				app,
				0,
				0,
				&app->game_library.watch_id)) {
			KERROR("Failed to watch the game library!");
			string_free(path);
			return false;
		}

		string_free(path);

		app->engine_state = 0;
		app->state = 0;
	}

	// Reach into platform and open new window(s) in accordance with app config.
	u32 window_count = darray_length(app->app_config.windows);
	if (window_count > 1) {
		KFATAL("Multiple windows are not yet implemented at the engine level. Please just stick to one for now.");
		return false;
	}

	// Create windows, but don't tell renderer about them just yet.
	engine_state->windows = darray_create(kwindow);
	for (u32 i = 0; i < window_count; ++i) {
		kwindow_config *window_config = &app->app_config.windows[i];
		kwindow new_window = {0};
		new_window.name = string_duplicate(window_config->name);
		// Add to tracked window list
		darray_push(engine_state->windows, &new_window);

		kwindow *window = &engine_state->windows[(darray_length(engine_state->windows) - 1)];
		if (app->app_config.headless) {
			// A virtual window: it has a size for render targets and viewports, but no OS window behind it.
			window->title = string_duplicate(window_config->title);
			window->width = window_config->width;
			window->height = window_config->height;
			window->device_pixel_ratio = 1.0f;
			KINFO("Created headless virtual window '%s' (%ux%u).", window->name, window->width, window->height);
		} else if (!platform_window_create(window_config, window, true)) {
			KERROR("Failed to create window '%s'.", window_config->name);
			return false;
		}
	}

	// Take the startup time so far and write it out.
	kclock_update(&engine_state->startup_clock);
	KINFO("Time to window open is %f seconds.", engine_state->startup_clock.elapsed);

	// Virtual File System
	{
		// TODO: Get the generic config from application config first.
		/* application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&game_inst->app_config, "virtual_file_system", &generic_sys_config)) {
			KERROR("No configuration exists in app config for the virtual file system. This configuration is required.");
			return false;
		} */

		// TODO: deserialize from app config.
		vfs_config vfs_sys_config = {0};
		vfs_sys_config.text_user_types = 0;
		// Take a copy of the asset manifest path.
		vfs_sys_config.manifest_file_path = string_duplicate(app->app_config.manifest_file_path);

		vfs_initialize(&systems->vfs_system_memory_requirement, 0, 0);
		systems->vfs_system_state = kallocate(systems->vfs_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!vfs_initialize(&systems->vfs_system_memory_requirement, systems->vfs_system_state, &vfs_sys_config)) {
			KERROR("Failed to initialize VFS. See logs for details.");
			return false;
		}
		string_free(vfs_sys_config.manifest_file_path);
	}

	// Asset system - must always come after the VFS since it relies on it.
	{
		// Get the generic config from application config first.
		application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&app->app_config, "asset", &generic_sys_config)) {
			KERROR("No configuration exists in app config for the asset system. This configuration is required.");
			return false;
		}

		// Deserialize from app config.
		asset_system_config asset_sys_config = {0};
		if (!asset_system_deserialize_config(generic_sys_config.configuration_str, &asset_sys_config)) {
			KERROR("Failed to deserialize asset system config, which is required.");
			return false;
		}
		asset_sys_config.default_package_name = app->app_config.default_package_name;

		asset_system_initialize(&systems->asset_system_memory_requirement, 0, 0);
		systems->asset_state = kallocate(systems->asset_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!asset_system_initialize(&systems->asset_system_memory_requirement, systems->asset_state, &asset_sys_config)) {
			KERROR("Failed to initialize Asset System. See logs for details.");
			return false;
		}
	}

	// Plugin system
	{
		// Get the generic config from application config first.
		application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&app->app_config, "plugin_system", &generic_sys_config)) {
			KERROR("No configuration exists in app config for the plugin system. This configuration is required.");
			return false;
		}

		// Parse plugin system config from app config.
		plugin_system_config plugin_sys_config = {0};
		if (!plugin_system_deserialize_config(generic_sys_config.configuration_str, &plugin_sys_config)) {
			KERROR("Failed to deserialize plugin system config, which is required.");
			return false;
		}

		plugin_system_intialize(&systems->plugin_system_memory_requirement, 0, &plugin_sys_config);
		systems->plugin_system = kallocate(systems->plugin_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!plugin_system_intialize(&systems->plugin_system_memory_requirement, systems->plugin_system, &plugin_sys_config)) {
			KERROR("Failed to initialize plugin system.");
			return false;
		}
		plugin_system_destroy_config(&plugin_sys_config);
	}

	// KVar system
	{
		kvar_system_initialize(&systems->kvar_system_memory_requirement, 0, 0);
		systems->kvar_system = kallocate(systems->kvar_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kvar_system_initialize(&systems->kvar_system_memory_requirement, systems->kvar_system, 0)) {
			KERROR("Failed to initialize KVar system.");
			return false;
		}
	}

	// Input system.
	{
		input_system_initialize(&systems->input_system_memory_requirement, KNULL, KNULL, KNULL);
		systems->input_system = kallocate(systems->input_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!input_system_initialize(&systems->input_system_memory_requirement, systems->input_system, KNULL, app)) {
			KERROR("Failed to initialize input system.");
			return false;
		}

		// Register input hooks with platform (i.e. handle_key/handle_button, etc.).
		platform_register_process_key(engine_on_process_key);
		platform_register_process_mouse_button_callback(engine_on_process_mouse_button);
		platform_register_process_mouse_move_callback(engine_on_process_mouse_move);
		platform_register_process_mouse_wheel_callback(engine_on_process_mouse_wheel);
	}

	// Clipboard
	{
		platform_register_clipboard_paste_callback(engine_on_paste);
	}

	// Renderer system
	{
		// Get the generic config from application config first.
		application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&app->app_config, "renderer", &generic_sys_config)) {
			KERROR("No configuration exists in app config for the renderer system. This configuration is required.");
			return false;
		}

		// Parse plugin system config from app config.
		renderer_system_config renderer_sys_config = {0};
		if (!renderer_system_deserialize_config(generic_sys_config.configuration_str, &renderer_sys_config)) {
			KERROR("Failed to deserialize renderer system config, which is required.");
			return false;
		}
		renderer_sys_config.max_texture_count = 16384;

		renderer_system_initialize(&systems->renderer_system_memory_requirement, 0, &renderer_sys_config);
		systems->renderer_system = kallocate(systems->renderer_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!renderer_system_initialize(&systems->renderer_system_memory_requirement, systems->renderer_system, &renderer_sys_config)) {
			KERROR("Failed to initialize renderer system.");
			return false;
		}
		renderer_system_destroy_config(&renderer_sys_config);
	}

	// Job system
	{
		b8 renderer_multithreaded = renderer_is_multithreaded();

		// This is really a core count. Subtract 1 to account for the main thread already being in use.
		i32 thread_count = platform_get_processor_count() - 1;
		if (thread_count < 1) {
			KFATAL("Error: Platform reported processor count (minus one for main thread) as %i. Need at least one additional thread for the job system.", thread_count);
			return false;
		} else {
			KDEBUG("Available threads: %i", thread_count);
		}

		// Cap the thread count.
		const i32 max_thread_count = 15;
		if (thread_count > max_thread_count) {
			KDEBUG("Available threads on the system is %i, but will be capped at %i.", thread_count, max_thread_count);
			thread_count = max_thread_count;
		}

		// Initialize the job system.
		// Requires knowledge of renderer multithread support, so should be initialized here.
		u32 job_thread_types[15];
		for (u32 i = 0; i < 15; ++i) {
			job_thread_types[i] = JOB_TYPE_GENERAL;
		}

		if (max_thread_count == 1 || !renderer_multithreaded) {
			// Everything on one job thread.
			job_thread_types[0] |= (JOB_TYPE_GPU_RESOURCE | JOB_TYPE_RESOURCE_LOAD);
		} else if (max_thread_count == 2) {
			// Split things between the 2 threads
			job_thread_types[0] |= JOB_TYPE_GPU_RESOURCE;
			job_thread_types[1] |= JOB_TYPE_RESOURCE_LOAD;
		} else {
			// Dedicate the first 2 threads to these things, pass off general tasks to other threads.
			job_thread_types[0] = JOB_TYPE_GPU_RESOURCE;
			job_thread_types[1] = JOB_TYPE_RESOURCE_LOAD;
		}

		job_system_config job_sys_config = {0};
		job_sys_config.max_job_thread_count = thread_count;
		job_sys_config.type_masks = job_thread_types;
		job_system_initialize(&systems->job_system_memory_requirement, 0, &job_sys_config);
		systems->job_system = kallocate(systems->job_system_memory_requirement, MEMORY_TAG_ENGINE);

		if (!job_system_initialize(&systems->job_system_memory_requirement, systems->job_system, &job_sys_config)) {
			KERROR("Failed to initialize job system.");
			return false;
		}
	}

	// Audio system
	{

		// Get the generic config from application config first.
		application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&app->app_config, "audio", &generic_sys_config)) {
			// TODO: Maybe audio shouldn't be required?
			KERROR("No configuration exists in app config for the audio system. This configuration is required.");
			return false;
		}

		kaudio_system_initialize(&systems->kaudio_system_memory_requirement, 0, generic_sys_config.configuration_str);
		systems->audio_system = kallocate(systems->kaudio_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kaudio_system_initialize(&systems->kaudio_system_memory_requirement, systems->audio_system, generic_sys_config.configuration_str)) {
			KERROR("Failed to initialize audio system.");
			return false;
		}
	}

	// Physics system. Optional: only started when the app config has a "physics" entry.
	{
		application_system_config generic_sys_config = {0};
		if (application_config_system_config_get(&app->app_config, "physics", &generic_sys_config)) {
			kphysics_system_initialize(&systems->physics_system_memory_requirement, 0, generic_sys_config.configuration_str);
			systems->physics_system = kallocate(systems->physics_system_memory_requirement, MEMORY_TAG_ENGINE);
			if (!kphysics_system_initialize(&systems->physics_system_memory_requirement, systems->physics_system, generic_sys_config.configuration_str)) {
				KERROR("Failed to initialize physics system.");
				return false;
			}
		}
	}

	// matrix
	{
		kmatrix_system_config matrix_system_config = {.max_matrix_count = 16384};
		kmatrix_system_initialize(&systems->kmatrix_system_memory_requirement, KNULL, &matrix_system_config);
		systems->matrix_system = kallocate(systems->kmatrix_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kmatrix_system_initialize(&systems->kmatrix_system_memory_requirement, systems->matrix_system, &matrix_system_config)) {
			KERROR("Failed to intialize kmatrix system.");
			return false;
		}
	}

	// ktransform
	{
		ktransform_system_config ktransform_sys_config = {0};
		ktransform_sys_config.initial_slot_count = 512;
		ktransform_system_initialize(&systems->ktransform_system_memory_requirement, 0, &ktransform_sys_config);
		systems->ktransform_system = kallocate(systems->ktransform_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!ktransform_system_initialize(&systems->ktransform_system_memory_requirement, systems->ktransform_system, &ktransform_sys_config)) {
			KERROR("Failed to intialize ktransform system.");
			return false;
		}
	}

	// Timeline
	{
		timeline_system_config timeline_config = {0};
		timeline_config.dummy = 1;
		ktimeline_system_initialize(&systems->timeline_system_memory_requirement, 0, 0);
		systems->timeline_system = kallocate(systems->timeline_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!ktimeline_system_initialize(&systems->timeline_system_memory_requirement, systems->timeline_system, &timeline_config)) {
			KERROR("Failed to initialize timeline system.");
			return false;
		}
	}

	// Shader system
	{
		kshader_system_config shader_sys_config;
		shader_sys_config.max_shader_count = 1024;
		shader_sys_config.max_uniform_count = 128;
		kshader_system_initialize(&systems->shader_system_memory_requirement, 0, &shader_sys_config);
		systems->shader_system = kallocate(systems->shader_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kshader_system_initialize(&systems->shader_system_memory_requirement, systems->shader_system, &shader_sys_config)) {
			KERROR("Failed to initialize shader system.");
			return false;
		}
	}

	// Texture system
	{
		texture_system_config texture_sys_config;
		texture_sys_config.max_texture_count = 4096;
		texture_system_initialize(&systems->texture_system_memory_requirement, 0, &texture_sys_config);
		systems->texture_system = kallocate(systems->texture_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!texture_system_initialize(&systems->texture_system_memory_requirement, systems->texture_system, &texture_sys_config)) {
			KERROR("Failed to initialize texture system.");
			return false;
		}
	}

	// Notify renderer of window(s)/setup surface(s), etc.
	// NOTE: This must happen after the texture system is initialized since the window "owns" it's render target textures.
	for (u32 i = 0; i < window_count; ++i) {
		kwindow_config *window_config = &app->app_config.windows[i];

		kwindow *window = &engine_state->windows[(darray_length(engine_state->windows) - 1)];

		// Tell the renderer about the window.
		if (!renderer_on_window_created(engine_state->systems.renderer_system, window)) {
			KERROR("The renderer failed to create resources for the window '%s.", window_config->name);
			return false;
		}

		// Manually call to make sure window is of the right size/viewports and such are the right size.
		renderer_on_window_resized(engine_state->systems.renderer_system, window);
	}

	// Light system
	{
		light_system_initialize(&systems->light_system_memory_requirement, 0, 0);
		systems->light_system = kallocate(systems->light_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!light_system_initialize(&systems->light_system_memory_requirement, systems->light_system, 0)) {
			KERROR("Failed to initialize light system.");
			return false;
		}
	}

	// Model system
	{
		kmodel_system_config model_sys_config = {
			.default_application_package_name = app->app_config.default_package_name,
			// FIXME: Read from app config.
			.max_instance_count = 128};

		kmodel_system_initialize(&systems->model_system_memory_requirement, 0, &model_sys_config);
		systems->model_system = kallocate(systems->model_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kmodel_system_initialize(&systems->model_system_memory_requirement, systems->model_system, &model_sys_config)) {
			KERROR("Failed to initialize model system.");
			return false;
		}
	}

	// Material system and renderer.
	{
		kmaterial_system_config material_sys_config = {0};
		// FIXME: Should be configurable.
		material_sys_config.max_material_count = 256;
		material_sys_config.max_instance_count = 1024;
		kmaterial_system_initialize(&systems->material_system_memory_requirement, 0, &material_sys_config);
		systems->material_system = kallocate(systems->material_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kmaterial_system_initialize(&systems->material_system_memory_requirement, systems->material_system, &material_sys_config)) {
			KERROR("Failed to initialize material system.");
			return false;
		}

		systems->material_renderer = kallocate(sizeof(kmaterial_renderer), MEMORY_TAG_ENGINE);
		KASSERT_MSG(
			kmaterial_renderer_initialize(
				systems->material_renderer,
				material_sys_config.max_material_count,
				material_sys_config.max_instance_count),
			"Failed to initialize material renderer.");

		// Setup default materials in material system. Must be done after the renderer is initialized
		// since it handles all GPU resources.
		KASSERT_MSG(kmaterial_system_setup_defaults(systems->material_system), "Failed to setup material system defaults.");
	}

	// Font system
	{
		// Get the generic config from application config first.
		application_system_config generic_sys_config = {0};
		if (!application_config_system_config_get(&app->app_config, "font", &generic_sys_config)) {
			KERROR("No configuration exists in app config for the font system. This configuration is required.");
			return false;
		}

		font_system_config font_sys_config = {0};

		// Parse system config from app config.
		if (!font_system_deserialize_config(generic_sys_config.configuration_str, &font_sys_config)) {
			KERROR("Failed to deserialize font system config, which is required.");
			return false;
		}

		font_system_initialize(&systems->font_system_memory_requirement, 0, &font_sys_config);
		systems->font_system = kallocate(systems->font_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!font_system_initialize(&systems->font_system_memory_requirement, systems->font_system, &font_sys_config)) {
			KERROR("Failed to initialize font system.");
			return false;
		}
	}

	// Camera system
	{
		kcamera_system_config camera_sys_config = {0};
		camera_sys_config.max_camera_count = 61;
		kcamera_system_initialize(&systems->camera_system_memory_requirement, 0, &camera_sys_config);
		systems->camera_system = kallocate(systems->camera_system_memory_requirement, MEMORY_TAG_ENGINE);
		if (!kcamera_system_initialize(&systems->camera_system_memory_requirement, systems->camera_system, &camera_sys_config)) {
			KERROR("Failed to initialize camera system.");
			return false;
		}
	}

	// NOTE: Boot sequence =======================================================================================================
	// Perform the application's boot sequence.
	app->stage = APPLICATION_STAGE_BOOTING;
	if (!app->boot(app)) {
		KFATAL("Game boot sequence failed; aborting application.");
		return false;
	}

	// NOTE: End boot application sequence.

	// Post-boot plugin init
	if (!plugin_system_initialize_plugins(engine_state->systems.plugin_system)) {
		KERROR("Plugin(s) failed initialization. See logs for details.");
		return false;
	}

	// Setup the frame allocator.
	linear_allocator_create(MEBIBYTES(app->app_config.frame_allocator_size), 0, &engine_state->frame_allocator);
	engine_state->p_frame_data.allocator.allocate = frame_allocator_allocate;
	engine_state->p_frame_data.allocator.free = frame_allocator_free;
	engine_state->p_frame_data.allocator.free_all = frame_allocator_free_all;
	engine_state->p_frame_data.allocator.total_space = frame_allocator_total_space;
	engine_state->p_frame_data.allocator.allocated = frame_allocator_allocated;

	// Allocate for the application's frame data.
	if (app->app_config.app_frame_data_size > 0) {
		engine_state->p_frame_data.app_frame_data = kallocate(app->app_config.app_frame_data_size, MEMORY_TAG_GAME);
	} else {
		engine_state->p_frame_data.app_frame_data = 0;
	}

	app->stage = APPLICATION_STAGE_BOOT_COMPLETE;

	// Initialize the game.
	app->stage = APPLICATION_STAGE_INITIALIZING;
	if (!app->initialize(app)) {
		KFATAL("Game failed to initialize.");
		return false;
	}
	app->stage = APPLICATION_STAGE_INITIALIZED;

	return true;
}

typedef enum engine_render_result {
	// The frame was rendered and presented.
	ENGINE_RENDER_RESULT_DONE,
	// Rendering was skipped this frame (renderer not ready, window resizing, swapchain recreated).
	ENGINE_RENDER_RESULT_SKIPPED,
	// An unrecoverable error occurred; the engine must shut down.
	ENGINE_RENDER_RESULT_FATAL
} engine_render_result;

// Runs one fixed simulation step: systems that must advance at a fixed rate, then the application.
static b8 engine_simulation_step (application *app, f64 step_seconds) {
	kmodel_system_update(engine_state->systems.model_system, (f32)step_seconds, &engine_state->p_frame_data);

	if (app->fixed_update && !app->fixed_update(app, step_seconds, &engine_state->p_frame_data)) {
		KFATAL("Game fixed update failed, shutting down.");
		return false;
	}

	// Physics steps after the application, so forces and velocities set this step take effect now.
	if (engine_state->systems.physics_system && !kphysics_system_step(engine_state->systems.physics_system, (f32)step_seconds)) {
		KFATAL("Physics step failed, shutting down.");
		return false;
	}

	ksim_clock_step_complete(&engine_state->sim_clock);
	return true;
}

// Per-frame application update and rendering. Skipping this (for example while the window is
// resizing) no longer skips simulation, which runs before it.
static engine_render_result engine_frame_update_and_render (application *app, kwindow *w) {
	frame_data *p_frame_data = &engine_state->p_frame_data;
	struct renderer_system_state *renderer = engine_state->systems.renderer_system;

	if (!renderer_frame_prepare(renderer, p_frame_data)) {
		return ENGINE_RENDER_RESULT_SKIPPED;
	}

	// Make sure the window is not currently being resized by waiting a designated
	// number of frames after the last resize operation before performing the backend updates.
	if (w->resizing) {
		w->frames_since_resize++;

		// If the required number of frames have passed since the resize, go ahead and perform the actual updates.
		// FIXME: Configurable delay here instead of magic 30 frames.
		if (w->frames_since_resize >= 3) {
			renderer_on_window_resized(renderer, w);

			// NOTE: Don't bother checking the result of this, since this will likely
			// recreate the swapchain and boot to the next frame anyway.
			renderer_frame_prepare_window_surface(renderer, w, p_frame_data);

			// Notify the application of the resize.
			app->on_window_resize(app, w);

			w->frames_since_resize = 0;
			w->resizing = false;
		} else {
			// Skip rendering the frame and try again next time.
			// NOTE: Simulate a frame being "drawn" at 60 FPS.
			platform_sleep(16);
		}

		// Either way, don't render this frame while resizing. Try again next frame.
		return ENGINE_RENDER_RESULT_SKIPPED;
	}
	if (!renderer_frame_prepare_window_surface(renderer, w, p_frame_data)) {
		// This can also happen not just from a resize above, but also if a renderer flag
		// (such as VSync) changed, which may also require resource recreation. To handle this,
		// Notify the application of a resize event, which it can then pass on to its rendergraph(s)
		// as needed.
		app->on_window_resize(app, w);
		return ENGINE_RENDER_RESULT_SKIPPED;
	}

	if (!app->update(app, p_frame_data)) {
		KFATAL("Game update failed, shutting down.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	// Update the transform system _after_ the application so we are sure all transform updates that
	// need to occur have happened.
	ktransform_system_update(engine_state->systems.ktransform_system, p_frame_data);
	kmatrix_system_update(engine_state->systems.matrix_system, p_frame_data);
	light_system_frame_prepare(engine_state->systems.light_system, p_frame_data);
	kmodel_system_frame_prepare(engine_state->systems.model_system, p_frame_data);

	// Start recording to the command list.
	if (!renderer_frame_command_list_begin(renderer, p_frame_data)) {
		KFATAL("Failed to begin renderer command list. Shutting down.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	// Begin "prepare_frame" render event grouping.
	renderer_begin_debug_label("prepare_frame", KCOLOUR4_YELLOW);

	// TODO: frame prepare for systems that need it.
	// NOTE: Frame preparation for plugins
	plugin_system_frame_prepare_plugins(engine_state->systems.plugin_system, p_frame_data);

	// Have the application generate the render packet.
	b8 prepare_result = app->prepare_frame(app, p_frame_data);
	// End "prepare_frame" render event grouping.
	renderer_end_debug_label();

	if (!prepare_result) {
		return ENGINE_RENDER_RESULT_SKIPPED;
	}

	// Call the game's render routine.
	if (!app->render_frame(app, p_frame_data)) {
		KFATAL("Game render failed, shutting down.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	// End the recording to the command list.
	if (!renderer_frame_command_list_end(renderer, p_frame_data)) {
		KFATAL("Failed to end renderer command list. Shutting down.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	if (!renderer_frame_submit(renderer, p_frame_data)) {
		KFATAL("Failed to submit work to the renderer for frame rendering.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	// Present the frame.
	if (!renderer_frame_present(renderer, w, p_frame_data)) {
		KERROR("The call to renderer_present failed. This is likely unrecoverable. Shutting down.");
		return ENGINE_RENDER_RESULT_FATAL;
	}

	return ENGINE_RENDER_RESULT_DONE;
}

b8 engine_run (application *app) {
	app->stage = APPLICATION_STAGE_RUNNING;
	engine_state->is_running = true;

	// Take the startup time and write it out.
	kclock_update(&engine_state->startup_clock);
	KINFO("Time to first update is %f seconds.", engine_state->startup_clock.elapsed);
	kclock_stop(&engine_state->startup_clock);

	kclock_start(&engine_state->clock);
	kclock_update(&engine_state->clock);
	engine_state->last_time = engine_state->clock.elapsed;
	f64 frame_elapsed_time = 0;

	ksim_clock *sim = &engine_state->sim_clock;
	if (sim->mode == KSIM_CLOCK_MODE_VARIABLE) {
		KINFO("Simulation uses a variable time step (set fixed_step_hz to enable fixed steps).");
	} else {
		KINFO("Simulation uses fixed steps of %.6f s (%u Hz), %s mode.", sim->step_seconds, app->app_config.fixed_step_hz, ksim_clock_mode_name(sim->mode));
		if (!app->fixed_update) {
			KINFO("The application has no application_fixed_update; only engine systems advance in fixed steps.");
		}
	}

	u64 max_frames = app->app_config.max_frames;
	u64 frames_run = 0;
	if (agent_channel_enabled()) {
		if (!agent_channel_start()) {
			return false;
		}
		if (max_frames) {
			// In agent mode the agent decides when to stop.
			KWARN("max_frames is ignored in agent mode.");
			max_frames = 0;
		}
	}
	if (max_frames) {
		KINFO("Engine will stop after %llu frames.", max_frames);
	}

	char *mem_usage = get_memory_usage_str();
	KINFO(mem_usage);
	string_free(mem_usage);

	// FIXME: Need a better way to select the active window.
	kwindow *w = &engine_state->windows[0];

	// FIXME: The event loop in the platform layer depends on active window.
	// In theory this means there should be one of these loops per window.
	while (engine_state->is_running) {
		if (!platform_pump_messages()) {
			engine_state->is_running = false;
		}

		// Agent requests act like OS input: they run before the simulation steps of this frame.
		agent_channel_update();

		if (engine_state->is_suspended) {
			KDEBUG("suspended...");
			continue;
		}

		// Wall-clock time since the last frame. Lockstep mode never uses it, so headless runs stay repeatable.
		kclock_update(&engine_state->clock);
		f64 current_time = engine_state->clock.elapsed;
		f64 wall_delta = current_time - engine_state->last_time;
		engine_state->last_time = current_time;
		f64 frame_start_time = platform_get_absolute_time();

		// Reset the frame allocator
		engine_state->p_frame_data.allocator.free_all();

		// TODO: Update systems here that need them.
		job_system_update(engine_state->systems.job_system, &engine_state->p_frame_data);
		plugin_system_update_plugins(engine_state->systems.plugin_system, &engine_state->p_frame_data);
		kaudio_system_update(engine_state->systems.audio_system, &engine_state->p_frame_data);

		// Simulation. Runs before, and independently of, rendering.
		u32 steps = ksim_clock_frame_begin(sim, wall_delta);
		for (u32 i = 0; i < steps && engine_state->is_running; ++i) {
			if (!engine_simulation_step(app, sim->step_seconds)) {
				engine_state->is_running = false;
				engine_state->failed = true;
			}
		}
		if (!engine_state->is_running) {
			break;
		}

		// Time that per-frame (non-simulation) code sees through the timelines. In lockstep mode it
		// is simulated time, so frame-based code such as cameras is repeatable too.
		f64 frame_delta;
		if (sim->mode == KSIM_CLOCK_MODE_LOCKSTEP) {
			frame_delta = (f64)steps * sim->step_seconds;
		} else {
			frame_delta = wall_delta;
		}
		if (sim->mode == KSIM_CLOCK_MODE_VARIABLE) {
			// Legacy path: simulation systems advance once per frame by the frame time.
			kmodel_system_update(engine_state->systems.model_system, (f32)frame_delta, &engine_state->p_frame_data);
			if (engine_state->systems.physics_system && frame_delta > 0.0) {
				kphysics_system_step(engine_state->systems.physics_system, (f32)frame_delta);
			}
			ksim_clock_variable_advance(sim, frame_delta);
		}

		// Update timelines. Note that this is not done by the systems manager
		// because we don't want or have timeline data in the frame_data struct any longer.
		ktimeline_system_update(engine_state->systems.timeline_system, (f32)frame_delta);

		engine_state->p_frame_data.sim_steps_this_frame = steps;
		engine_state->p_frame_data.sim_alpha = ksim_clock_alpha(sim);

		// update metrics
		metrics_update(frame_elapsed_time);

		engine_render_result render_result = engine_frame_update_and_render(app, w);
		if (render_result == ENGINE_RENDER_RESULT_FATAL) {
			engine_state->is_running = false;
			engine_state->failed = true;
			break;
		}

		f64 frame_end_time = platform_get_absolute_time();
		frame_elapsed_time = frame_end_time - frame_start_time;

		// A paused headless run has nothing to do until a step is requested; don't spin a core.
		if (sim->mode == KSIM_CLOCK_MODE_LOCKSTEP && steps == 0) {
			platform_sleep(1);
		}

		// NOTE: Input update/state copying should always be handled
		// after any input should be recorded; I.E. before this line.
		// As a safety, input is the last thing to be updated before
		// this frame ends. This also runs on frames that skipped rendering,
		// so key presses are not seen twice.
		input_update(&engine_state->p_frame_data);

		frames_run++;
		if (max_frames && frames_run >= max_frames) {
			KINFO("Reached max_frames (%llu); shutting down.", max_frames);
			engine_state->is_running = false;
		}
	}

	kclock_update(&engine_state->clock);
	KINFO("Ran %llu frames and %llu simulation steps: %.3f s simulated in %.3f s wall-clock.",
		  frames_run, sim->step_count, sim->sim_seconds, engine_state->clock.elapsed);
	if (sim->dropped_seconds > 0.0) {
		KWARN("The simulation fell behind real time by %.3f s because some frames took longer than max_steps_per_frame steps.", sim->dropped_seconds);
	}

	// Read now: engine_state is freed with the memory system below.
	b8 failed = engine_state->failed;
	if (failed) {
		KERROR("The engine stopped because of an error; see the log above.");
	}

	engine_state->is_running = false;
	app->stage = APPLICATION_STAGE_SHUTTING_DOWN;
	agent_channel_shutdown();

	// Shut down the game and unload the lib.
	app->shutdown(app);
	platform_dynamic_library_unload(&app->game_library);

	// Unregister from events.
	event_unregister(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);

	// TODO: Close/destroy any and all active windows.
	u32 window_count = darray_length(engine_state->windows);
	for (u32 i = 0; i < window_count; ++i) {
		kwindow *window = &engine_state->windows[i];

		// Tell the renderer about the window destruction.
		renderer_on_window_destroyed(engine_state->systems.renderer_system, window);

		if (app->app_config.headless) {
			string_free(window->name);
			string_free(window->title);
		} else {
			platform_window_destroy(window);
		}
	}
	darray_destroy(engine_state->windows);

	string_free(app->game_library_name);
	string_free(app->game_library_loaded_name);

	application_config_destroy(&app->app_config);

	// Shut down all systems.
	{
		// Engine systems
		engine_system_states *systems = &engine_state->systems;

		kcamera_system_shutdown(systems->camera_system);
		kmodel_system_shutdown(systems->model_system);
		kmaterial_system_shutdown(systems->material_system);
		kmaterial_renderer_shutdown(systems->material_renderer);
		light_system_shutdown(systems->light_system);
		font_system_shutdown(systems->font_system);
		texture_system_shutdown(systems->texture_system);
		ktimeline_system_shutdown(systems->timeline_system);
		kaudio_system_shutdown(systems->audio_system);
		if (systems->physics_system) {
			kphysics_system_shutdown(systems->physics_system);
		}
		// Shutdown plugins, but not the system yet in case plugins which do not
		// "auto-unload" rely on the state still existing.
		plugin_system_shutdown_all_plugins(systems->plugin_system);

		ktransform_system_shutdown(systems->ktransform_system);
		kmatrix_system_shutdown(systems->matrix_system);
		kshader_system_shutdown(systems->shader_system);
		renderer_system_shutdown(systems->renderer_system);
		job_system_shutdown(systems->job_system);
		input_system_shutdown(systems->input_system);
		event_system_shutdown(systems->event_system);
		kvar_system_shutdown(systems->kvar_system);
		vfs_shutdown(systems->vfs_system_state);
		console_shutdown(systems->console_system);
		// Now shutdown the plugin system itself.
		plugin_system_shutdown(systems->plugin_system);
		platform_system_shutdown(systems->platform_system);

		kstring_id_shutdown();
		kname_shutdown();
		kregistry_destroy(&engine_state->external_systems_registry);

		memory_system_shutdown();
	}

	app->stage = APPLICATION_STAGE_UNINITIALIZED;

	return !failed;
}

void engine_on_event_system_initialized (void) {
	// Register for engine-level events.
	event_register(EVENT_CODE_APPLICATION_QUIT, 0, engine_on_event);
}

void engine_simulation_pause_set (b8 paused) {
	ksim_clock *sim = &engine_state->sim_clock;
	if (sim->mode == KSIM_CLOCK_MODE_VARIABLE) {
		KWARN("The simulation cannot be paused with a variable time step. Set fixed_step_hz in the app config.");
		return;
	}
	ksim_clock_pause_set(sim, paused);
	KINFO("Simulation %s at step %llu.", paused ? "paused" : "resumed", sim->step_count);
}

b8 engine_simulation_paused (void) {
	return engine_state->sim_clock.paused;
}

void engine_simulation_steps_request (u32 count) {
	ksim_clock_request_steps(&engine_state->sim_clock, count);
}

u32 engine_simulation_pending_steps_get (void) {
	return engine_state->sim_clock.pending_steps;
}

u64 engine_simulation_step_count_get (void) {
	return engine_state->sim_clock.step_count;
}

f64 engine_simulation_time_get (void) {
	return engine_state->sim_clock.sim_seconds;
}

f64 engine_simulation_step_seconds_get (void) {
	return engine_state->sim_clock.step_seconds;
}

const struct frame_data *engine_frame_data_get (void) {
	return &engine_state->p_frame_data;
}

const engine_system_states *engine_systems_get (void) {
	return &engine_state->systems;
}

struct application *engine_app_state_get (void) {
	return engine_state->app;
}

khandle engine_external_system_register (u64 system_state_memory_requirement) {
	// Don't pass a block of memory here since the system should call "get state" next for it.
	// This keeps memory ownership inside the engine and its registry.
	return kregistry_add_entry(&engine_state->external_systems_registry, 0, system_state_memory_requirement, true);
}

void *engine_external_system_state_get (khandle system_handle) {
	// Acquire the system state, but without any listener/callback.
	return kregistry_entry_acquire(&engine_state->external_systems_registry, system_handle, 0, 0);
}

struct kwindow *engine_active_window_get (void) {
	// FIXME: multi-window support
	return &engine_state->windows[0];
}

static b8 engine_on_event (u16 code, void *sender, void *listener_inst, event_context context) {
	switch (code) {
	case EVENT_CODE_APPLICATION_QUIT: {
		KINFO("EVENT_CODE_APPLICATION_QUIT recieved, shutting down.\n");
		engine_state->is_running = false;
		return true;
	}
	}

	return false;
}

static void engine_on_window_closed (const struct kwindow *window) {
	if (window) {
		// TODO: handle window closes independently.
		event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
	}
}

static void engine_on_window_resized (const struct kwindow *window) {
	// Handle minimization
	if (window->width == 0 || window->height == 0) {
		KINFO("Window minimized, suspending application.");
		// FIXME: This should be per-window, not global.
		engine_state->is_suspended = true;
	} else {
		if (engine_state->is_suspended) {
			KINFO("Window restored, resuming application.");
			engine_state->is_suspended = false;
		}

		// Fire an event for anything listening for window resizes.
		event_context context = {0};
		context.data.u16[0] = window->width;
		context.data.u16[1] = window->height;
		event_fire(EVENT_CODE_WINDOW_RESIZED, (kwindow *)window, context);
	}
}

static void engine_on_process_key (keys key, b8 pressed, b8 is_repeat) {
	input_process_key(key, pressed, is_repeat);
}

static void engine_on_process_mouse_button (mouse_buttons button, b8 pressed) {
	input_process_button(button, pressed);
}

static void engine_on_process_mouse_move (i16 x, i16 y) {
	input_process_mouse_move(x, y);
}

static void engine_on_process_mouse_wheel (i8 z_delta) {
	input_process_mouse_wheel(z_delta);
}

static void engine_on_paste (kclipboard_context context) {
	KTRACE("Clipboard paste event from platform.");
	event_context evt = {
		.data.custom_data = {
			.data = &context,
			.size = sizeof(kclipboard_context)}};

	event_fire(EVENT_CODE_CLIPBOARD_PASTE, KNULL, evt);
}

static b8 engine_log_file_write (void *engine_state, log_level level, const char *message) {
	engine_state_t *engine = engine_state;
	// Append to log file
	if (engine && engine->log_file_handle.is_valid) {
		// Since the message already contains a '\n', just write the bytes directly.
		u64 length = string_length(message);
		u64 written = 0;
		if (!filesystem_write(&engine->log_file_handle, length, message, &written)) {
			platform_console_write(0, LOG_LEVEL_ERROR, "ERROR writing to console.log.");
			return false;
		}
		return true;
	}
	return false;
}

static b8 engine_platform_console_write (void *platform, log_level level, const char *message) {
	// Just pass it on to the platform layer.
	platform_console_write(platform, level, message);
	return true;
}

static b8 load_game_lib (application *app) {
	// Dynamically load game library

	if (!platform_dynamic_library_load(app->game_library_loaded_name, &app->game_library)) {
		return false;
	}
	// Get pfns
	app->boot = platform_dynamic_library_load_function("application_boot", &app->game_library);
	if (!app->boot) {
		return false;
	}
	app->initialize = platform_dynamic_library_load_function("application_initialize", &app->game_library);
	if (!app->initialize) {
		return false;
	}
	app->update = platform_dynamic_library_load_function("application_update", &app->game_library);
	if (!app->update) {
		return false;
	}
	// Optional: only needed by applications that use fixed-step simulation.
	app->fixed_update = platform_dynamic_library_load_function("application_fixed_update", &app->game_library);

	app->prepare_frame = platform_dynamic_library_load_function("application_prepare_frame", &app->game_library);
	if (!app->prepare_frame) {
		return false;
	}
	app->render_frame = platform_dynamic_library_load_function("application_render_frame", &app->game_library);
	if (!app->render_frame) {
		return false;
	}
	app->on_window_resize = platform_dynamic_library_load_function("application_on_window_resize", &app->game_library);
	if (!app->on_window_resize) {
		return false;
	}
	app->on_action = platform_dynamic_library_load_function("application_on_action", &app->game_library);
	if (!app->on_action) {
		return false;
	}
	app->shutdown = platform_dynamic_library_load_function("application_shutdown", &app->game_library);
	if (!app->shutdown) {
		return false;
	}

	app->lib_on_load = platform_dynamic_library_load_function("application_lib_on_load", &app->game_library);
	if (!app->lib_on_load) {
		return false;
	}

	app->lib_on_unload = platform_dynamic_library_load_function("application_lib_on_unload", &app->game_library);
	if (!app->lib_on_unload) {
		return false;
	}

	// Invoke the onload.
	app->lib_on_load(app);

	return true;
}

static void watched_file_updated (u32 watcher_id, const char *file_path, b8 is_binary, void *context) {
	/* b8 watched_file_updated(u16 code, void* sender, void* listener_inst, event_context context) { */
	application *app = (application *)context;
	if (watcher_id == app->game_library.watch_id) {
		KINFO("Hot-Reloading game library.");

		// Tell the app it is about to be unloaded.
		app->lib_on_unload(app);

		// Actually unload the app's lib.
		if (!platform_dynamic_library_unload(&app->game_library)) {
			KERROR("Failed to unload game library");
			return;
		}

		// Wait a bit before trying to copy the file.
		platform_sleep(100);

		const char *prefix = platform_dynamic_library_prefix();
		const char *extension = platform_dynamic_library_extension();
		char source_file[260];
		char target_file[260];
		string_format_unsafe(source_file, "%s%s%s", prefix, app->game_library_name, extension);
		string_format_unsafe(target_file, "%s%s%s", prefix, app->game_library_loaded_name, extension);

		platform_error_code err_code = PLATFORM_ERROR_FILE_LOCKED;
		while (err_code == PLATFORM_ERROR_FILE_LOCKED) {
			err_code = platform_copy_file(source_file, target_file, true);
			if (err_code == PLATFORM_ERROR_FILE_LOCKED) {
				platform_sleep(100);
			}
		}
		if (err_code != PLATFORM_ERROR_SUCCESS) {
			KERROR("File copy failed!");
			return;
		}

		if (!load_game_lib(app)) {
			KERROR("Game lib reload failed.");
			return;
		}
	}
}
