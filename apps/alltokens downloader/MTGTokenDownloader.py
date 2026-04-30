#!/usr/bin/env python3
"""
MTG Token Downloader
Downloads all MTG tokens from Scryfall and converts them to RGB565 .bin files
for use with ESP32 TFT displays.
"""

import os
import re
import struct
import asyncio
import threading
import sys
import platform
from io import BytesIO
from collections import defaultdict
from datetime import datetime
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# ── Try to import required libraries, show friendly error if missing ──────────
try:
    import aiohttp
    from PIL import Image
except ImportError as e:
    import tkinter as tk
    from tkinter import messagebox
    root = tk.Tk(); root.withdraw()
    messagebox.showerror("Missing Libraries",
        f"Required library not found: {e}\n\nPlease run:\n  pip install aiohttp Pillow")
    sys.exit(1)

# ── Constants ─────────────────────────────────────────────────────────────────
WIDTH          = 320
HEIGHT         = 480
BYTES_PER_FILE = WIDTH * HEIGHT * 2
APP_VERSION    = "1.0.0"

# ── Colours (dark magic theme to match the device) ───────────────────────────
BG       = "#0A0612"
PANEL    = "#100C20"
PANEL2   = "#160E2A"
ACCENT   = "#8C5AFF"
GOLD     = "#FFC800"
TEXT     = "#FFFFFF"
TEXTDIM  = "#8C82AA"
GREEN    = "#3CB46E"
RED      = "#C83232"
ROWEVEN  = "#0E0A20"
ROWODD   = "#140E28"

# ── Core conversion logic (same as the standalone script) ─────────────────────
def clean(text):
    if not text: return "token"
    text = re.sub(r"[^\w\s-]", "", text)
    return text.replace(" ", "_").lower()

def color_code(colors):
    return "".join(colors).lower() if colors else "c"

def build_base_name(card):
    name   = clean(card.get("name"))
    power  = card.get("power")
    tough  = card.get("toughness")
    pt     = f"{clean(power)}_{clean(tough)}" if power and tough else "token"
    colors = color_code(card.get("colors"))
    return f"{name}_{pt}_{colors}"

def get_image_url(card):
    for fmt in ("border_crop", "art_crop", "png"):
        if "image_uris" in card:
            url = card["image_uris"].get(fmt)
            if url: return url
        if "card_faces" in card:
            for face in card["card_faces"]:
                if "image_uris" in face:
                    url = face["image_uris"].get(fmt)
                    if url: return url
    return None

def img_to_rgb565_bin(img):
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
            buf[idx]     = (word >> 8) & 0xFF
            buf[idx + 1] =  word       & 0xFF
            idx += 2
    return bytes(buf)

# ── Async downloader ──────────────────────────────────────────────────────────
async def fetch_json(session, url):
    async with session.get(url) as resp:
        resp.raise_for_status()
        return await resp.json()

async def get_all_tokens(session, progress_cb):
    url    = "https://api.scryfall.com/cards/search?q=t:token&unique=prints"
    tokens = []
    page   = 0
    while url:
        page += 1
        data = await fetch_json(session, url)
        tokens.extend(data["data"])
        url = data.get("next_page")
        progress_cb(f"Fetching card list... page {page} ({len(tokens)} cards found)")
    return tokens

async def process_token(session, semaphore, card, out_dir, name_counts, counters, progress_cb, stop_event):
    if stop_event.is_set():
        return

    url = get_image_url(card)
    if not url:
        counters["skip"] += 1
        return

    base     = build_base_name(card)
    name_counts[base] += 1
    filename = f"{base}_{name_counts[base]}.bin"
    path     = os.path.join(out_dir, filename)

    if os.path.exists(path) and os.path.getsize(path) == BYTES_PER_FILE:
        counters["skip"] += 1
        progress_cb(None)
        return

    async with semaphore:
        if stop_event.is_set():
            return
        try:
            async with session.get(url) as resp:
                resp.raise_for_status()
                img_bytes = await resp.read()
            img = Image.open(BytesIO(img_bytes))
            raw = img_to_rgb565_bin(img)
            with open(path, "wb") as f:
                f.write(raw)
            counters["ok"] += 1
        except Exception as e:
            counters["fail"] += 1
            counters["errors"].append(f"{filename}: {e}")
        progress_cb(None)

