#!/usr/bin/env python3
"""Drives the headless testbed over the agent channel and checks physics determinism.

Run from the repository root after a build:
    python3 scripts/agent_demo.py

It steps the physics scene, saves the physics state, steps on, restores the state, and replays
the same steps. The run passes when the replay ends at the same physics hash as the first time.
"""
import json
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(REPO, "bin")
CONFIG = os.path.join(REPO, "testbed.kapp", "app_config_headless.kson")
# An absolute path: Windows resolves a relative one against this script's working directory, not
# cwd, and does not add .exe to a name that already has an extension.
EXE = os.path.join(BIN, "testbed.kapp.exe" if os.name == "nt" else "testbed.kapp")


class Engine:
    """A running engine with the agent channel on stdin/stdout. Logs go to log_path."""

    def __init__(self, config, log_path, extra_args=()):
        self.log = open(log_path, "w")
        self.process = subprocess.Popen(
            [EXE, "--agent", "--config", config, *extra_args],
            cwd=BIN,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=self.log,
            text=True,
            # OpenAL needs an output device; the null driver works without one.
            env=dict(os.environ, ALSOFT_DRIVERS="null"),
        )
        self.next_id = 1

    def call(self, cmd, **args):
        """Sends one request and returns its reply. Raises if the engine reports an error."""
        request = {"id": self.next_id, "cmd": cmd, **args}
        self.next_id += 1
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError(f"engine exited; see {self.log.name}")
        reply = json.loads(line)
        if not reply["ok"]:
            raise RuntimeError(f"{cmd} failed: {reply['error']}")
        return reply

    def close(self):
        self.call("quit")
        self.process.stdin.close()
        code = self.process.wait()
        self.log.close()
        return code


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "agent_demo.log"))
    try:
        engine.call("step", count=30)
        engine.call("console", line="physics_state_save")
        engine.call("step", count=30)
        first = engine.call("physics_hash")
        engine.call("console", line="physics_state_restore")
        engine.call("step", count=30)
        replay = engine.call("physics_hash")
    finally:
        code = engine.close()

    print(f"step {first['step']}: {first['hash']}")
    print(f"step {replay['step']} (after restore and replay): {replay['hash']}")
    if first["hash"] != replay["hash"] or code != 0:
        print("FAIL")
        return 1
    print("PASS: the replay reproduced the physics state exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
