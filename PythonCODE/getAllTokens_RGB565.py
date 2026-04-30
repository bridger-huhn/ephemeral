#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
getAllTokens_RGB565.py

Downloads every token from Scryfall, resizes to 320×480, and saves directly
as a raw big-endian RGB565 .bin file — no intermediate PNG step.

Each .bin is exactly 320 × 480 × 2 = 307,200 bytes and can be streamed
straight to a TFT display via TFT_eSPI pushImage() with no decoding overhead.

Requirements:
    pip install aiohttp Pillow

Usage:
    python3 getAllTokens_RGB565.py
    python3 getAllTokens_RGB565.py --out /path/to/sd/tokens --workers 20
"""

import os
import re
import struct
import asyncio
import argparse
import sys
from io import BytesIO
from collections import defaultdict

import aiohttp
from PIL import Image

# ── Config ────────────────────────────────────────────────────────────────────
WIDTH   = 320
HEIGHT  = 480
BYTES_PER_FILE = WIDTH * HEIGHT * 2   # 307,200

# ── Helpers ───────────────────────────────────────────────────────────────────
def clean(text):
    if not text:
        return "token"
    text = re.sub(r"[^\w\s-]", "", text)
    return text.replace(" ", "_").lower()

def color_code(colors):
    return "".join(colors).lower() if colors else "c"

def build_base_name(card):
    name   = clean(card.get("name"))
    power  = card.get("power")
    toughness = card.get("toughness")
    pt     = f"{clean(power)}_{clean(toughness)}" if power and toughness else "token"
    colors = color_code(card.get("colors"))
    return f"{name}_{pt}_{colors}"

def get_image_url(card):
    """Prefer border_crop (tight JPEG), fall back to art_crop then png."""
    for fmt in ("border_crop", "art_crop", "png"):
        if "image_uris" in card:
            url = card["image_uris"].get(fmt)
            if url:
                return url
        if "card_faces" in card:
            for face in card["card_faces"]:
                if "image_uris" in face:
                    url = face["image_uris"].get(fmt)
                    if url:
                        return url
    return None

def img_to_rgb565_bin(img: Image.Image) -> bytes:
    """Convert a PIL Image (any size/mode) to big-endian RGB565 bytes."""
    img = img.convert("RGB")
    if img.size != (WIDTH, HEIGHT):
        img = img.resize((WIDTH, HEIGHT), Image.LANCZOS)
    pixels = img.load()
    buf = bytearray(WIDTH * HEIGHT * 2)
    idx = 0
    for y in range(HEIGHT):
        for x in range(WIDTH):
            r, g, b = pixels[x, y]
            word = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            buf[idx]     = (word >> 8) & 0xFF   # big-endian high byte
            buf[idx + 1] =  word       & 0xFF
            idx += 2
    return bytes(buf)

# ── Scryfall fetch ─────────────────────────────────────────────────────────────
async def fetch_json(session, url):
    async with session.get(url) as resp:
        resp.raise_for_status()
        return await resp.json()

async def get_all_tokens(session):
    url    = "https://api.scryfall.com/cards/search?q=t:token&unique=prints"
    tokens = []
    page   = 0
    while url:
        page += 1
        data = await fetch_json(session, url)
        tokens.extend(data["data"])
        url = data.get("next_page")
        sys.stdout.write(f"\r  Fetching card list... page {page}  ({len(tokens)} cards)")
        sys.stdout.flush()
    print()
    return tokens

# ── Download + convert worker ─────────────────────────────────────────────────
async def process_token(session, semaphore, card, out_dir, name_counts, counters):
    url = get_image_url(card)
    if not url:
        counters["skip"] += 1
        return

    base = build_base_name(card)
    name_counts[base] += 1
    filename = f"{base}_{name_counts[base]}.bin"
    path     = os.path.join(out_dir, filename)

    if os.path.exists(path) and os.path.getsize(path) == BYTES_PER_FILE:
        counters["skip"] += 1
        return

    async with semaphore:
        try:
            async with session.get(url) as resp:
                resp.raise_for_status()
                img_bytes = await resp.read()

            img     = Image.open(BytesIO(img_bytes))
            raw     = img_to_rgb565_bin(img)

            with open(path, "wb") as f:
                f.write(raw)

            counters["ok"] += 1

        except Exception as e:
            counters["fail"] += 1
            print(f"\n  FAILED {filename}: {e}")

    done = counters["ok"] + counters["fail"] + counters["skip"]
    total = counters["total"]
    pct = done / total * 100
    sys.stdout.write(
        f"\r  [{done}/{total}]  {pct:.1f}%  "
        f"ok={counters['ok']}  skip={counters['skip']}  fail={counters['fail']}   "
    )
    sys.stdout.flush()

# ── Main ──────────────────────────────────────────────────────────────────────
async def main(out_dir: str, workers: int):
    os.makedirs(out_dir, exist_ok=True)

    print(f"Output : {os.path.abspath(out_dir)}")
    print(f"Format : {WIDTH}×{HEIGHT} big-endian RGB565 ({BYTES_PER_FILE:,} bytes/file)")
    print(f"Workers: {workers}\n")

    timeout = aiohttp.ClientTimeout(total=60)
    async with aiohttp.ClientSession(timeout=timeout) as session:
        print("Step 1/2 — Fetching token list from Scryfall...")
        tokens = await get_all_tokens(session)
        print(f"  Found {len(tokens)} tokens\n")

        # name_counts must be a shared dict — count order matters for filenames
        name_counts = defaultdict(int)
        counters    = {"ok": 0, "fail": 0, "skip": 0, "total": len(tokens)}

        print("Step 2/2 — Downloading and converting...")
        semaphore = asyncio.Semaphore(workers)
        tasks = [
            process_token(session, semaphore, card, out_dir, name_counts, counters)
            for card in tokens
        ]
        await asyncio.gather(*tasks)

    print(f"\n\nDone.")
    print(f"  Converted : {counters['ok']}")
    print(f"  Skipped   : {counters['skip']}  (already existed)")
    print(f"  Failed    : {counters['fail']}")
    total_files = counters["ok"] + counters["skip"]
    print(f"  Total size: {total_files * BYTES_PER_FILE / 1_048_576:.1f} MB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Download all Scryfall tokens as raw RGB565 .bin files"
    )
    parser.add_argument(
        "--out", default="/Users/bridgerhuhn/Documents/Mtg-tokens/micropythonSD/tokens",
        help="Output directory for .bin files (default: tokens/)"
    )
    parser.add_argument(
        "--workers", type=int, default=20,
        help="Max concurrent downloads (default: 20)"
    )
    args = parser.parse_args()
    asyncio.run(main(args.out, args.workers))
