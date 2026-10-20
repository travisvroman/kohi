#!/usr/bin/env python3
"""Captures a frame from the headless testbed, rendered by Vulkan with no display.

Run from the repository root after a build:
    python3 scripts/frame_capture_demo.py

It starts the testbed with the Vulkan renderer on a headless surface, steps the physics scene,
saves a PNG with the "screenshot" agent command and checks the file: a valid PNG of the window's
size, with more than one colour in it (a frame that was never drawn is a single flat colour).

Needs a Vulkan driver. On a machine with no GPU, install Mesa's software driver (lavapipe), for
example `apt install mesa-vulkan-drivers`. The script uses lavapipe when it finds its driver file
and VK_ICD_FILENAMES is not already set. Pixels differ between drivers, so only the structure is
checked, never exact values.
"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # python -I leaves it out
from agent_demo import BIN, REPO, Engine  # noqa: E402

CONFIG = os.path.join(REPO, "testbed.kapp", "app_config_headless_vulkan.kson")
LAVAPIPE_ICD = "/usr/share/vulkan/icd.d/lvp_icd.json"
OUTPUT = os.path.join(BIN, "frame_capture_demo.png")


def png_read(path):
    """Decodes an 8-bit RGBA PNG without filtering tricks beyond what the engine writes. Returns (width, height, rows)."""
    with open(path, "rb") as file:
        data = file.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    width = height = 0
    idat = b""
    offset = 8
    while offset < len(data):
        (length,) = struct.unpack(">I", data[offset:offset + 4])
        kind = data[offset + 4:offset + 8]
        body = data[offset + 8:offset + 8 + length]
        (crc,) = struct.unpack(">I", data[offset + 8 + length:offset + 12 + length])
        if zlib.crc32(kind + body) != crc:
            raise ValueError(f"bad checksum in {kind.decode()} chunk")
        if kind == b"IHDR":
            width, height, depth, colour_type = struct.unpack(">IIBB", body[:10])
            if (depth, colour_type) != (8, 6):
                raise ValueError("expected 8-bit RGBA")
        elif kind == b"IDAT":
            idat += body
        offset += 12 + length
    raw = zlib.decompress(idat)
    stride = 1 + width * 4
    if len(raw) != stride * height:
        raise ValueError("image data has the wrong size")
    rows = []
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        if row[0] != 0:
            raise ValueError("unexpected row filter")
        rows.append(row[1:])
    return width, height, rows


def main():
    env = {}
    if "VK_ICD_FILENAMES" not in os.environ and os.path.exists(LAVAPIPE_ICD):
        env["VK_ICD_FILENAMES"] = LAVAPIPE_ICD

    engine = Engine(CONFIG, os.path.join(BIN, "frame_capture_demo.log"), env=env)
    try:
        engine.call("step", count=30)
        reply = engine.call("screenshot", path=OUTPUT)
        print(f"Saved {reply['width']}x{reply['height']} frame from step {reply['step']} to {reply['path']}")
    finally:
        code = engine.close()

    width, height, rows = png_read(OUTPUT)
    if (width, height) != (reply["width"], reply["height"]):
        print("FAIL: the PNG size differs from the reply")
        return 1
    colours = {row[i:i + 4] for row in rows for i in range(0, len(row), 4)}
    print(f"The frame has {len(colours)} distinct colours.")
    if len(colours) < 2:
        print("FAIL: the frame is a single flat colour, so nothing was drawn")
        return 1
    if code != 0:
        print(f"FAIL: the engine exited with code {code}")
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
