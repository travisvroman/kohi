#!/usr/bin/env python3
"""Checks that a save file carries the world from one engine process into another.

Run from the repository root after a build:
    python3 scripts/save_game_check.py

Run 1 steps the headless testbed, saves a game, steps 30 more times and notes the hashes. Run 2 is
a new process: it loads the save and steps 30 times. The check passes when both runs end with the
same physics and scene hashes. It also checks that a damaged save is refused and changes nothing.
"""
import os
import sys

from agent_demo import BIN, CONFIG, Engine

SAVE = os.path.join(BIN, "saves", "check.ksav")


def hashes(engine):
    return engine.call("physics_hash")["hash"], engine.call("world_hash")["scene_hash"]


def main():
    if os.path.exists(SAVE):
        os.remove(SAVE)

    first = Engine(CONFIG, os.path.join(BIN, "save_game_check_1.log"))
    try:
        first.call("step", count=30)
        first.call("save_game", slot="check")
        first.call("step", count=30)
        expected = hashes(first)
    finally:
        first.close()

    second = Engine(CONFIG, os.path.join(BIN, "save_game_check_2.log"))
    try:
        second.call("step", count=1)  # the scene finishes loading on the first frame
        second.call("load_game", slot="check")
        second.call("step", count=30)
        loaded = hashes(second)

        # Flip one byte: the load must fail and the world must stay where it is.
        with open(SAVE, "r+b") as f:
            f.seek(40)
            byte = f.read(1)
            f.seek(40)
            f.write(bytes([byte[0] ^ 0x40]))
        before = hashes(second)
        try:
            second.call("load_game", slot="check")
            damaged_refused = False
        except RuntimeError:
            damaged_refused = True
        after = hashes(second)
    finally:
        code = second.close()

    print(f"run 1 at step 60: physics {expected[0]}, scene {expected[1]}")
    print(f"run 2 after load and 30 steps: physics {loaded[0]}, scene {loaded[1]}")
    if loaded != expected:
        print("FAIL: the loaded game did not reach the same state")
        return 1
    if not damaged_refused or before != after:
        print("FAIL: a damaged save was accepted or changed the world")
        return 1
    if code != 0:
        print(f"FAIL: engine exit code {code}")
        return 1
    print("PASS: the save carried the world into a new process; a damaged save was refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
