#!/usr/bin/env python3

import os
import re
import httpx

TREE_FILE = "map_tree.txt"
OUT_DIR = "tiles"

tile_re = re.compile(
    r"(\d+)\s*\.png$"
)

current_z = None
current_x = None

with open(TREE_FILE, "r", encoding="utf-8") as f:
    for line in f:

        line = line.strip()

        # zoom directory
        m = re.search(r"├── (\d+)$|└── (\d+)$", line)
        if m and len(line.split("│")) <= 2:
            current_z = int(m.group(1) or m.group(2))
            continue

        # x directory
        m = re.search(r"├── (\d+)$|└── (\d+)$", line)
        if m and current_z is not None:
            parts = line.split("│")
            if len(parts) >= 3:
                current_x = int(m.group(1) or m.group(2))
            continue

        # y.png
        m = re.search(r"(\d+)\.png", line)
        if not m or current_z is None or current_x is None:
            continue

        y = int(m.group(1))

        url = (
            f"https://tile.openstreetmap.org/"
            f"{current_z}/{current_x}/{y}.png"
        )

        out_dir = os.path.join(
            OUT_DIR,
            "map",
            str(current_z),
            str(current_x),
        )

        os.makedirs(out_dir, exist_ok=True)

        out_file = os.path.join(
            out_dir,
            f"{y}.png"
        )

        print("Downloading", url)

        r = httpx.get(url, timeout=30)
        r.raise_for_status()

        with open(out_file, "wb") as fp:
            fp.write(r.content)

        print("Saved:", out_file)

