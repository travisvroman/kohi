#!/usr/bin/env python3
"""Checks grid pathfinding over the agent channel.

Run from the repository root after a build:
    python3 scripts/nav_path_check.py

The headless testbed has a 20 x 20 navigation grid with a wall in column 10 from row 0 to row 17,
so the only way from the left half to the right half is through rows 18 and 19.
"""
import os
import sys

from agent_demo import BIN, CONFIG, Engine


def path(engine, from_cell, to_cell, **options):
    reply = engine.call("nav_path", from_x=from_cell[0], from_y=from_cell[1], to_x=to_cell[0], to_y=to_cell[1], **options)
    return [tuple(cell) for cell in reply["cells"]]


def main():
    engine = Engine(CONFIG, os.path.join(BIN, "nav_path_check.log"))
    failures = []

    def expect(what, actual, wanted):
        if actual != wanted:
            failures.append(f"{what}: got {actual!r}, wanted {wanted!r}")

    try:
        engine.call("step", count=1)

        # Open ground: a diagonal then a straight run, and the same answer twice.
        first = path(engine, (0, 0), (5, 3))
        expect("open path length", len(first), 6)
        expect("starts and ends", (first[0], first[-1]), ((0, 0), (5, 3)))
        expect("same path again", path(engine, (0, 0), (5, 3)), first)

        # Across the wall the path has to use the gap.
        across = path(engine, (2, 2), (18, 2))
        expect("path ends at the goal", across[-1], (18, 2))
        expect("path crosses in the gap", any(x == 10 and y >= 18 for x, y in across), True)
        expect("path never enters the wall", any(x == 10 and y < 18 for x, y in across), False)

        # Without diagonals every step is along an axis.
        straight = path(engine, (0, 0), (3, 3), diagonal=False)
        expect("no-diagonal length", len(straight), 7)

        # Close the gap and there is no path.
        engine.call("nav_block", x=10, y=18, w=1, h=2)
        try:
            path(engine, (2, 2), (18, 2))
            failures.append("found a path through a closed wall")
        except RuntimeError:
            pass

        # Reopen it and the path is back.
        engine.call("nav_block", x=10, y=18, w=1, h=2, blocked=False)
        expect("reopened", path(engine, (2, 2), (18, 2)), across)

        # A blocked end is refused.
        try:
            path(engine, (2, 2), (10, 5))
            failures.append("accepted a blocked goal")
        except RuntimeError:
            pass
    finally:
        code = engine.close()

    expect("exit code", code, 0)
    for f in failures:
        print("FAIL:", f)
    if failures:
        return 1
    print("PASS: paths are found around walls, are repeatable, and follow edits to the grid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
