#!/usr/bin/env python3
"""Move every parameter and fail if any of them made no difference to the frame.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op --
so a slider can be stone dead while everything compiles, links and renders.

Each render is one throw to rest (ditest's default: Roll on frame 0, then the
frames to Roll Time), so the controls that act on the THROW -- Roll Time, Throw,
Spin, Bounce, Seed -- show up as dice resting somewhere else.

## The context table

Some controls only act when something else is true, and the table supplies
exactly that as harness arguments. Checked by emptying the whole table
(2026-10-04): Fixed Total, Interval, Line Width, Texture File, D6 Faces and the
three Table Colour channels go dead without theirs. The other entries make the
change the one the control is FOR rather than luck of the seed: a 9 on top for
the 6/9 mark, a close-up for the engraving and the weight, a second roll for
Auto Roll.

- Fixed Total: only in Result Fixed.
- Interval and Auto Roll: only over a run long enough for a second roll.
- Line Width: only in the Wireframe texture. Texture File: only in Image.
- Mark 6 and 9: only with a 6 or a 9 in sight (a d20 resting on 9).
- D6 Faces: only on a d6. Table Colour: only with a table drawn.
- Font File and Font Name: a font that is on this Mac.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N]
"""

import argparse
import concurrent.futures
import os
import pathlib
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

SIZE = "320x180"

FONT_FILE = "/System/Library/Fonts/Supplemental/Georgia.ttf"

CONTEXT = {
    "Fixed Total": ["--set", "Result=1"],
    "Auto Roll": ["--frames", "420", "--set", "Interval=0.2"],
    "Interval": ["--frames", "420", "--set", "Auto Roll=1"],
    "Line Width": ["--set", "Texture=9"],
    "Texture File": ["--set", "Texture=8"],
    "Mark 6 and 9": ["--set", "Result=1", "--set", "Fixed Total=9", "--set", "Size=0.8"],
    "D6 Faces": ["--set", "Die=1", "--set", "Size=0.8"],
    "Table Colour": ["--set", "Table=1"],
    "Table_Green": ["--set", "Table=1"],
    "Table_Blue": ["--set", "Table=1"],
    "Number Style": ["--set", "Size=0.8"],
    "Weight": ["--set", "Size=0.8"],
}

# The awkward values are load-bearing: an angle swept at 0, 0.5 and 1 can land
# on the same picture.
SWEEP_VALUES = [0.0, 0.137, 0.611, 1.0]

# Discrete parameters, by the values they take (--list reports the kind but
# not the element count, so these track Controls.h by hand).
DISCRETE = {
    "Die": [0, 1, 3, 5, 6],
    "Count": [1, 2, 4],
    "Result": [0, 1],
    "Fixed Total": [3, 11, 20],
    "Seed": [0, 1, 2],
    "Auto Roll": [0, 1],
    "Throw": [0, 1, 2, 3, 4, 5],
    "Texture": [0, 1, 2, 3, 4, 5, 6, 7, 9],
    "Number Style": [0, 1],
    "Mark 6 and 9": [0, 1, 2],
    "D6 Faces": [0, 1],
    "Table": [0, 1, 2],
    "Font": [0, 1, 40],
}

# Text and file controls, by the strings they take.
TEXTS = {
    "Texture File": ["", str(REPO / "docs" / "sweep-texture.png")],
    "Font File": ["", FONT_FILE],
    "Font Name": ["", "Georgia"],
}

SKIP_KINDS = {"buffer", "event"}
SKIP_NAMES = {"About"}
OVER_ONLY = {"Mix"}


def read_png(path):
    data = path.read_bytes()
    pos, idat = 8, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        if data[pos + 4:pos + 8] == b"IDAT":
            idat += data[pos + 8:pos + 8 + length]
        pos += 12 + length
    return zlib.decompress(idat)


def parameters(harness, over):
    args = [str(harness), "--list"] + (["--over"] if over else [])
    out = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    found = []
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 4:
            found.append((" ".join(parts[1:-2]), parts[-2]))
    return found


def render(harness, tmp, over, setting, extra, index):
    out = tmp / f"sweep-{index}.png"
    args = [str(harness), "--out", str(out), "--size", SIZE]
    args += (["--over"] if over else []) + extra + ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"ditest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def write_texture_fixture(path):
    """A 64x64 checker for Texture File: made here, so the repo carries no binary."""
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    width = height = 64
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        for x in range(width):
            on = ((x // 8) + (y // 8)) % 2 == 0
            raw += bytes((230, 40, 40, 255) if on else (30, 30, 200, 255))

    def chunk(kind, body):
        crc = zlib.crc32(kind + body) & 0xFFFFFFFF
        return len(body).to_bytes(4, "big") + kind + body + crc.to_bytes(4, "big")

    header = width.to_bytes(4, "big") + height.to_bytes(4, "big") + bytes((8, 6, 0, 0, 0))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--jobs", type=int, default=6)
    args = parser.parse_args()

    harness = REPO / args.build / "ditest"
    if not harness.exists():
        print(f"no ditest at {harness} -- build first", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as fixtures:
        texture = pathlib.Path(fixtures) / "sweep-texture.png"
        write_texture_fixture(texture)
        TEXTS["Texture File"] = ["", str(texture)]
        if not os.path.exists(FONT_FILE):
            print(f"  note: {FONT_FILE} is not on this machine; Font File is swept with no file only")
            TEXTS["Font File"] = [""]

        work = []
        for name, kind in parameters(harness, False):
            if kind in SKIP_KINDS or name in SKIP_NAMES:
                continue
            work.append((False, name, kind))
        for name, kind in parameters(harness, True):
            if name in OVER_ONLY:
                work.append((True, name, kind))
        # The effect's own texture: the clip on the faces.
        work.append((True, "Texture", "option-over"))

        dead, checked = [], 0
        with tempfile.TemporaryDirectory() as tmp, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            tmp = pathlib.Path(tmp)
            jobs = {}
            index = 0
            for over, name, kind in work:
                if kind == "option-over":
                    values = [9, 10]
                elif name in TEXTS:
                    values = TEXTS[name]
                else:
                    values = DISCRETE.get(name, SWEEP_VALUES)
                futures = []
                for value in values:
                    futures.append(pool.submit(render, harness, tmp, over, f"{name}={value}", CONTEXT.get(name, []), index))
                    index += 1
                jobs[(over, name, kind)] = futures
            for (over, name, kind), futures in jobs.items():
                frames = [f.result() for f in futures]
                if len(frames) < 2:
                    continue
                checked += 1
                label = f"{name}{' (Over)' if over else ''}"
                if all(f == frames[0] for f in frames[1:]):
                    dead.append(label)
                    print(f"  DEAD {label}")
                elif args.verbose:
                    print(f"  ok   {label}")

    print()
    if dead:
        print(f"sweep: {checked} parameters, {len(dead)} made no difference:")
        for entry in dead:
            print(f"  - {entry}")
        return 1
    print(f"sweep: {checked} parameters, all live")
    return 0


if __name__ == "__main__":
    sys.exit(main())
