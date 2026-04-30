#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Wed Apr 22 20:14:33 2026

@author: bridgerhuhn
"""

import requests
import os
from PIL import Image
from io import BytesIO

os.chdir("/Users/bridgerhuhn/Documents/ephemeral")

#%%
import requests
import os
import re
import csv

def extract_deck_id(input_str):
    """
    Accepts either:
      - full URL: https://archidekt.com/decks/14326552/...
      - or just: 14326552

    Returns the numeric deck ID.
    """
    match = re.search(r"/decks/(\d+)", input_str)
    if match:
        return match.group(1)

    # fallback: assume it's already an ID
    if input_str.isdigit():
        return input_str

    raise ValueError("Invalid Archidekt URL or deck ID")

# --- CONFIG ---
ARCHIDEKT_INPUT = "https://archidekt.com/decks/21948302/withering_my_tokens"

# --- HELPERS ---

def safe_name(s):
    return s.lower().replace(" ", "_").replace("/", "_")


def get_deck_data(deck_id):
    url = f"https://archidekt.com/api/decks/{deck_id}/"
    r = requests.get(url)
    r.raise_for_status()
    return r.json()


def get_card_names(deck_data):
    names = []
    for item in deck_data["cards"]:
        try:
            names.append(item["card"]["oracleCard"]["name"])
        except KeyError:
            continue
    return list(set(names))


def scryfall_named(name):
    url = "https://api.scryfall.com/cards/named"
    r = requests.get(url, params={"exact": name})
    if r.status_code != 200:
        return None
    return r.json()


# --- TOKEN DISCOVERY ---

def tokens_from_all_parts(card_json):
    tokens = []
    for part in card_json.get("all_parts", []):
        if part.get("component") == "token" and "uri" in part:
            tokens.append(part["uri"])
    return tokens


def tokens_from_oracle(card_json):
    oracle = card_json.get("oracle_text", "")
    if not oracle:
        return []

    uris = []

    matches = re.findall(r"create (?:a|an|two|three|X)? ([^\.]+?) token", oracle, re.IGNORECASE)

    for m in matches:
        query = f't:{m}'
        url = "https://api.scryfall.com/cards/search"

        r = requests.get(url, params={"q": query})
        if r.status_code != 200:
            continue

        data = r.json()
        for c in data.get("data", []):
            if c.get("layout") == "token":
                uris.append(c["uri"])

    return uris


# --- TOKEN METADATA EXTRACTION ---

def extract_token_data(uri):
    r = requests.get(uri)
    if r.status_code != 200:
        return None

    data = r.json()

    name = safe_name(data.get("name", "unknown"))

    # power/toughness
    power = data.get("power")
    tough = data.get("toughness")

    # color (use color_identity)
    colors = data.get("color_identity", [])
    color_map = {
        "W": "w",
        "U": "u",
        "B": "b",
        "R": "r",
        "G": "g"
    }

    if colors:
        color = "".join(color_map[c] for c in colors)
    else:
        color = "c"  # colorless

    # version (just use collector number or fallback counter)
    version = data.get("collector_number", "1")

    if power and tough:
        return name, power, tough, color, version
    else:
        return name, "token", "token", color, version


# --- MAIN ---

def main():
    deck_id = extract_deck_id(ARCHIDEKT_INPUT)
    deck = get_deck_data(deck_id)
    deck_name = safe_name(deck["name"])
    card_names = get_card_names(deck)

    all_token_uris = set()
    failed_cards = []

    print("Scanning deck...")

    for name in card_names:
        card_json = scryfall_named(name)

        if not card_json:
            failed_cards.append(name)
            continue

        tokens = tokens_from_all_parts(card_json)

        if not tokens:
            tokens = tokens_from_oracle(card_json)

        if tokens:
            for t in tokens:
                all_token_uris.add(t)
        else:
            failed_cards.append(name)

    print(f"Found {len(all_token_uris)} tokens")

    # --- BUILD CSV ---
    rows = []
    version_counter = {}

    for uri in all_token_uris:
        data = extract_token_data(uri)

        if not data:
            continue

        name, power, tough, color, version = data

        # ensure unique version numbers per name
        key = name
        version_counter.setdefault(key, 0)
        version_counter[key] += 1
        version = str(version_counter[key])

        # build filename
        if power == "token":
            filename = f"{name}_token_{color}_{version}.png"
        else:
            filename = f"{name}_{power}_{tough}_{color}_{version}.png"

        rows.append([filename, name, power, tough, color, version])

    # --- WRITE CSV ---
    csv_name = f"{deck_name}.csv"

    with open(csv_name, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerows(rows)

    # --- REPORT ---
    print(f"\n✅ CSV written: {csv_name}")
    print(f"Entries: {len(rows)}")

    print("\n⚠️ Cards with no tokens:")
    for c in failed_cards:
        print(f"- {c}")


if __name__ == "__main__":
    main()

