#!/usr/bin/env python3

import os
import re
import httpx

TREE_FILE = "map_tree.txt"
OUT_DIR = "tiles"

current_z = None
current_x = None

with open(TREE_FILE, "r", encoding="utf-8") as f:
    for line in f:

        # Keep indentation, remove only newline
        line = line.rstrip("\n")

        # Extract name after ├── or └──
        m = re.search(r"[├└]──\s+(.+)$", line)
        if not m:
            continue

        name = m.group(1).strip()

        # Determine tree depth from indentation
        indent = line.find("├──")
        if indent < 0:
            indent = line.find("└──")

        # Zoom directory
        # Example: │   └── 9
        if indent == 4 and name.isdigit():
            current_z = int(name)
            current_x = None
            print(f"Zoom: {current_z}")
            continue

        # X directory
        # Example: │       ├── 290
        if indent == 8 and name.isdigit():
            current_x = int(name)
            continue

        # Tile file
        # Example: │       │   ├── 161.png
        if (
            indent == 12
            and name.endswith(".png")
            and current_z is not None
            and current_x is not None
        ):
            y = int(name[:-4])

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

            if os.path.exists(out_file):
                print("Skipping:", out_file)
                continue

            print(
                f"Downloading z={current_z} "
                f"x={current_x} y={y}"
            )

            try:
                r = httpx.get(url, timeout=30)
                r.raise_for_status()

                with open(out_file, "wb") as fp:
                    fp.write(r.content)

                print("Saved:", out_file)

            except Exception as e:
                print(
                    f"Failed: z={current_z} "
                    f"x={current_x} y={y} ({e})"
                )

print("Done.")
