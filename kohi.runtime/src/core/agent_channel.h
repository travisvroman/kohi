/**
 * @file agent_channel.h
 * @brief A control channel for AI agents and test harnesses: JSON lines over stdin and stdout.
 *
 * Start the engine with `--agent` to enable it. Each line on stdin is one request, a flat JSON
 * object with a "cmd" and an optional "id":
 *
 *   {"id": 1, "cmd": "step", "count": 60}
 *
 * Each request gets exactly one reply line on stdout, in the order the requests were sent:
 *
 *   {"id": 1, "ok": true, "step": 60, "time": 1.0}
 *   {"id": 2, "ok": false, "error": "unknown command 'jump'"}
 *
 * Requests run one at a time. A "step" request holds the requests after it until its steps have
 * run, so an agent can send "step" then "key" and know the key lands after those steps. Logs go
 * to stderr, so stdout carries only replies. Closing stdin quits the engine.
 *
 * In agent mode the simulation starts paused and only advances when asked to, which needs
 * fixed_step_hz in the app config. Send {"cmd": "commands"} for the list of commands.
 *
 * Applications and plugins can add commands with agent_command_register.
 */
#pragma once

#include <defines.h>

typedef struct agent_request agent_request;
typedef struct agent_reply agent_reply;

/**
 * @brief Handles one agent command. Read arguments from request and add result fields to reply.
 * @returns False on failure; set a message with agent_reply_error first.
 */
typedef b8 (*PFN_agent_command)(const agent_request *request, agent_reply *reply, void *listener);

/** @brief Adds a command, or replaces one with the same name. Callable before or after the channel starts. */
KAPI b8 agent_command_register (const char *name, const char *help, void *listener, PFN_agent_command handler);

/** @brief True when the engine was started with --agent. */
KAPI b8 agent_channel_enabled (void);

KAPI b8 agent_request_string_get (const agent_request *request, const char *key, const char **out_value);
KAPI b8 agent_request_number_get (const agent_request *request, const char *key, f64 *out_value);
KAPI b8 agent_request_bool_get (const agent_request *request, const char *key, b8 *out_value);

KAPI void agent_reply_string_add (agent_reply *reply, const char *key, const char *value);
KAPI void agent_reply_number_add (agent_reply *reply, const char *key, f64 value);
KAPI void agent_reply_int_add (agent_reply *reply, const char *key, i64 value);
KAPI void agent_reply_bool_add (agent_reply *reply, const char *key, b8 value);
/** @brief Adds a field whose value is already valid JSON, such as an array or object. */
KAPI void agent_reply_json_add (agent_reply *reply, const char *key, const char *json);
/** @brief Marks the reply as failed with a message. The handler should then return false. */
KAPI void agent_reply_error (agent_reply *reply, const char *message);

/**
 * @brief Appends value to a JSON string buffer as a quoted, escaped JSON string. *buffer may be
 * null and grows as needed. For building agent_reply_json_add values; free with string_free.
 */
KAPI void agent_json_append_string (char **buffer, const char *value);
/** @brief Appends raw text to a JSON string buffer (see agent_json_append_string). */
KAPI void agent_json_append (char **buffer, const char *text);

// Engine-only.

/** @brief Turns agent mode on. Called from main() for --agent, before the engine starts, so no log reaches stdout. */
KAPI void agent_channel_enable (void);
b8 agent_channel_start (void);
void agent_channel_shutdown (void);
/** @brief Runs queued requests. Called once per frame, before the simulation steps. */
void agent_channel_update (void);
