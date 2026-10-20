#!/usr/bin/env python3
"""Checks the slider and progress bar over the agent channel.

Run from the repository root after a build:
    python3 scripts/ui_widgets_check.py

The headless testbed has a slider (0 to 10, step 1, starts at 5) whose callback drives a progress
bar (value / 10).
"""
import sys

from agent_demo import BIN, CONFIG, Engine
import os


def controls(engine):
    return {c["name"]: c for c in engine.call("ui_controls")["controls"]}


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "ui_widgets_check.log"))
    failures = []

    def expect(what, actual, wanted):
        if actual != wanted:
            failures.append(f"{what}: got {actual!r}, wanted {wanted!r}")

    try:
        engine.call("step", count=2)
        c = controls(engine)
        expect("slider type", c["demo_slider"]["type"], "slider")
        expect("progress type", c["demo_progress"]["type"], "progress_bar")
        expect("slider start", c["demo_slider"]["value"], 5)
        expect("progress start", c["demo_progress"]["value"], 0.5)

        # Values snap to the step, and the callback moves the progress bar.
        expect("reply", engine.call("ui_value_set", control="demo_slider", value=7.4)["value"], 7)
        c = controls(engine)
        expect("slider after set", c["demo_slider"]["value"], 7)
        expect("progress follows slider", round(c["demo_progress"]["value"], 3), 0.7)

        # Out of range clamps.
        expect("clamp high", engine.call("ui_value_set", control="demo_slider", value=99)["value"], 10)
        expect("clamp low", engine.call("ui_value_set", control="demo_slider", value=-3)["value"], 0)
        expect("progress clamps", engine.call("ui_value_set", control="demo_progress", value=2)["value"], 1)

        # A click in the middle of the bar picks the middle value.
        engine.call("ui_click", control="demo_slider")
        expect("slider after click", controls(engine)["demo_slider"]["value"], 5)

        # Other controls have no value.
        try:
            engine.call("ui_value_set", control="testbed_mono_test_text", value=1)
            failures.append("a label accepted a value")
        except RuntimeError:
            pass
    finally:
        code = engine.close()

    expect("exit code", code, 0)
    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("PASS: slider snaps, clamps, clicks and drives the progress bar")
    return 0


if __name__ == "__main__":
    sys.exit(main())
