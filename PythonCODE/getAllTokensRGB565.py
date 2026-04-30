#!/usr/bin/env python3
"""
convert_to_bin.py
Converts all .png token images to raw big-endian RGB565 .bin files
ready for direct streaming to a TFT display via TFT_eSPI pushImage().

Usage:
    python3 convert_to_bin.py                         # convert tokens/ in place
    python3 convert_to_bin.py --src tokens --dst bins # separate output folder
    python3 convert_to_bin.py --delete-png            # remove PNGs after converting

Each .bin is exactly 320 * 480 * 2 = 307,200 bytes.
Pixel format: big-endian RGB565  (matches PNG_RGB565_BIG_ENDIAN used in Arduino).
"""

import os
import sys
import struct
import argparse
from PIL import Image

# ── Display resolution ────────────────────────────────────────────────────────
WIDTH  = 320
HEIGHT = 480
BYTES_PER_FILE = WIDTH * HEIGHT * 2   # 307,200

def rgb_to_rgb565_be(r, g, b):
    """Pack R8G8B8 → big-endian RGB565 bytes (2 bytes, high byte first)."""
    word = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return struct.pack(">H", word)   # big-endian, matches PNG_RGB565_BIG_ENDIAN

def convert_file(src_path, dst_path):
    try:
        img = Image.open(src_path).convert("RGB")
        if img.size != (WIDTH, HEIGHT):
            img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
        pixels = img.load()
        with open(dst_path, "wb") as f:
            for y in range(HEIGHT):
                row = b"".join(rgb_to_rgb565_be(*pixels[x, y]) for x in range(WIDTH))
                f.write(row)
        return True
    except Exception as e:
        print(f"  ERROR: {src_path}: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Convert PNG tokens to RGB565 bin files")
    parser.add_argument("--src",        default="tokens", help="Source folder containing PNGs")
    parser.add_argument("--dst",        default=None,     help="Output folder (default: same as src)")
    parser.add_argument("--delete-png", action="store_true", help="Delete source PNG after converting")
    args = parser.parse_args()

    src_dir = args.src
    dst_dir = args.dst if args.dst else src_dir

    if not os.path.isdir(src_dir):
        print(f"Source folder not found: {src_dir}")
        sys.exit(1)
    os.makedirs(dst_dir, exist_ok=True)

    pngs = [f for f in os.listdir(src_dir) if f.lower().endswith(".png")]
    if not pngs:
        print(f"No PNG files found in {src_dir}")
        sys.exit(0)

    print(f"Converting {len(pngs)} PNG files  ({src_dir} → {dst_dir})")
    print(f"Target: {WIDTH}×{HEIGHT}, big-endian RGB565, {BYTES_PER_FILE:,} bytes each\n")

    ok = fail = skip = 0
    for i, fname in enumerate(sorted(pngs), 1):
        src = os.path.join(src_dir, fname)
        dst = os.path.join(dst_dir, os.path.splitext(fname)[0] + ".bin")

        if os.path.exists(dst):
            # Skip if already converted and source hasn't changed
            if os.path.getmtime(dst) >= os.path.getmtime(src):
                skip += 1
                continue

        sys.stdout.write(f"\r[{i}/{len(pngs)}] {fname[:50]:<50}")
        sys.stdout.flush()

        if convert_file(src, dst):
            ok += 1
            if args.delete_png:
                os.remove(src)
        else:
            fail += 1

    print(f"\n\nDone.  converted={ok}  skipped={skip}  failed={fail}")
    print(f"Total bin size: {(ok + skip) * BYTES_PER_FILE / 1_048_576:.1f} MB")

if __name__ == "__main__":
    main()