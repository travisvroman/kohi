#!/usr/bin/env python3
"""Checks keyboard-style UI navigation over the agent channel.

Run from the repository root after a build:
    python3 scripts/ui_navigation_check.py

The headless testbed has, top to bottom at the right of the window: a slider (0 to 10, step 1,
starts at 5), a Reset button that puts the slider back to 5, and a Sound checkbox.
"""
import os
import sys

from agent_demo import BIN, CONFIG, Engine


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "ui_navigation_check.log"))
    failures = []

    def expect(what, actual, wanted):
        if actual != wanted:
            failures.append(f"{what}: got {actual!r}, wanted {wanted!r}")

    def navigate(action):
        return engine.call("ui_navigate", action=action)

    def control(name):
        return {c["name"]: c for c in engine.call("ui_controls")["controls"]}[name]

    def focused():
        return [c["name"] for c in engine.call("ui_controls")["controls"] if c.get("focused")]

    def press(key, shift=False):
        if shift:
            engine.call("key", key="shift", pressed=True)
        engine.call("key", key=key, pressed=True)
        engine.call("key", key=key, pressed=False)
        if shift:
            engine.call("key", key="shift", pressed=False)
        engine.call("step", count=1)

    try:
        engine.call("step", count=2)
        # The null renderer has no window to resize, so tell the UI the screen size to place the controls.
        engine.call("ui_screen_size", width=1280, height=720)

        # With nothing focused, arrows are left to the game; accept and cancel do nothing either.
        expect("arrow with no focus", navigate("down")["used"], True)  # the API focuses the first control
        expect("first focus", navigate("cancel").get("focused"), None)
        expect("cancel with no focus", navigate("cancel")["used"], False)
        expect("accept with no focus", navigate("accept")["used"], False)

        # Tab order is reading order: slider, button, checkbox, dropdown, list, then around again.
        order = [navigate("next")["focused"] for _ in range(6)]
        expect("next order", order, ["demo_slider", "demo_button", "demo_checkbox", "demo_dropdown", "demo_list", "demo_slider"])
        expect("prev wraps", navigate("prev")["focused"], "demo_list")
        navigate("prev")
        expect("back to checkbox", navigate("prev")["focused"], "demo_checkbox")

        # Directions go to the nearest control that way and stop at the edge.
        expect("up", navigate("up")["focused"], "demo_button")
        expect("up again", navigate("up")["focused"], "demo_slider")
        expect("up at the edge is used up", navigate("up")["used"], True)
        expect("still on slider", navigate("up")["focused"], "demo_slider")
        # Down goes to the nearest control in line with it: the dropdown sits under the slider, the narrower button does not.
        expect("down", navigate("down")["focused"], "demo_dropdown")

        # Left and right on a focused slider change its value instead of moving focus.
        for _ in range(3):
            navigate("prev")
        navigate("right")
        navigate("right")
        expect("slider after two rights", control("demo_slider")["value"], 7)
        expect("focus stays", navigate("left")["focused"], "demo_slider")
        expect("slider after a left", control("demo_slider")["value"], 6)

        # Accept presses the focused control: the button resets the slider, the checkbox toggles.
        navigate("next")
        navigate("accept")
        expect("reset by accept", control("demo_slider")["value"], 5)
        expect("focus kept after accept", control("demo_button")["focused"], True)
        navigate("next")
        before = control("demo_checkbox")["checked"]
        navigate("accept")
        expect("checkbox toggled", control("demo_checkbox")["checked"], not before)

        # Cancel drops focus.
        navigate("cancel")
        expect("focus dropped", focused(), [])

        # The same through the keyboard. Arrow keys with nothing focused are left to the game.
        press("down")
        expect("arrow with nothing focused", focused(), [])
        press("tab")
        expect("tab focuses the first control", focused(), ["demo_slider"])
        press("down")
        expect("down arrow", focused(), ["demo_dropdown"])
        for _ in range(2):
            press("tab")
        expect("tab wraps", focused(), ["demo_slider"])
        press("tab", shift=True)
        expect("shift-tab wraps backwards", focused(), ["demo_list"])
        press("tab", shift=True)
        press("tab", shift=True)
        expect("shift-tab goes back", focused(), ["demo_checkbox"])
        press("space")
        expect("space presses", control("demo_checkbox")["checked"], before)
        press("esc")
        expect("escape drops focus", focused(), [])
    finally:
        code = engine.close()

    expect("exit code", code, 0)
    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("PASS: focus moves by reading order and direction, and accept and cancel work")
    return 0


if __name__ == "__main__":
    sys.exit(main())
