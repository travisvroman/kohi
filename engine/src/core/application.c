#include "application.h"
#include "game_types.h"

#include "version.h"

#include "platform/platform.h"
#include "core/kmemory.h"
#include "core/logger.h"
#include "core/event.h"
#include "core/input.h"
#include "core/clock.h"
#include "core/kstring.h"
#include "core/identifier.h"
#include "core/uuid.h"

#include "memory/linear_allocator.h"

#include "renderer/renderer_frontend.h"

// systems
#include "systems/texture_system.h"
#include "systems/material_system.h"
#include "systems/geometry_system.h"
#include "systems/resource_system.h"
#include "systems/shader_system.h"
#include "systems/camera_system.h"
#include "systems/render_view_system.h"
#include "systems/job_system.h"
#include "systems/font_system.h"

// TODO: temp
#include "math/kmath.h"
#include "math/transform.h"
#include "math/geometry_utils.h"
#include "containers/darray.h"
#include "resources/mesh.h"

#include "resources/ui_text.h"
// TODO: end temp

typedef struct application_state {
    game* game_inst;
    b8 is_running;
    b8 is_suspended;
    i16 width;
    i16 height;
    clock clock;
    f64 last_time;
    linear_allocator systems_allocator;

    u64 event_system_memory_requirement;
    void* event_system_state;

    u64 job_system_memory_requirement;
    void* job_system_state;

    u64 logging_system_memory_requirement;
    void* logging_system_state;

    u64 input_system_memory_requirement;
    void* input_system_state;

    u64 platform_system_memory_requirement;
    void* platform_system_state;

    u64 resource_system_memory_requirement;
    void* resource_system_state;

    u64 shader_system_memory_requirement;
    void* shader_system_state;

    u64 renderer_system_memory_requirement;
    void* renderer_system_state;

    u64 renderer_view_system_memory_requirement;
    void* renderer_view_system_state;

    u64 texture_system_memory_requirement;
    void* texture_system_state;

    u64 material_system_memory_requirement;
    void* material_system_state;

    u64 geometry_system_memory_requirement;
    void* geometry_system_state;

    u64 camera_system_memory_requirement;
    void* camera_system_state;

    u64 font_system_memory_requirement;
    void* font_system_state;

    // TODO: temp
    skybox sb;

    mesh meshes[10];
    mesh* car_mesh;
    mesh* sponza_mesh;
    b8 models_loaded;

    mesh ui_meshes[10];
    ui_text test_text;
    ui_text test_sys_text;

    // The unique identifier of the currently hovered-over object.
    u32 hovered_object_id;
    // TODO: end temp

} application_state;

static application_state* app_state;

// Event handlers
b8 application_on_event(u16 code, void* sender, void* listener_inst, event_context context);
b8 application_on_key(u16 code, void* sender, void* listener_inst, event_context context);
b8 application_on_resized(u16 code, void* sender, void* listener_inst, event_context context);

// TODO: temp
b8 event_on_debug_event(u16 code, void* sender, void* listener_inst, event_context data) {
    if (code == EVENT_CODE_DEBUG0) {
        const char* names[3] = {
            "cobblestone",
            "paving",
            "paving2"};
        static i8 choice = 2;

        // Save off the old names.
        const char* old_name = names[choice];

        choice++;
        choice %= 3;

        // Just swap out the material on the first mesh if it exists.
        geometry* g = app_state->meshes[0].geometries[0];
        if (g) {
            // Acquire the new material.
            g->material = material_system_acquire(names[choice]);
            if (!g->material) {
                KWARN("event_on_debug_event no material found! Using default material.");
                g->material = material_system_get_default();
            }

            // Release the old diffuse material.
            material_system_release(old_name);
        }
        return true;
    } else if (code == EVENT_CODE_DEBUG1) {
        if (!app_state->models_loaded) {
            KDEBUG("Loading models...");
            app_state->models_loaded = true;
            if (!mesh_load_from_resource("falcon", app_state->car_mesh)) {
                KERROR("Failed to load falcon mesh!");
            }
            if (!mesh_load_from_resource("sponza", app_state->sponza_mesh)) {
                KERROR("Failed to load falcon mesh!");
            }
        }
        return true;
    }

    return false;
}
// TODO: end temp

