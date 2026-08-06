#!/usr/bin/env python3
"""
Download all the map tiles for a particular zoom level from OpenStreetMap,
and save them into individual files using the standard z/x/y folder structure.
"""

import io
import itertools
import os

import httpx
from PIL import Image

# Configuration matching the second script's style
map_style = "map"  # Use "map" or "satellite"
zoom_level = 4 # 0; 1; 2; 3; ...  

# Base directory matching the second script
base_dir = f"tiles/{map_style}"

for x, y in itertools.product(range(2**zoom_level), range(2**zoom_level)):
    # Establish the precise path matching: tiles/{map_style}/{z}/{x}/{y}.png
    tile_dir = os.path.join(base_dir, str(zoom_level), str(x))
    out_path = os.path.join(tile_dir, f"{y}.png")

    # Create the necessary subdirectories if they do not exist
    os.makedirs(tile_dir, exist_ok=True)

    # Fetch the specific tile
    resp = httpx.get(
        f"https://tile.openstreetmap.org/{zoom_level}/{x}/{y}.png", timeout=50
    )
    resp.raise_for_status()

    # Open the image from the downloaded bytes
    im_buffer = Image.open(io.BytesIO(resp.content))

    # Save to its dedicated location
    im_buffer.save(out_path)
    print(f"Saved: {out_path}")

print(f"\nAll tiles saved successfully into the '{base_dir}' folder structure!")

