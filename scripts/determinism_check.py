#!/usr/bin/env python3
"""Checks that a run with input repeats exactly: record it, replay it, compare.

Run from the repository root after a build:
    python3 scripts/determinism_check.py

1. Drives the headless testbed over the agent channel for 600 steps, pushing the ball in the
   physics scene with the arrow keys, and records the input with --record.
2. Replays the recording twice with --replay (no agent), the second time recording it again.
3. Passes when all three runs end at the same physics hash, that hash differs from a run with
   no input (so the input took effect), and the re-recorded file matches the original.
"""
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from agent_demo import BIN, CONFIG, EXE, Engine  # noqa: E402

STEPS = 600  # max_frames in the headless config; lockstep runs one step per frame.
HASH_LINE = re.compile(r"Physics state hash: ([0-9a-f]+)")


def recorded_run(recording):
    """Runs the scripted agent session and returns its physics hash."""
    engine = Engine(CONFIG, os.path.join(BIN, "determinism_record.log"), ["--record", recording])
    try:
        engine.call("step", count=100)
        engine.call("key", key="right", pressed=True)
        engine.call("step", count=40)
        engine.call("key", key="up", pressed=True)
        engine.call("mouse_move", x=100, y=50)
        engine.call("step", count=25)
        engine.call("key", key="right", pressed=False)
        engine.call("mouse_button", button="left", pressed=True)
        engine.call("mouse_button", button="left", pressed=False)
        engine.call("step", count=20)
        engine.call("key", key="up", pressed=False)
        engine.call("step", count=STEPS - 185)
        reply = engine.call("physics_hash")
    finally:
        code = engine.close()
    if code != 0 or reply["step"] != STEPS:
        raise RuntimeError(f"recorded run exited {code} at step {reply['step']}")
    return reply["hash"]


def headless_run(*args):
    """Runs the headless testbed to max_frames and returns the physics hash from its log."""
    result = subprocess.run(
        [EXE, "--config", CONFIG, *args],
        cwd=BIN,
        capture_output=True,
        text=True,
        env=dict(os.environ, ALSOFT_DRIVERS="null"),
    )
    log = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout + result.stderr)
    hashes = HASH_LINE.findall(log)
    if result.returncode != 0 or not hashes:
        print(log[-4000:])
        raise RuntimeError(f"headless run {args} exited {result.returncode}")
    return hashes[-1]


def main():
    recording = os.path.join(BIN, "determinism_check.rec")
    rerecording = os.path.join(BIN, "determinism_check_replay.rec")

    no_input = headless_run()
    recorded = recorded_run(recording)
    replay_1 = headless_run("--replay", recording)
    replay_2 = headless_run("--replay", recording, "--record", rerecording)
    with open(recording) as a, open(rerecording) as b:
        same_file = a.read() == b.read()

    print(f"no input:      {no_input}")
    print(f"recorded run:  {recorded}")
    print(f"replay 1:      {replay_1}")
    print(f"replay 2:      {replay_2}")
    print(f"re-recording matches: {same_file}")

    if recorded == no_input:
        print("FAIL: the recorded input did not change the physics state")
        return 1
    if not (recorded == replay_1 == replay_2) or not same_file:
        print("FAIL: the replays did not repeat the recorded run")
        return 1
    print("PASS: the recorded run replayed exactly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