b8 application_create(game* game_inst) {
    if (game_inst->application_state) {
        KERROR("application_create called more than once.");
        return false;
    }

    // Report engine version
    KINFO("Kohi Engine v. %s", KVERSION);

    // Memory system must be the first thing to be stood up.
    memory_system_configuration memory_system_config = {};
    memory_system_config.total_alloc_size = GIBIBYTES(1);
    if (!memory_system_initialize(memory_system_config)) {
        KERROR("Failed to initialize memory system; shutting down.");
        return false;
    }

    // Seed the uuid generator.
    // TODO: A better seed here.
    uuid_seed(101);

    // Allocate the game state.
    game_inst->state = kallocate(game_inst->state_memory_requirement, MEMORY_TAG_GAME);

    // Stand up the application state.
    game_inst->application_state = kallocate(sizeof(application_state), MEMORY_TAG_APPLICATION);
    app_state = game_inst->application_state;
    app_state->game_inst = game_inst;
    app_state->is_running = false;
    app_state->is_suspended = false;

    // TODO: temp debug
    app_state->models_loaded = false;
    // TODO: end temp debug

    // Create a linear allocator for all systems (except memory) to use.
    u64 systems_allocator_total_size = 64 * 1024 * 1024;  // 64 mb
    linear_allocator_create(systems_allocator_total_size, 0, &app_state->systems_allocator);

    // Initialize other subsystems.

    // Events
    event_system_initialize(&app_state->event_system_memory_requirement, 0);
    app_state->event_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->event_system_memory_requirement);
    event_system_initialize(&app_state->event_system_memory_requirement, app_state->event_system_state);

    // Logging
    initialize_logging(&app_state->logging_system_memory_requirement, 0);
    app_state->logging_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->logging_system_memory_requirement);
    if (!initialize_logging(&app_state->logging_system_memory_requirement, app_state->logging_system_state)) {
        KERROR("Failed to initialize logging system; shutting down.");
        return false;
    }

    // Input
    input_system_initialize(&app_state->input_system_memory_requirement, 0);
    app_state->input_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->input_system_memory_requirement);
    input_system_initialize(&app_state->input_system_memory_requirement, app_state->input_system_state);

    // Register for engine-level events.
    event_register(EVENT_CODE_APPLICATION_QUIT, 0, application_on_event);
    event_register(EVENT_CODE_KEY_PRESSED, 0, application_on_key);
    event_register(EVENT_CODE_KEY_RELEASED, 0, application_on_key);
    event_register(EVENT_CODE_RESIZED, 0, application_on_resized);
    event_register(EVENT_CODE_OBJECT_HOVER_ID_CHANGED, 0, application_on_event);
    // TODO: temp
    event_register(EVENT_CODE_DEBUG0, 0, event_on_debug_event);
    event_register(EVENT_CODE_DEBUG1, 0, event_on_debug_event);
    // TODO: end temp

    // Platform
    platform_system_startup(&app_state->platform_system_memory_requirement, 0, 0, 0, 0, 0, 0);
    app_state->platform_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->platform_system_memory_requirement);
    if (!platform_system_startup(
            &app_state->platform_system_memory_requirement,
            app_state->platform_system_state,
            game_inst->app_config.name,
            game_inst->app_config.start_pos_x,
            game_inst->app_config.start_pos_y,
            game_inst->app_config.start_width,
            game_inst->app_config.start_height)) {
        return false;
    }

    // Resource system.
    resource_system_config resource_sys_config;
    resource_sys_config.asset_base_path = "../assets";
    resource_sys_config.max_loader_count = 32;
    resource_system_initialize(&app_state->resource_system_memory_requirement, 0, resource_sys_config);
    app_state->resource_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->resource_system_memory_requirement);
    if (!resource_system_initialize(&app_state->resource_system_memory_requirement, app_state->resource_system_state, resource_sys_config)) {
        KFATAL("Failed to initialize resource system. Aborting application.");
        return false;
    }

    // Shader system
    shader_system_config shader_sys_config;
    shader_sys_config.max_shader_count = 1024;
    shader_sys_config.max_uniform_count = 128;
    shader_sys_config.max_global_textures = 31;
    shader_sys_config.max_instance_textures = 31;
    shader_system_initialize(&app_state->shader_system_memory_requirement, 0, shader_sys_config);
    app_state->shader_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->shader_system_memory_requirement);
    if (!shader_system_initialize(&app_state->shader_system_memory_requirement, app_state->shader_system_state, shader_sys_config)) {
        KFATAL("Failed to initialize shader system. Aborting application.");
        return false;
    }

    // Renderer system
    renderer_system_initialize(&app_state->renderer_system_memory_requirement, 0, 0);
    app_state->renderer_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->renderer_system_memory_requirement);
    if (!renderer_system_initialize(&app_state->renderer_system_memory_requirement, app_state->renderer_system_state, game_inst->app_config.name)) {
        KFATAL("Failed to initialize renderer. Aborting application.");
        return false;
    }

    b8 renderer_multithreaded = renderer_is_multithreaded();

    // This is really a core count. Subtract 1 to account for the main thread already being in use.
    i32 thread_count = platform_get_processor_count() - 1;
    if (thread_count < 1) {
        KFATAL("Error: Platform reported processor count (minus one for main thread) as %i. Need at least one additional thread for the job system.", thread_count);
        return false;
    } else {
        KTRACE("Available threads: %i", thread_count);
    }

    // Cap the thread count.
    const i32 max_thread_count = 15;
    if (thread_count > max_thread_count) {
        KTRACE("Available threads on the system is %i, but will be capped at %i.", thread_count, max_thread_count);
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

    job_system_initialize(&app_state->job_system_memory_requirement, 0, 0, 0);
    app_state->job_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->job_system_memory_requirement);
    if (!job_system_initialize(&app_state->job_system_memory_requirement, app_state->job_system_state, thread_count, job_thread_types)) {
        KFATAL("Failed to initialize job system. Aborting application.");
        return false;
    }

    // Texture system.
    texture_system_config texture_sys_config;
    texture_sys_config.max_texture_count = 65536;
    texture_system_initialize(&app_state->texture_system_memory_requirement, 0, texture_sys_config);
    app_state->texture_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->texture_system_memory_requirement);
    if (!texture_system_initialize(&app_state->texture_system_memory_requirement, app_state->texture_system_state, texture_sys_config)) {
        KFATAL("Failed to initialize texture system. Application cannot continue.");
        return false;
    }

    // Font system.
    font_system_config font_sys_config;
    font_sys_config.auto_release = false;
    font_sys_config.default_bitmap_font_count = 1;

    bitmap_font_config bmp_font_config = {};
    // UbuntuMono21px NotoSans21px
    bmp_font_config.name = "Ubuntu Mono 21px";
    bmp_font_config.resource_name = "UbuntuMono21px";
    bmp_font_config.size = 21;
    font_sys_config.bitmap_font_configs = &bmp_font_config;

    system_font_config sys_font_config;
    sys_font_config.default_size = 20;
    sys_font_config.name = "Noto Sans";
    sys_font_config.resource_name = "NotoSansCJK";

    font_sys_config.default_system_font_count = 1;
    font_sys_config.system_font_configs = &sys_font_config;

    font_sys_config.max_bitmap_font_count = 101;
    font_sys_config.max_system_font_count = 101;
    font_system_initialize(&app_state->font_system_memory_requirement, 0, &font_sys_config);
    app_state->font_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->font_system_memory_requirement);
    if (!font_system_initialize(&app_state->font_system_memory_requirement, app_state->font_system_state, &font_sys_config)) {
        KFATAL("Failed to initialize font system. Application cannot continue.");
        return false;
    }

    // Camera
    camera_system_config camera_sys_config;
    camera_sys_config.max_camera_count = 61;
    camera_system_initialize(&app_state->camera_system_memory_requirement, 0, camera_sys_config);
    app_state->camera_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->camera_system_memory_requirement);
    if (!camera_system_initialize(&app_state->camera_system_memory_requirement, app_state->camera_system_state, camera_sys_config)) {
        KFATAL("Failed to initialize camera system. Application cannot continue.");
        return false;
    }

    render_view_system_config render_view_sys_config = {};
    render_view_sys_config.max_view_count = 251;
    render_view_system_initialize(&app_state->renderer_view_system_memory_requirement, 0, render_view_sys_config);
    app_state->renderer_view_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->renderer_view_system_memory_requirement);
    if (!render_view_system_initialize(&app_state->renderer_view_system_memory_requirement, app_state->renderer_view_system_state, render_view_sys_config)) {
        KFATAL("Failed to initialize render view system. Aborting application.");
        return false;
    }

    // Load render views

    // Skybox view
    render_view_config skybox_config = {};
    skybox_config.type = RENDERER_VIEW_KNOWN_TYPE_SKYBOX;
    skybox_config.width = 0;
    skybox_config.height = 0;
    skybox_config.name = "skybox";
    skybox_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;

    // Renderpass config.
    skybox_config.pass_count = 1;
    renderpass_config skybox_passes[1];
    skybox_passes[0].name = "Renderpass.Builtin.Skybox";
    skybox_passes[0].render_area = (vec4){0, 0, 1280, 720};  // Default render area resolution.
    skybox_passes[0].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    skybox_passes[0].clear_flags = RENDERPASS_CLEAR_COLOUR_BUFFER_FLAG;
    skybox_passes[0].depth = 1.0f;
    skybox_passes[0].stencil = 0;

    render_target_attachment_config skybox_target_attachment = {};
    // Color attachment.
    skybox_target_attachment.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    skybox_target_attachment.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    skybox_target_attachment.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    skybox_target_attachment.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    skybox_target_attachment.present_after = false;

    skybox_passes[0].target.attachment_count = 1;
    skybox_passes[0].target.attachments = &skybox_target_attachment;
    skybox_passes[0].render_target_count = renderer_window_attachment_count_get();

    skybox_config.passes = skybox_passes;

    if (!render_view_system_create(&skybox_config)) {
        KFATAL("Failed to create skybox view. Aborting application.");
        return false;
    }

    // World view.
    render_view_config world_view_config = {};
    world_view_config.type = RENDERER_VIEW_KNOWN_TYPE_WORLD;
    world_view_config.width = 0;
    world_view_config.height = 0;
    world_view_config.name = "world";
    world_view_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;

    // Renderpass config.
    world_view_config.pass_count = 1;
    renderpass_config world_passes[1] = {0};
    world_passes[0].name = "Renderpass.Builtin.World";
    world_passes[0].render_area = (vec4){0, 0, 1280, 720};  // Default render area resolution.
    world_passes[0].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    world_passes[0].clear_flags = RENDERPASS_CLEAR_DEPTH_BUFFER_FLAG | RENDERPASS_CLEAR_STENCIL_BUFFER_FLAG;
    world_passes[0].depth = 1.0f;
    world_passes[0].stencil = 0;

    render_target_attachment_config world_target_attachments[2] = {0};
    // Colour attachment
    world_target_attachments[0].type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    world_target_attachments[0].source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    world_target_attachments[0].load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    world_target_attachments[0].store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_target_attachments[0].present_after = false;
    // Depth attachment
    world_target_attachments[1].type = RENDER_TARGET_ATTACHMENT_TYPE_DEPTH;
    world_target_attachments[1].source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    world_target_attachments[1].load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_DONT_CARE;
    world_target_attachments[1].store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    world_target_attachments[1].present_after = false;

    world_passes[0].target.attachment_count = 2;
    world_passes[0].target.attachments = world_target_attachments;
    world_passes[0].render_target_count = renderer_window_attachment_count_get();

    world_view_config.passes = world_passes;

    if (!render_view_system_create(&world_view_config)) {
        KFATAL("Failed to create world view. Aborting application.");
        return false;
    }

    // UI view
    render_view_config ui_view_config = {};
    ui_view_config.type = RENDERER_VIEW_KNOWN_TYPE_UI;
    ui_view_config.width = 0;
    ui_view_config.height = 0;
    ui_view_config.name = "ui";
    ui_view_config.view_matrix_source = RENDER_VIEW_VIEW_MATRIX_SOURCE_SCENE_CAMERA;

    // Renderpass config
    ui_view_config.pass_count = 1;
    renderpass_config ui_passes[1];
    ui_passes[0].name = "Renderpass.Builtin.UI";
    ui_passes[0].render_area = (vec4){0, 0, 1280, 720};
    ui_passes[0].clear_colour = (vec4){0.0f, 0.0f, 0.2f, 1.0f};
    ui_passes[0].clear_flags = RENDERPASS_CLEAR_NONE_FLAG;
    ui_passes[0].depth = 1.0f;
    ui_passes[0].stencil = 0;

    render_target_attachment_config ui_target_attachment = {};
    // Colour attachment.
    ui_target_attachment.type = RENDER_TARGET_ATTACHMENT_TYPE_COLOUR;
    ui_target_attachment.source = RENDER_TARGET_ATTACHMENT_SOURCE_DEFAULT;
    ui_target_attachment.load_operation = RENDER_TARGET_ATTACHMENT_LOAD_OPERATION_LOAD;
    ui_target_attachment.store_operation = RENDER_TARGET_ATTACHMENT_STORE_OPERATION_STORE;
    ui_target_attachment.present_after = true;

    ui_passes[0].target.attachment_count = 1;
    ui_passes[0].target.attachments = &ui_target_attachment;
    ui_passes[0].render_target_count = renderer_window_attachment_count_get();

    ui_view_config.passes = ui_passes;

    if (!render_view_system_create(&ui_view_config)) {
        KFATAL("Failed to create ui view. Aborting application.");
        return false;
    }

    // Material system.
    material_system_config material_sys_config;
    material_sys_config.max_material_count = 4096;
    material_system_initialize(&app_state->material_system_memory_requirement, 0, material_sys_config);
    app_state->material_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->material_system_memory_requirement);
    if (!material_system_initialize(&app_state->material_system_memory_requirement, app_state->material_system_state, material_sys_config)) {
        KFATAL("Failed to initialize material system. Application cannot continue.");
        return false;
    }

    // Geometry system.
    geometry_system_config geometry_sys_config;
    geometry_sys_config.max_geometry_count = 4096;
    geometry_system_initialize(&app_state->geometry_system_memory_requirement, 0, geometry_sys_config);
    app_state->geometry_system_state = linear_allocator_allocate(&app_state->systems_allocator, app_state->geometry_system_memory_requirement);
    if (!geometry_system_initialize(&app_state->geometry_system_memory_requirement, app_state->geometry_system_state, geometry_sys_config)) {
        KFATAL("Failed to initialize geometry system. Application cannot continue.");
        return false;
    }

    // TODO: temp

    // Create test ui text objects
    if (!ui_text_create(UI_TEXT_TYPE_BITMAP, "Ubuntu Mono 21px", 21, "Some test text 123,\n\tyo!", &app_state->test_text)) {
        KERROR("Failed to load basic ui bitmap text.");
        return false;
    }
    // Move debug text to new bottom of screen.
    ui_text_set_position(&app_state->test_text, vec3_create(20, app_state->height - 75, 0));

    if(!ui_text_create(UI_TEXT_TYPE_SYSTEM, "Noto Sans CJK JP", 31, "Some system text 123, \n\tyo!\n\n\tこんにちは 한", &app_state->test_sys_text)) {
        KERROR("Failed to load basic ui system text.");
        return false;
    }
    ui_text_set_position(&app_state->test_sys_text, vec3_create(50, 250, 0));

    // Skybox
    texture_map* cube_map = &app_state->sb.cubemap;
    cube_map->filter_magnify = cube_map->filter_minify = TEXTURE_FILTER_MODE_LINEAR;
    cube_map->repeat_u = cube_map->repeat_v = cube_map->repeat_w = TEXTURE_REPEAT_CLAMP_TO_EDGE;
    cube_map->use = TEXTURE_USE_MAP_CUBEMAP;
    if (!renderer_texture_map_acquire_resources(cube_map)) {
        KFATAL("Unable to acquire resources for cube map texture.");
        return false;
    }
    cube_map->texture = texture_system_acquire_cube("skybox", true);
    geometry_config skybox_cube_config = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, "skybox_cube", 0);
    // Clear out the material name.
    skybox_cube_config.material_name[0] = 0;
    app_state->sb.g = geometry_system_acquire_from_config(skybox_cube_config, true);
    app_state->sb.render_frame_number = INVALID_ID_U64;
    shader* skybox_shader = shader_system_get("Shader.Builtin.Skybox");
    texture_map* maps[1] = {&app_state->sb.cubemap};
    if (!renderer_shader_acquire_instance_resources(skybox_shader, maps, &app_state->sb.instance_id)) {
        KFATAL("Unable to acquire shader resources for skybox texture.");
        return false;
    }

    // World meshes
    // Invalidate all meshes.
    for (u32 i = 0; i < 10; ++i) {
        app_state->meshes[i].generation = INVALID_ID_U8;
        app_state->ui_meshes[i].generation = INVALID_ID_U8;
    }

    u8 mesh_count = 0;

    // Load up a cube configuration, and load geometry from it.
    mesh* cube_mesh = &app_state->meshes[mesh_count];
    cube_mesh->geometry_count = 1;
    cube_mesh->geometries = kallocate(sizeof(mesh*) * cube_mesh->geometry_count, MEMORY_TAG_ARRAY);
    geometry_config g_config = geometry_system_generate_cube_config(10.0f, 10.0f, 10.0f, 1.0f, 1.0f, "test_cube", "test_material");
    cube_mesh->geometries[0] = geometry_system_acquire_from_config(g_config, true);
    cube_mesh->transform = transform_create();
    mesh_count++;
    cube_mesh->generation = 0;
    cube_mesh->unique_id = identifier_aquire_new_id(cube_mesh);
    // Clean up the allocations for the geometry config.
    geometry_system_config_dispose(&g_config);

    // A second cube
    mesh* cube_mesh_2 = &app_state->meshes[mesh_count];
    cube_mesh_2->geometry_count = 1;
    cube_mesh_2->geometries = kallocate(sizeof(mesh*) * cube_mesh_2->geometry_count, MEMORY_TAG_ARRAY);
    g_config = geometry_system_generate_cube_config(5.0f, 5.0f, 5.0f, 1.0f, 1.0f, "test_cube_2", "test_material");
    cube_mesh_2->geometries[0] = geometry_system_acquire_from_config(g_config, true);
    cube_mesh_2->transform = transform_from_position((vec3){10.0f, 0.0f, 1.0f});
    // Set the first cube as the parent to the second.
    transform_set_parent(&cube_mesh_2->transform, &cube_mesh->transform);
    mesh_count++;
    cube_mesh_2->generation = 0;
    cube_mesh_2->unique_id = identifier_aquire_new_id(cube_mesh_2);
    // Clean up the allocations for the geometry config.
    geometry_system_config_dispose(&g_config);

    // A third cube!
    mesh* cube_mesh_3 = &app_state->meshes[mesh_count];
    cube_mesh_3->geometry_count = 1;
    cube_mesh_3->geometries = kallocate(sizeof(mesh*) * cube_mesh_3->geometry_count, MEMORY_TAG_ARRAY);
    g_config = geometry_system_generate_cube_config(2.0f, 2.0f, 2.0f, 1.0f, 1.0f, "test_cube_2", "test_material");
    cube_mesh_3->geometries[0] = geometry_system_acquire_from_config(g_config, true);
    cube_mesh_3->transform = transform_from_position((vec3){5.0f, 0.0f, 1.0f});
    // Set the second cube as the parent to the third.
    transform_set_parent(&cube_mesh_3->transform, &cube_mesh_2->transform);
    mesh_count++;
    cube_mesh_3->generation = 0;
    cube_mesh_3->unique_id = identifier_aquire_new_id(cube_mesh_3);
    // Clean up the allocations for the geometry config.
    geometry_system_config_dispose(&g_config);

    app_state->car_mesh = &app_state->meshes[mesh_count];
    app_state->car_mesh->unique_id = identifier_aquire_new_id(app_state->car_mesh);
    app_state->car_mesh->transform = transform_from_position((vec3){15.0f, 0.0f, 1.0f});
    mesh_count++;

    app_state->sponza_mesh = &app_state->meshes[mesh_count];
    app_state->sponza_mesh->unique_id = identifier_aquire_new_id(app_state->sponza_mesh);
    app_state->sponza_mesh->transform = transform_from_position_rotation_scale((vec3){15.0f, 0.0f, 1.0f}, quat_identity(), (vec3){0.05f, 0.05f, 0.05f});
    mesh_count++;

    // Load up some test UI geometry.
    geometry_config ui_config;
    ui_config.vertex_size = sizeof(vertex_2d);
    ui_config.vertex_count = 4;
    ui_config.index_size = sizeof(u32);
    ui_config.index_count = 6;
    string_ncopy(ui_config.material_name, "test_ui_material", MATERIAL_NAME_MAX_LENGTH);
    string_ncopy(ui_config.name, "test_ui_geometry", GEOMETRY_NAME_MAX_LENGTH);

    const f32 w = 128.0f;
    const f32 h = 32.0f;
    vertex_2d uiverts[4];
    uiverts[0].position.x = 0.0f;  // 0    3
    uiverts[0].position.y = 0.0f;  //
    uiverts[0].texcoord.x = 0.0f;  //
    uiverts[0].texcoord.y = 0.0f;  // 2    1

    uiverts[1].position.y = h;
    uiverts[1].position.x = w;
    uiverts[1].texcoord.x = 1.0f;
    uiverts[1].texcoord.y = 1.0f;

    uiverts[2].position.x = 0.0f;
    uiverts[2].position.y = h;
    uiverts[2].texcoord.x = 0.0f;
    uiverts[2].texcoord.y = 1.0f;

    uiverts[3].position.x = w;
    uiverts[3].position.y = 0.0;
    uiverts[3].texcoord.x = 1.0f;
    uiverts[3].texcoord.y = 0.0f;
    ui_config.vertices = uiverts;

    // Indices - counter-clockwise
    u32 uiindices[6] = {2, 1, 0, 3, 0, 1};
    ui_config.indices = uiindices;

    // Get UI geometry from config.
    app_state->ui_meshes[0].unique_id = identifier_aquire_new_id(&app_state->ui_meshes[0]);
    app_state->ui_meshes[0].geometry_count = 1;
    app_state->ui_meshes[0].geometries = kallocate(sizeof(geometry*), MEMORY_TAG_ARRAY);
    app_state->ui_meshes[0].geometries[0] = geometry_system_acquire_from_config(ui_config, true);
    app_state->ui_meshes[0].transform = transform_create();
    app_state->ui_meshes[0].generation = 0;

    // Load up default geometry.
    // app_state->test_geometry = geometry_system_get_default();
    // TODO: end temp

    // Initialize the game.
    if (!app_state->game_inst->initialize(app_state->game_inst)) {
        KFATAL("Game failed to initialize.");
        return false;
    }

    // Call resize once to ensure the proper size has been set.
    renderer_on_resized(app_state->width, app_state->height);
    app_state->game_inst->on_resize(app_state->game_inst, app_state->width, app_state->height);

    return true;
}

