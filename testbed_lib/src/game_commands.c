#include <core/console.h>
#include <application_types.h>
#include <core/event.h>

void game_command_exit(console_command_context context) {
    KDEBUG("game exit called!");
    event_fire(EVENT_CODE_APPLICATION_QUIT, 0, (event_context){});
}

void game_setup_commands(application* game_inst) {
    console_register_command("exit", 0, game_command_exit);
    console_register_command("quit", 0, game_command_exit);
}
