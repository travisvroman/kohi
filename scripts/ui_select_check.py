#!/usr/bin/env python3
"""Checks the list and dropdown over the agent channel.

Run from the repository root after a build:
    python3 scripts/ui_select_check.py

The headless testbed has a list of four maps and a dropdown of three levels. Choosing a level sets
the slider to 0, 5 or 10.
"""
import os
import sys

from agent_demo import BIN, CONFIG, Engine


def controls(engine):
    return {c["name"]: c for c in engine.call("ui_controls")["controls"]}


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "ui_select_check.log"))
    failures = []

    def expect(what, actual, wanted):
        if actual != wanted:
            failures.append(f"{what}: got {actual!r}, wanted {wanted!r}")

    try:
        engine.call("ui_screen_size", width=1280, height=720)
        engine.call("step", count=2)
        c = controls(engine)
        expect("list type", c["demo_list"]["type"], "list")
        expect("list items", c["demo_list"]["items"], ["Lowfield", "Ashen Keep", "The Tower", "Sunken Gate"])
        expect("list starts empty", c["demo_list"]["selected"], -1)
        expect("dropdown type", c["demo_dropdown"]["type"], "dropdown")
        expect("dropdown items", c["demo_dropdown"]["items"], ["Easy", "Normal", "Hard"])
        expect("dropdown start", c["demo_dropdown"]["selected"], 1)
        expect("dropdown starts closed", c["demo_dropdown"]["open"], False)

        expect("list select", engine.call("ui_select", control="demo_list", index=2)["selected"], 2)
        expect("list after select", controls(engine)["demo_list"]["selected"], 2)
        expect("list deselect", engine.call("ui_select", control="demo_list", index=-1)["selected"], -1)

        # Choosing a level drives the slider and the progress bar.
        engine.call("ui_select", control="demo_dropdown", index=2)
        c = controls(engine)
        expect("dropdown after select", c["demo_dropdown"]["selected"], 2)
        expect("slider follows dropdown", c["demo_slider"]["value"], 10)
        expect("progress follows slider", c["demo_progress"]["value"], 1)

        # Clicking the header opens it; clicking an item chooses it and closes it.
        engine.call("ui_click", control="demo_dropdown")
        expect("open after header click", controls(engine)["demo_dropdown"]["open"], True)
        engine.call("ui_click", control="demo_dropdown")
        expect("closed after second click", controls(engine)["demo_dropdown"]["open"], False)

        # Keyboard: accept opens, down moves, accept confirms.
        engine.call("ui_select", control="demo_dropdown", index=0)
        for _ in range(6):
            if engine.call("ui_navigate", action="next").get("focused") == "demo_dropdown":
                break
        else:
            failures.append("tab never reached the dropdown")
        for action, open_wanted in (("accept", True), ("down", True), ("accept", False)):
            engine.call("ui_navigate", action=action)
            expect(f"open after {action}", controls(engine)["demo_dropdown"]["open"], open_wanted)
        expect("keyboard choice", controls(engine)["demo_dropdown"]["selected"], 1)

        # Bad requests are refused.
        for bad in ({"control": "demo_list", "index": 9}, {"control": "demo_slider", "index": 0}):
            try:
                engine.call("ui_select", **bad)
                failures.append(f"accepted {bad}")
            except RuntimeError:
                pass
    finally:
        code = engine.close()

    expect("exit code", code, 0)
    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("PASS: list and dropdown select, open, close and drive the slider")
    return 0


if __name__ == "__main__":
    sys.exit(main())