async def run_download(out_dir, workers, progress_cb, status_cb, stop_event):
    timeout = aiohttp.ClientTimeout(total=90)
    async with aiohttp.ClientSession(timeout=timeout) as session:
        status_cb("step1")
        tokens = await get_all_tokens(session, progress_cb)
        status_cb(f"step2:{len(tokens)}")

        name_counts = defaultdict(int)
        counters    = {"ok": 0, "fail": 0, "skip": 0, "total": len(tokens), "errors": []}

        semaphore = asyncio.Semaphore(workers)
        tasks = [
            process_token(session, semaphore, card, out_dir,
                          name_counts, counters, progress_cb, stop_event)
            for card in tokens
        ]
        await asyncio.gather(*tasks)
    return counters

# ── GUI ───────────────────────────────────────────────────────────────────────
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("MTG Token Downloader")
        self.resizable(False, False)
        self.configure(bg=BG)

        # State
        self._running     = False
        self._stop_event  = threading.Event()
        self._thread      = None
        self._total       = 0
        self._counters    = {}

        self._build_ui()
        self._center()

    # ── Layout ────────────────────────────────────────────────────────────────
    def _build_ui(self):
        pad = dict(padx=18, pady=6)

        # ── Header ────────────────────────────────────────────────────────────
        hdr = tk.Frame(self, bg=PANEL, pady=12)
        hdr.pack(fill="x")
        tk.Label(hdr, text="✦  MTG Token Downloader  ✦",
                 font=("Georgia", 18, "bold"), fg=GOLD, bg=PANEL).pack()
        tk.Label(hdr, text="Downloads all Scryfall tokens as RGB565 .bin files for ESP32 displays",
                 font=("Helvetica", 10), fg=TEXTDIM, bg=PANEL).pack(pady=(0,4))

        sep = tk.Frame(self, bg=ACCENT, height=2)
        sep.pack(fill="x")

        # ── Output folder ─────────────────────────────────────────────────────
        row1 = tk.Frame(self, bg=BG)
        row1.pack(fill="x", **pad, pady=(14,4))
        tk.Label(row1, text="Output Folder", font=("Helvetica", 11, "bold"),
                 fg=TEXT, bg=BG, width=14, anchor="w").pack(side="left")

        self._out_var = tk.StringVar(value=self._default_out())
        entry = tk.Entry(row1, textvariable=self._out_var, font=("Helvetica", 10),
                         bg=PANEL2, fg=TEXT, insertbackground=TEXT,
                         relief="flat", bd=4, width=38)
        entry.pack(side="left", padx=(0,6))
        tk.Button(row1, text="Browse…", command=self._browse,
                  font=("Helvetica", 10), bg=PANEL2, fg=ACCENT,
                  activebackground=ACCENT, activeforeground=TEXT,
                  relief="flat", padx=10, cursor="hand2").pack(side="left")

        # ── Workers slider ────────────────────────────────────────────────────
        row2 = tk.Frame(self, bg=BG)
        row2.pack(fill="x", **pad)
        tk.Label(row2, text="Parallel Downloads", font=("Helvetica", 11, "bold"),
                 fg=TEXT, bg=BG, width=14, anchor="w").pack(side="left")
        self._workers_var = tk.IntVar(value=20)
        slider = ttk.Scale(row2, from_=1, to=40, orient="horizontal",
                           variable=self._workers_var, length=220,
                           command=lambda _: self._lbl_workers.config(
                               text=str(self._workers_var.get())))
        slider.pack(side="left", padx=(0,8))
        self._lbl_workers = tk.Label(row2, text="20", font=("Helvetica", 11, "bold"),
                                     fg=GOLD, bg=BG, width=3)
        self._lbl_workers.pack(side="left")

        # ── Divider ───────────────────────────────────────────────────────────
        tk.Frame(self, bg=PANEL2, height=1).pack(fill="x", padx=18, pady=8)

        # ── Stats row ─────────────────────────────────────────────────────────
        stats = tk.Frame(self, bg=BG)
        stats.pack(fill="x", padx=18, pady=(0,6))
        self._stat_vars = {}
        for col, (key, label, colour) in enumerate([
            ("total",    "Total",     TEXTDIM),
            ("ok",       "Converted", GREEN),
            ("skip",     "Skipped",   GOLD),
            ("fail",     "Failed",    RED),
        ]):
            cell = tk.Frame(stats, bg=PANEL2, padx=14, pady=8)
            cell.grid(row=0, column=col, padx=6, sticky="nsew")
            stats.columnconfigure(col, weight=1)
            v = tk.StringVar(value="—")
            self._stat_vars[key] = v
            tk.Label(cell, textvariable=v, font=("Helvetica", 22, "bold"),
                     fg=colour, bg=PANEL2).pack()
            tk.Label(cell, text=label, font=("Helvetica", 9),
                     fg=TEXTDIM, bg=PANEL2).pack()

        # ── Progress bar ──────────────────────────────────────────────────────
        pb_frame = tk.Frame(self, bg=BG)
        pb_frame.pack(fill="x", padx=18, pady=(4,0))

        style = ttk.Style()
        style.theme_use("default")
        style.configure("Magic.Horizontal.TProgressbar",
                        troughcolor=PANEL2, background=ACCENT,
                        thickness=14, borderwidth=0)
        self._pb = ttk.Progressbar(pb_frame, style="Magic.Horizontal.TProgressbar",
                                   orient="horizontal", mode="determinate",
                                   length=540)
        self._pb.pack(fill="x")

        self._lbl_status = tk.Label(self, text="Ready. Choose a folder and press Download.",
                                    font=("Helvetica", 9), fg=TEXTDIM, bg=BG)
        self._lbl_status.pack(pady=(4,0))

        # ── Log box ───────────────────────────────────────────────────────────
        log_frame = tk.Frame(self, bg=BG)
        log_frame.pack(fill="both", expand=True, padx=18, pady=(8,0))
        self._log = tk.Text(log_frame, height=10, font=("Courier", 9),
                            bg=PANEL, fg=TEXTDIM, insertbackground=TEXT,
                            relief="flat", bd=0, state="disabled",
                            wrap="word")
        sb = tk.Scrollbar(log_frame, command=self._log.yview, bg=PANEL2)
        self._log.configure(yscrollcommand=sb.set)
        self._log.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        self._log.tag_config("ok",   foreground=GREEN)
        self._log.tag_config("err",  foreground=RED)
        self._log.tag_config("gold", foreground=GOLD)
        self._log.tag_config("dim",  foreground=TEXTDIM)

        # ── Buttons ───────────────────────────────────────────────────────────
        btn_row = tk.Frame(self, bg=BG)
        btn_row.pack(fill="x", padx=18, pady=12)

        self._btn_start = tk.Button(btn_row, text="⬇  Download & Convert",
                                    command=self._start,
                                    font=("Helvetica", 13, "bold"),
                                    bg=ACCENT, fg=TEXT,
                                    activebackground="#6A3ADD", activeforeground=TEXT,
                                    relief="flat", padx=20, pady=10, cursor="hand2")
        self._btn_start.pack(side="left", fill="x", expand=True, padx=(0,8))

        self._btn_stop = tk.Button(btn_row, text="■  Stop",
                                   command=self._stop,
                                   font=("Helvetica", 13, "bold"),
                                   bg=PANEL2, fg=TEXTDIM,
                                   activebackground=RED, activeforeground=TEXT,
                                   relief="flat", padx=20, pady=10, cursor="hand2",
                                   state="disabled")
        self._btn_stop.pack(side="left", fill="x", expand=True)

        # ── Footer ────────────────────────────────────────────────────────────
        tk.Label(self, text=f"v{APP_VERSION}  •  RGB565 {WIDTH}×{HEIGHT}  •  {BYTES_PER_FILE:,} bytes/file",
                 font=("Helvetica", 8), fg=TEXTDIM, bg=BG).pack(pady=(0,8))

    # ── Helpers ───────────────────────────────────────────────────────────────
    def _center(self):
        self.update_idletasks()
        w, h = self.winfo_width(), self.winfo_height()
        sw, sh = self.winfo_screenwidth(), self.winfo_screenheight()
        self.geometry(f"+{(sw-w)//2}+{(sh-h)//2}")

    def _default_out(self):
        home = os.path.expanduser("~")
        return os.path.join(home, "Desktop", "MTG_Tokens")

    def _browse(self):
        folder = filedialog.askdirectory(title="Choose output folder",
                                         initialdir=self._out_var.get())
        if folder:
            self._out_var.set(folder)

    def _log_write(self, msg, tag=None):
        self._log.configure(state="normal")
        ts = datetime.now().strftime("%H:%M:%S")
        self._log.insert("end", f"[{ts}] {msg}\n", tag or "")
        self._log.see("end")
        self._log.configure(state="disabled")

    # ── Start / Stop ──────────────────────────────────────────────────────────
    def _start(self):
        out_dir = self._out_var.get().strip()
        if not out_dir:
            messagebox.showwarning("No folder", "Please choose an output folder first.")
            return
        try:
            os.makedirs(out_dir, exist_ok=True)
        except Exception as e:
            messagebox.showerror("Folder error", str(e))
            return

        self._running = True
        self._stop_event.clear()
        self._btn_start.configure(state="disabled", bg=PANEL2, fg=TEXTDIM)
        self._btn_stop.configure(state="normal", bg=RED, fg=TEXT)
        for k in self._stat_vars:
            self._stat_vars[k].set("—")
        self._pb["value"] = 0
        self._log.configure(state="normal"); self._log.delete("1.0","end")
        self._log.configure(state="disabled")

        workers = self._workers_var.get()
        self._log_write(f"Starting download → {out_dir}", "gold")
        self._log_write(f"Workers: {workers}  |  Target: {WIDTH}×{HEIGHT} RGB565", "dim")

        self._thread = threading.Thread(
            target=self._run_thread,
            args=(out_dir, workers),
            daemon=True
        )
        self._thread.start()

    def _stop(self):
        if self._running:
            self._stop_event.set()
            self._lbl_status.config(text="Stopping… finishing in-progress downloads…")
            self._btn_stop.configure(state="disabled", bg=PANEL2, fg=TEXTDIM)

    def _run_thread(self, out_dir, workers):
        """Runs in a background thread — drives the asyncio event loop."""
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        try:
            counters = loop.run_until_complete(
                run_download(out_dir, workers,
                             self._progress_cb,
                             self._status_cb,
                             self._stop_event)
            )
            self._counters = counters
        except Exception as e:
            self.after(0, self._log_write, f"Fatal error: {e}", "err")
            counters = {"ok":0,"fail":0,"skip":0,"total":0,"errors":[str(e)]}
            self._counters = counters
        finally:
            loop.close()
            self.after(0, self._on_done)

    # ── Callbacks from async code (thread-safe via after()) ──────────────────
    def _progress_cb(self, msg):
        """Called from async tasks — may be None (just update counters)."""
        self.after(0, self._update_progress, msg)

    def _status_cb(self, msg):
        self.after(0, self._handle_status, msg)

    def _handle_status(self, msg):
        if msg == "step1":
            self._lbl_status.config(text="Step 1/2 — Fetching card list from Scryfall…")
        elif msg.startswith("step2:"):
            total = int(msg.split(":")[1])
            self._total = total
            self._stat_vars["total"].set(str(total))
            self._lbl_status.config(text=f"Step 2/2 — Downloading and converting {total} tokens…")
            self._log_write(f"Found {total} tokens. Starting downloads…", "gold")

    def _update_progress(self, msg):
        c = self._counters if self._counters else {"ok":0,"fail":0,"skip":0}
        # pull live counters from the module-level dict if available
        # (we update via the counters dict reference in the async tasks)
        if msg:
            self._lbl_status.config(text=msg)
        done = c.get("ok",0) + c.get("fail",0) + c.get("skip",0)
        total = self._total or 1
        self._pb["value"] = min(done / total * 100, 100)
        self._stat_vars["ok"].set(str(c.get("ok", 0)))
        self._stat_vars["skip"].set(str(c.get("skip", 0)))
        self._stat_vars["fail"].set(str(c.get("fail", 0)))

    def _on_done(self):
        self._running = False
        c = self._counters
        stopped = self._stop_event.is_set()

        self._pb["value"] = 0 if stopped else 100
        self._stat_vars["ok"].set(str(c.get("ok", 0)))
        self._stat_vars["skip"].set(str(c.get("skip", 0)))
        self._stat_vars["fail"].set(str(c.get("fail", 0)))
        self._stat_vars["total"].set(str(c.get("total", 0)))

        if stopped:
            self._log_write("Stopped by user.", "err")
            self._lbl_status.config(text="Stopped.")
        else:
            total_mb = (c.get("ok",0) + c.get("skip",0)) * BYTES_PER_FILE / 1_048_576
            self._log_write(
                f"Done! Converted={c.get('ok',0)}  Skipped={c.get('skip',0)}  "
                f"Failed={c.get('fail',0)}  Total={total_mb:.1f} MB", "ok")
            self._lbl_status.config(text=f"Complete — {total_mb:.1f} MB of .bin files saved.")

        if c.get("errors"):
            self._log_write(f"{len(c['errors'])} error(s):", "err")
            for e in c["errors"][:20]:
                self._log_write(f"  {e}", "err")

        self._btn_start.configure(state="normal", bg=ACCENT, fg=TEXT)
        self._btn_stop.configure(state="disabled", bg=PANEL2, fg=TEXTDIM)

        if not stopped and c.get("fail", 0) == 0:
            messagebox.showinfo("Done!",
                f"All tokens downloaded and converted.\n\n"
                f"Converted : {c.get('ok',0)}\n"
                f"Skipped   : {c.get('skip',0)}\n"
                f"Location  : {self._out_var.get()}")


if __name__ == "__main__":
    app = App()
    app.mainloop()
