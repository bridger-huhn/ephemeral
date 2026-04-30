
import os
import re
import asyncio
import aiohttp
from PIL import Image
from io import BytesIO
from collections import defaultdict

os.chdir("/Users/bridgerhuhn/Documents/Mtg-tokens/micropythonSD")

SAVE_DIR = "tokens"
SIZE = (320, 480)
CONCURRENT_DOWNLOADS = 20

os.makedirs(SAVE_DIR, exist_ok=True)

name_counts = defaultdict(int)


def clean(text):
    if not text:
        return "token"
    text = re.sub(r"[^\w\s-]", "", text)
    text = text.replace(" ", "_")
    return text.lower()


def color_code(colors):
    if not colors:
        return "c"
    return "".join(colors).lower()


def build_base_name(card):
    name = clean(card.get("name"))
    power = card.get("power")
    toughness = card.get("toughness")

    if power and toughness:
        pt = f"{clean(power)}_{clean(toughness)}"
    else:
        pt = "token"

    colors = color_code(card.get("colors"))

    return f"{name}_{pt}_{colors}"


def get_image_url(card):
    if "image_uris" in card:
        return card["image_uris"].get("png")
    if "card_faces" in card:
        for face in card["card_faces"]:
            if "image_uris" in face:
                return face["image_uris"].get("png")
    return None


async def fetch_json(session, url):
    async with session.get(url) as resp:
        return await resp.json()


async def get_all_tokens(session):
    url = "https://api.scryfall.com/cards/search?q=t:token&unique=prints"
    tokens = []

    while url:
        data = await fetch_json(session, url)
        tokens.extend(data["data"])
        url = data.get("next_page")

    return tokens


async def download_token(session, semaphore, card):
    url = get_image_url(card)
    if not url:
        return

    base = build_base_name(card)

    name_counts[base] += 1
    art_number = name_counts[base]

    filename = f"{base}_{art_number}.png"
    path = os.path.join(SAVE_DIR, filename)

    async with semaphore:
        try:
            async with session.get(url) as resp:
                img_bytes = await resp.read()

            img = Image.open(BytesIO(img_bytes)).convert("RGB")
            img = img.resize(SIZE, Image.LANCZOS)
            img.save(path, "PNG")

            print("saved", filename)

        except Exception as e:
            print("failed", filename, e)


async def main():
    semaphore = asyncio.Semaphore(CONCURRENT_DOWNLOADS)

    async with aiohttp.ClientSession() as session:
        tokens = await get_all_tokens(session)

        print(f"found {len(tokens)} tokens")

        tasks = [
            download_token(session, semaphore, card)
            for card in tokens
        ]

        await asyncio.gather(*tasks)


if __name__ == "__main__":
    asyncio.run(main())