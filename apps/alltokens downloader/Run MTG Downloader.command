#!/bin/bash
# MTG Token Downloader — Mac Launcher
# Double-click this file to run the app.
# It will install any missing dependencies automatically.

# ── Find a working Python 3 ───────────────────────────────────────────────────
PYTHON=""
for candidate in python3 /usr/local/bin/python3 /opt/homebrew/bin/python3 /usr/bin/python3; do
    if command -v "$candidate" &>/dev/null; then
        VER=$("$candidate" --version 2>&1 | awk '{print $2}')
        MAJOR=$(echo "$VER" | cut -d. -f1)
        MINOR=$(echo "$VER" | cut -d. -f2)
        if [ "$MAJOR" -ge 3 ] && [ "$MINOR" -ge 8 ]; then
            PYTHON="$candidate"
            break
        fi
    fi
done

if [ -z "$PYTHON" ]; then
    osascript -e 'display alert "Python 3.8+ not found" message "Please install Python from https://www.python.org/downloads/ and try again." as critical'
    exit 1
fi

echo "Using: $PYTHON ($($PYTHON --version))"

# ── Check / install tkinter ───────────────────────────────────────────────────
if ! $PYTHON -c "import tkinter" &>/dev/null; then
    osascript -e 'display alert "tkinter not found" message "tkinter is missing from your Python installation.\n\nPlease install Python from https://www.python.org/downloads/ (the official installer includes tkinter) then try again." as critical'
    exit 1
fi

# ── Install missing pip packages ─────────────────────────────────────────────
echo "Checking dependencies..."
$PYTHON -m pip install --quiet --upgrade aiohttp Pillow 2>&1 | grep -v "already satisfied" || true

# ── Launch the app ────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP="$SCRIPT_DIR/MTGTokenDownloader.py"

if [ ! -f "$APP" ]; then
    osascript -e "display alert \"File not found\" message \"MTGTokenDownloader.py must be in the same folder as this launcher.\n\nLooked in: $SCRIPT_DIR\" as critical"
    exit 1
fi

echo "Launching MTG Token Downloader..."
$PYTHON "$APP"