#define AVG_COUNT 30
b8 application_run() {
    app_state->is_running = true;
    clock_start(&app_state->clock);
    clock_update(&app_state->clock);
    app_state->last_time = app_state->clock.elapsed;
    // f64 running_time = 0;
    u8 frame_count = 0;
    f64 target_frame_seconds = 1.0f / 60;
    f64 frame_elapsed_time = 0;
    u8 frame_avg_counter = 0;
    f64 ms_times[AVG_COUNT] = {0};
    f64 ms_avg = 0;
    i32 frames = 0;
    f64 accumulated_frame_ms = 0;
    f64 fps = 0;

    KINFO(get_memory_usage_str());

    while (app_state->is_running) {
        if (!platform_pump_messages()) {
            app_state->is_running = false;
        }

        if (!app_state->is_suspended) {
            // Update clock and get delta time.
            clock_update(&app_state->clock);
            f64 current_time = app_state->clock.elapsed;
            f64 delta = (current_time - app_state->last_time);
            f64 frame_start_time = platform_get_absolute_time();

            // Update the job system.
            job_system_update();

            if (!app_state->game_inst->update(app_state->game_inst, (f32)delta)) {
                KFATAL("Game update failed, shutting down.");
                app_state->is_running = false;
                break;
            }

            // Call the game's render routine.
            if (!app_state->game_inst->render(app_state->game_inst, (f32)delta)) {
                KFATAL("Game render failed, shutting down.");
                app_state->is_running = false;
                break;
            }

            // Perform a small rotation on the first mesh.
            quat rotation = quat_from_axis_angle((vec3){0, 1, 0}, 0.5f * delta, false);
            transform_rotate(&app_state->meshes[0].transform, rotation);

            // Perform a similar rotation on the second mesh, if it exists.
            transform_rotate(&app_state->meshes[1].transform, rotation);

            // Perform a similar rotation on the third mesh, if it exists.
            transform_rotate(&app_state->meshes[2].transform, rotation);

            // TODO: refactor packet creation
            render_packet packet = {};
            packet.delta_time = delta;

            // TODO: Read from frame config.
            packet.view_count = 3;
            render_view_packet views[3];
            kzero_memory(views, sizeof(render_view_packet) * packet.view_count);
            packet.views = views;

            // Skybox
            skybox_packet_data skybox_data = {};
            skybox_data.sb = &app_state->sb;
            if (!render_view_system_build_packet(render_view_system_get("skybox"), &skybox_data, &packet.views[0])) {
                KERROR("Failed to build packet for view 'skybox'.");
                return false;
            }

            // World
            mesh_packet_data world_mesh_data = {};

            u32 mesh_count = 0;
            mesh* meshes[10];
            // TODO: flexible size array
            for (u32 i = 0; i < 10; ++i) {
                if (app_state->meshes[i].generation != INVALID_ID_U8) {
                    meshes[mesh_count] = &app_state->meshes[i];
                    mesh_count++;
                }
            }
            world_mesh_data.mesh_count = mesh_count;
            world_mesh_data.meshes = meshes;

            // TODO: performs a lookup on every frame.
            if (!render_view_system_build_packet(render_view_system_get("world"), &world_mesh_data, &packet.views[1])) {
                KERROR("Failed to build packet for view 'world_opaque'.");
                return false;
            }

            // ui

            // Update the bitmap text with camera position. NOTE: just using the default camera for now.
            camera* world_camera = camera_system_get_default();
            vec3 pos = camera_position_get(world_camera);
            vec3 rot = camera_rotation_euler_get(world_camera);

            // Also tack on current mouse state.
            b8 left_down = input_is_button_down(BUTTON_LEFT);
            b8 right_down = input_is_button_down(BUTTON_RIGHT);
            i32 mouse_x, mouse_y;
            input_get_mouse_position(&mouse_x, &mouse_y);

            // Convert to NDC
            f32 mouse_x_ndc = range_convert_f32((f32)mouse_x, 0.0f, (f32)app_state->width, -1.0f, 1.0f);
            f32 mouse_y_ndc = range_convert_f32((f32)mouse_y, 0.0f, (f32)app_state->height, -1.0f, 1.0f);

            // Calculate frame ms average
            f64 frame_ms = (frame_elapsed_time * 1000.0);
            ms_times[frame_avg_counter] = frame_ms;
            if (frame_avg_counter == AVG_COUNT - 1) {
                for (u8 i = 0; i < AVG_COUNT; ++i) {
                    ms_avg += ms_times[i];
                }

                ms_avg /= AVG_COUNT;
            }
            frame_avg_counter++;
            frame_avg_counter %= AVG_COUNT;

            // Calculate frames per second.
            accumulated_frame_ms += frame_ms;
            if (accumulated_frame_ms > 1000) {
                fps = frames;
                accumulated_frame_ms -= 1000;
                frames = 0;
            }

            char text_buffer[256];
            string_format(
                text_buffer,
                "\
FPS: %5.1f(%4.1fms)        Pos=[%7.3f %7.3f %7.3f] Rot=[%7.3f, %7.3f, %7.3f]\n\
Mouse: X=%-5d Y=%-5d   L=%s R=%s   NDC: X=%.6f, Y=%.6f\n\
Hovered: %s%u",
                fps,
                ms_avg,
                pos.x, pos.y, pos.z,
                rad_to_deg(rot.x), rad_to_deg(rot.y), rad_to_deg(rot.z),
                mouse_x, mouse_y,
                left_down ? "Y" : "N",
                right_down ? "Y" : "N",
                mouse_x_ndc,
                mouse_y_ndc,
                app_state->hovered_object_id == INVALID_ID ? "none" : "",
                app_state->hovered_object_id == INVALID_ID ? 0 : app_state->hovered_object_id);
            ui_text_set_text(&app_state->test_text, text_buffer);

            ui_packet_data ui_packet = {};

            u32 ui_mesh_count = 0;
            mesh* ui_meshes[10];

            // TODO: flexible size array
            for (u32 i = 0; i < 10; ++i) {
                if (app_state->ui_meshes[i].generation != INVALID_ID_U8) {
                    ui_meshes[ui_mesh_count] = &app_state->ui_meshes[i];
                    ui_mesh_count++;
                }
            }

            ui_packet.mesh_data.mesh_count = ui_mesh_count;
            ui_packet.mesh_data.meshes = ui_meshes;
            ui_packet.text_count = 2;
            ui_text* texts[2];
            texts[0] = &app_state->test_text;
            texts[1] = &app_state->test_sys_text;
            ui_packet.texts = texts;
            if (!render_view_system_build_packet(render_view_system_get("ui"), &ui_packet, &packet.views[2])) {
                KERROR("Failed to build packet for view 'ui'.");
                return false;
            }

            // TODO: end temp

            renderer_draw_frame(&packet);

            // TODO: temp
            // Cleanup the packet.
            for (u32 i = 0; i < packet.view_count; ++i) {
                packet.views[i].view->on_destroy_packet(packet.views[i].view, &packet.views[i]);
            }
            // TODO: end temp

            // Figure out how long the frame took and, if below
            f64 frame_end_time = platform_get_absolute_time();
            frame_elapsed_time = frame_end_time - frame_start_time;
            // running_time += frame_elapsed_time;
            f64 remaining_seconds = target_frame_seconds - frame_elapsed_time;

            if (remaining_seconds > 0) {
                u64 remaining_ms = (remaining_seconds * 1000);

                // If there is time left, give it back to the OS.
                b8 limit_frames = false;
                if (remaining_ms > 0 && limit_frames) {
                    platform_sleep(remaining_ms - 1);
                }

                frame_count++;
            }

            // Count all frames.
            frames++;

            // NOTE: Input update/state copying should always be handled
            // after any input should be recorded; I.E. before this line.
            // As a safety, input is the last thing to be updated before
            // this frame ends.
            input_update(delta);

            // Update last time
            app_state->last_time = current_time;
        }
    }

    app_state->is_running = false;

    // TODO: Temp
    // TODO: implement skybox destroy.
    renderer_texture_map_release_resources(&app_state->sb.cubemap);
    // Destroy ui texts
    ui_text_destroy(&app_state->test_text);
    ui_text_destroy(&app_state->test_sys_text);
    // TODO: end temp

    // Shutdown event system.
    event_unregister(EVENT_CODE_APPLICATION_QUIT, 0, application_on_event);
    event_unregister(EVENT_CODE_KEY_PRESSED, 0, application_on_key);
    event_unregister(EVENT_CODE_KEY_RELEASED, 0, application_on_key);
    // TODO: temp
    event_unregister(EVENT_CODE_DEBUG0, 0, event_on_debug_event);
    // TODO: end temp

    input_system_shutdown(app_state->input_system_state);

    font_system_shutdown(app_state->font_system_state);

    geometry_system_shutdown(app_state->geometry_system_state);

    material_system_shutdown(app_state->material_system_state);

    texture_system_shutdown(app_state->texture_system_state);

    shader_system_shutdown(app_state->shader_system_state);

    renderer_system_shutdown(app_state->renderer_system_state);

    resource_system_shutdown(app_state->resource_system_state);

    job_system_shutdown(app_state->job_system_state);

    platform_system_shutdown(app_state->platform_system_state);

    event_system_shutdown(app_state->event_system_state);

    memory_system_shutdown();

    return true;
}

