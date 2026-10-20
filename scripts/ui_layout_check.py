#!/usr/bin/env python3
"""Checks KUI anchoring and resolution-independent scaling over the agent channel.

Run from the repository root after a build:
    python3 scripts/ui_layout_check.py

The headless testbed anchors a progress bar (300 x 20 units) to the top right corner with a
(-20, 20) offset. The check resizes the UI's screen and reference size and reads where it lands.
"""
import os
import sys

from agent_demo import BIN, CONFIG, Engine


def rect(engine, name):
    for c in engine.call("ui_controls")["controls"]:
        if c["name"] == name:
            return c["rect"]
    raise RuntimeError(f"no control named {name}")


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "ui_layout_check.log"))
    failures = []

    def expect(what, actual, wanted):
        if [round(v, 2) for v in actual] != wanted:
            failures.append(f"{what}: got {actual!r}, wanted {wanted!r}")

    try:
        engine.call("step", count=2)

        engine.call("ui_screen_size", width=1280, height=720)
        expect("top right at 1280x720", rect(engine, "demo_progress"), [960, 20, 300, 20])

        # The anchor keeps the same distance from the corner in a bigger window.
        engine.call("ui_screen_size", width=1920, height=1080)
        expect("top right at 1920x1080", rect(engine, "demo_progress"), [1600, 20, 300, 20])

        # Designed for 1280x720, shown in 1920x1080: everything is 1.5 times bigger, and the
        # anchor still holds the 20 unit margin from the corner.
        reply = engine.call("ui_reference_size", width=1280, height=720)
        expect("scale", [reply["scale"]], [1.5])
        expect("scaled", rect(engine, "demo_progress"), [1920 - 30 - 450, 30, 450, 30])

        # A window with a different shape fits the reference size by its tighter side.
        reply = engine.call("ui_screen_size", width=1280, height=1440)
        expect("scale at 1280x1440", [reply["scale"]], [1.0])
        reply = engine.call("ui_screen_size", width=2560, height=720)
        expect("scale at 2560x720", [reply["scale"]], [1.0])
        expect("right edge at 2560x720", rect(engine, "demo_progress"), [2240, 20, 300, 20])

        # Scaling off again.
        engine.call("ui_reference_size", width=0, height=0)
        engine.call("ui_screen_size", width=1280, height=720)
        expect("scaling off", rect(engine, "demo_progress"), [960, 20, 300, 20])

        # A click goes through the scaled UI: the slider moves to its middle at any scale.
        engine.call("ui_reference_size", width=640, height=360)
        engine.call("ui_value_set", control="demo_slider", value=0)
        engine.call("ui_click", control="demo_slider")
        slider = [c for c in engine.call("ui_controls")["controls"] if c["name"] == "demo_slider"][0]
        if slider["value"] != 5:
            failures.append(f"click at scale 2: slider value {slider['value']}, wanted 5")
    finally:
        code = engine.close()

    if code != 0:
        failures.append(f"exit code {code}")
    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("PASS: anchors follow the window and the UI scales to the reference size")
    return 0


if __name__ == "__main__":
    sys.exit(main())
