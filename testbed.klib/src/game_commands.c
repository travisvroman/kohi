#include <application/application_types.h>
#include <core/console.h>
#include <core/event.h>

void game_command_exit(console_command_context context) {
    KDEBUG("game exit called!");
    event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
}

void game_setup_commands(application* app) {
    console_command_register("exit", 0, app, game_command_exit);
    console_command_register("quit", 0, app, game_command_exit);
}

void game_remove_commands(struct application* game_inst) {
    console_command_unregister("exit");
    console_command_unregister("quit");
}
