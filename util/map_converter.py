#!/usr/bin/env python3

import os
import sys
import shutil
import subprocess

LVGL_IMAGE = "/home/pi/lvglMreDemo1/lvgl/scripts/LVGLImage.py"


def convert(rootdir):

    count = 0

    for subdir, _, files in os.walk(rootdir):

        for f in files:

            ext = os.path.splitext(f)[1].lower()

            if ext not in (
                ".png",
                ".jpg",
                ".jpeg",
                ".bmp",
                ".webp",
            ):
                continue

            src = os.path.join(subdir, f)

            rel_dir = os.path.relpath(subdir, rootdir)

            dst_dir = os.path.join(
                rootdir + "_lvgl",
                rel_dir
            )

            os.makedirs(dst_dir, exist_ok=True)

            base = os.path.splitext(f)[0]

            tmp_dir = os.path.join(
                dst_dir,
                "__tmp__"
            )

            os.makedirs(tmp_dir, exist_ok=True)

            print("Converting:", src)

            ret = subprocess.run(
                [
                    "python3",
                    LVGL_IMAGE,
                    src,
                    "--ofmt",
                    "BIN",
                    "--cf",
                    "RGB565",
                    "-o",
                    tmp_dir,
                ]
            )

            if ret.returncode != 0:
                print("FAILED:", src)
                shutil.rmtree(tmp_dir, ignore_errors=True)
                continue

            generated = os.path.join(
                tmp_dir,
                base + ".bin"
            )

            final = os.path.join(
                dst_dir,
                base + ".bin"
            )

            if not os.path.isfile(generated):
                print("Missing:", generated)
                shutil.rmtree(tmp_dir, ignore_errors=True)
                continue

            shutil.move(generated, final)

            shutil.rmtree(tmp_dir, ignore_errors=True)

            count += 1

    print()
    print("Converted {} files".format(count))


if len(sys.argv) != 2:
    print("Usage:")
    print("    {} <tile_directory>".format(sys.argv[0]))
    sys.exit(1)

convert(sys.argv[1])

