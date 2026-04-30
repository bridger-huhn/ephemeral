"""
generate_index.py
Run this once after adding/removing images from the SD card.
Copy the resulting index.csv to the ROOT of the SD card.

Usage:
    python generate_index.py /path/to/sd/card/folder
    python generate_index.py .   (if already in the SD card folder)

Handles two filename formats produced by the download script:

  Creature tokens  (have power/toughness):
    name_power_tough_color_version.png
    e.g. octopus_8_8_u_1.png

  Non-creature tokens (no power/toughness, pt="token"):
    name_token_color_version.png
    e.g. treasure_token_c_89.png
         blood_token_c_8.png
"""

import os
import sys


def parse_filename(fname):
    """
    Returns (name, power, tough, color, version) or None if unrecognised.
    power/tough are the string "token" for non-creature tokens.
    """
    if not fname.lower().endswith(".bin"):
        return None

    base = fname[:-4]           # strip .png
    parts = base.split("_")

    # Need at least 4 parts: name + token/pt + color + version
    if len(parts) < 4:
        return None

    version = parts[-1]
    color   = parts[-2]

    # Non-creature token: third-from-last segment is literally "token"
    #   e.g. ["treasure", "token", "c", "89"]
    #   e.g. ["blood",    "token", "c", "8"]
    #   e.g. ["clue",     "token", "c", "3"]
    if parts[-3] == "token":
        name  = "_".join(parts[:-3])  # everything before "token"
        power = "token"
        tough = "token"

    # Creature token: needs at least 5 parts  name + power + tough + color + version
    #   e.g. ["octopus", "8", "8", "u", "1"]
    elif len(parts) >= 5:
        tough = parts[-3]
        power = parts[-4]
        name  = "_".join(parts[:-4])

    else:
        return None

    if not name:
        return None

    return name, power, tough, color, version


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    folder = os.path.abspath(folder)
    out_path = os.path.join(folder, "index.csv")

    entries  = []
    skipped  = 0
    tokens   = 0   # non-creature tokens
    creatures = 0  # creature tokens

    for fname in sorted(os.listdir(folder)):
        if not fname.lower().endswith(".bin"):
            continue

        result = parse_filename(fname)

        if result is None:
            print(f"  SKIP (bad format): {fname}")
            skipped += 1
            continue

        name, power, tough, color, version = result
        entries.append(f"{fname},{name},{power},{tough},{color},{version}")

        if power == "token":
            tokens += 1
        else:
            creatures += 1

    with open(out_path, "w") as f:
        f.write("\n".join(entries))
        f.write("\n")

    print(f"Written {len(entries)} entries to {out_path}")
    print(f"  Creature tokens : {creatures}")
    print(f"  Non-creature    : {tokens}")
    if skipped:
        print(f"  Skipped         : {skipped}  (unexpected filename format)")


if __name__ == "__main__":
    main()
