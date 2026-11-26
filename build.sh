#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -f "ps5-linux-tools/Makefile" ] && { [ ! -x "ps5-linux-tools/ps5_control" ] || [ ! -x "ps5-linux-tools/m2_init" ]; }; then
    make -C ps5-linux-tools
fi

if [ -d "overlay_c" ]; then
    make -C overlay_c
fi

mkdir -p build
cd build
qmake6 ../ps5tools-cpp.pro
make -j"$(nproc)"
cp ps5tools-qt6 ../ps5tools-qt6
strip --strip-unneeded ../ps5tools-qt6
echo "✓ Собрано: $SCRIPT_DIR/ps5tools-qt6 ($(du -h ../ps5tools-qt6 | cut -f1))"
[ -x "$SCRIPT_DIR/ps5overlayd" ] && echo "✓ Собрано: $SCRIPT_DIR/ps5overlayd ($(du -h "$SCRIPT_DIR/ps5overlayd" | cut -f1))" || true
