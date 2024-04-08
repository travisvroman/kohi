#include "application_config.h"
#include "containers/darray.h"
#include "logger.h"
#include "math/kmath.h"
#include "parsers/kson_parser.h"
#include "platform/platform.h"
#include "strings/kstring.h"

b8 application_config_parse_file_content(const char* file_content, application_config* out_config) {
    if (!file_content || !out_config) {
        KERROR("application_config_parse_file_content requires valid pointers to file_content and out_config.");
        return false;
    }

    // Attempt to parse the file into a tree.
    kson_tree app_config_tree = {0};
    if (!kson_tree_from_string(file_content, &app_config_tree)) {
        KERROR("Failed to parse application config file content. See logs for details.");
        return false;
    }

    // Extract required properties.
    if (!kson_object_property_value_get_string(&app_config_tree.root, "app_name", &out_config->name)) {
        KERROR("Failed to find property 'app_name', which is required.");
        return false;
    }

    if (!kson_object_property_value_get_string(&app_config_tree.root, "renderer_plugin_name", &out_config->renderer_plugin_name)) {
        KERROR("Failed to find property 'renderer_plugin_name', which is required.");
        return false;
    }

    if (!kson_object_property_value_get_string(&app_config_tree.root, "audio_plugin_name", &out_config->audio_plugin_name)) {
        KERROR("Failed to find property 'audio_plugin_name', which is required.");
        return false;
    }

    // frame_allocator_size is optional, so use a defualt if it isn't defined.
    i64 frame_alloc_size = 0; // kson doesn't do unsigned ints, so convert it after.
    if (!kson_object_property_value_get_int(&app_config_tree.root, "frame_allocator_size", &frame_alloc_size)) {
        out_config->frame_allocator_size = 64;
    } else {
        out_config->frame_allocator_size = (u64)frame_alloc_size;
    }

    // app_frame_data_size is optional, so use a defualt if it isn't defined.
    // NOTE: It's likely the application will want to override this anyway with a sizeof(some_struct).
    i64 iapp_frame_data_size = 0; // kson doesn't do unsigned ints, so convert it after.
    if (!kson_object_property_value_get_int(&app_config_tree.root, "app_frame_data_size", &iapp_frame_data_size)) {
        // Default of 0 means not used.
        out_config->app_frame_data_size = 0;
    } else {
        out_config->app_frame_data_size = (u64)iapp_frame_data_size;
    }

    // Window configs.
    out_config->windows = darray_create(application_window_config);
    kson_array window_configs_array;
    if (kson_object_property_value_get_object(&app_config_tree.root, "windows", &window_configs_array)) {
        u32 window_config_count = 0;
        if (!kson_array_element_count_get(&window_configs_array, &window_config_count)) {
            KERROR("Failed to get element count of 'windows' array. Using default config.");
        }

        for (u32 i = 0; i < window_config_count; ++i) {
            kson_object window_config = {0};
            if (!kson_array_element_value_get_object(&window_configs_array, i, &window_config)) {
                KERROR("Failed to get window object at index %u. Continuing on and trying the next...", i);
                continue;
            }

            // Parse window properties. Nothing is technically required here, just use sane defaults for undefined options.
            // Name
            kwindow_config new_window = {0};
            if (!kson_object_property_value_get_string(&window_config, "name", &new_window.name)) {
                char name_buf[256] = {0};
                string_format(name_buf, "app_window_%u", i);
                new_window.name = string_duplicate(name_buf);
            }

            // Title
            if (!kson_object_property_value_get_string(&window_config, "title", &new_window.title)) {
                char title_buf[256] = {0};
                string_format(title_buf, "Kohi Application Window %u", i);
                new_window.title = string_duplicate(title_buf);
            }

            // Resolution
            const char* res_str = 0;
            if (kson_object_property_value_get_string(&window_config, "resolution", &res_str)) {
                vec2 resolution;
                if (string_to_vec2(res_str, &resolution)) {
                    new_window.width = (u32)resolution.x;
                    new_window.height = (u32)resolution.y;
                }
            }
            if (!new_window.width) {
                new_window.width = 1280;
            }
            if (!new_window.height) {
                new_window.height = 720;
            }

            // Starting position.
            const char* sp_str = 0;
            if (kson_object_property_value_get_string(&window_config, "position", &sp_str)) {
                vec2 start_position;
                if (string_to_vec2(res_str, &start_position)) {
                    new_window.position_x = (u32)start_position.x;
                    new_window.position_y = (u32)start_position.y;
                }
            }
            // TODO: Maybe use some value here to indicate a "use default" to the platform layer?
            if (!new_window.position_x) {
                new_window.position_x = 10;
            }
            if (!new_window.position_y) {
                new_window.position_y = 10;
            }
        }
    }

    // Make sure there is at least one window available.
    u32 window_count = darray_length(out_config->windows);
    if (window_count == 0) {
        KWARN("A window configuration was not provided or was not valid, so a default one will be used.");
        application_window_config win = {0};
        win.name = "main_window";
        win.title = "Kohi Application Main Window";
        win.position = vec2_create(100, 100);
        win.resolution = vec2_create(1280, 720);
        darray_push(out_config->windows, win);
    }

    // System configs
    out_config->systems = darray_create(application_system_config);
    kson_array system_configs_array;
    if (!kson_object_property_value_get_object(&app_config_tree.root, "systems", &system_configs_array)) {
        KERROR("systems config is required in application configuration.");
        return false;
    }

    // Loop through and fill up struct
}