void application_get_framebuffer_size(u32* width, u32* height) {
    *width = app_state->width;
    *height = app_state->height;
}

b8 application_on_event(u16 code, void* sender, void* listener_inst, event_context context) {
    switch (code) {
        case EVENT_CODE_APPLICATION_QUIT: {
            KINFO("EVENT_CODE_APPLICATION_QUIT recieved, shutting down.\n");
            app_state->is_running = false;
            return true;
        }
        case EVENT_CODE_OBJECT_HOVER_ID_CHANGED: {
            app_state->hovered_object_id = context.data.u32[0];
            return true;
        }
    }

    return false;
}

b8 application_on_key(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_KEY_PRESSED) {
        u16 key_code = context.data.u16[0];
        if (key_code == KEY_ESCAPE) {
            // NOTE: Technically firing an event to itself, but there may be other listeners.
            event_context data = {};
            event_fire(EVENT_CODE_APPLICATION_QUIT, 0, data);

            // Block anything else from processing this.
            return true;
        } else if (key_code == KEY_A) {
            // Example on checking for a key
            KDEBUG("Explicit - A key pressed!");
        } else {
            
            KDEBUG("'%s' key pressed in window.", input_keycode_str(key_code));
        }
    } else if (code == EVENT_CODE_KEY_RELEASED) {
        u16 key_code = context.data.u16[0];
        if (key_code == KEY_B) {
            // Example on checking for a key
            KDEBUG("Explicit - B key released!");
        } else {
            KDEBUG("'%s' key released in window.", input_keycode_str(key_code));
        }
    }
    return false;
}

b8 application_on_resized(u16 code, void* sender, void* listener_inst, event_context context) {
    if (code == EVENT_CODE_RESIZED) {
        u16 width = context.data.u16[0];
        u16 height = context.data.u16[1];

        // Check if different. If so, trigger a resize event.
        if (width != app_state->width || height != app_state->height) {
            app_state->width = width;
            app_state->height = height;

            KDEBUG("Window resize: %i, %i", width, height);

            // Handle minimization
            if (width == 0 || height == 0) {
                KINFO("Window minimized, suspending application.");
                app_state->is_suspended = true;
                return true;
            } else {
                if (app_state->is_suspended) {
                    KINFO("Window restored, resuming application.");
                    app_state->is_suspended = false;
                }
                app_state->game_inst->on_resize(app_state->game_inst, width, height);
                renderer_on_resized(width, height);
            }
  
            // TODO: temp
            // Move debug text to new bottom of screen.
            ui_text_set_position(&app_state->test_text, vec3_create(20, app_state->height - 75, 0));
            // TODO: end temp
        }
    }

    // Event purposely not handled to allow other listeners to get this.
    return false;
}