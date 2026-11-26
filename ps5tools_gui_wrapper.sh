#!/usr/bin/env bash
# ps5tools_gui_wrapper.sh — launcher wrapper for PS5 Linux Tools GUI.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE="$SCRIPT_DIR/PS5LinuxTools-x86_64.AppImage"
GUI="$SCRIPT_DIR/ps5tools_gui.py"

if [ -x "$APPIMAGE" ] && [ "${PS5TOOLS_NO_APPIMAGE:-0}" != "1" ]; then
    exec "$APPIMAGE" "$@"
fi

exec python3 "$GUI" "$@"
