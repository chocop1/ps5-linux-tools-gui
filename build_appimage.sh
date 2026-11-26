#!/usr/bin/env bash
# build_appimage.sh — AppImage builder for PS5 Linux Tools (PyQt6 + modular ps5tools/ package).
# Does not require root privileges to build.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APPDIR="./PS5LinuxTools.AppDir"
OUTPUT="PS5LinuxTools-x86_64.AppImage"

echo "==> Проверка зависимостей..."

if ! command -v python3 &>/dev/null; then
    echo "✗  python3 не найден. Установите: sudo pacman -S python"
    exit 1
fi
echo "    ✓  $(python3 --version 2>&1)"

if ! python3 -c "import PyQt6" &>/dev/null 2>&1; then
    echo "✗  PyQt6 не установлен! Установите: sudo pacman -S python-pyqt6"
    exit 1
fi
echo "    ✓  PyQt6"

echo ""
echo "==> Создание структуры AppDir: $APPDIR"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib/ps5tools"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"

echo "==> Копирование приложения и модулей..."
if [ ! -f "ps5tools_gui.py" ] || [ ! -d "ps5tools" ]; then
    echo "✗  Не найдены ps5tools_gui.py или пакет ps5tools/"
    exit 1
fi

cp ps5tools_gui.py "$APPDIR/usr/lib/ps5tools/"
chmod +x "$APPDIR/usr/lib/ps5tools/ps5tools_gui.py"

# Copy the package without __pycache__
rsync_or_cp() {
    cp -r ps5tools "$APPDIR/usr/lib/ps5tools/"
    find "$APPDIR/usr/lib/ps5tools/ps5tools" -name "__pycache__" -type d -exec rm -rf {} +
}
rsync_or_cp

cp ps5tools-root-helper "$APPDIR/usr/lib/ps5tools/"
chmod +x "$APPDIR/usr/lib/ps5tools/ps5tools-root-helper"
cp org.ps5linuxtools.policy "$APPDIR/usr/lib/ps5tools/"

if [ -d "overlay_c" ] && command -v make &>/dev/null && command -v gcc &>/dev/null; then
    echo "==> Сборка микро-оверлея ps5overlayd..."
    make -C overlay_c
fi
if [ -x "ps5overlayd" ]; then
    cp ps5overlayd "$APPDIR/usr/lib/ps5tools/ps5overlayd"
    chmod +x "$APPDIR/usr/lib/ps5tools/ps5overlayd"
fi

if [ -d "ps5-linux-tools" ]; then
    if [ -f "ps5-linux-tools/Makefile" ] && { [ ! -x "ps5-linux-tools/ps5_control" ] || [ ! -x "ps5-linux-tools/m2_init" ]; }; then
        if command -v make &>/dev/null && command -v gcc &>/dev/null; then
            echo "==> Сборка апстрим-утилит ps5-linux-tools..."
            make -C ps5-linux-tools
        fi
    fi
    echo "==> Копирование ps5-linux-tools/ (без .git и объектных файлов)..."
    mkdir -p "$APPDIR/usr/lib/ps5tools/ps5-linux-tools"
    for f in ps5_control m2_init m2_install.sh m2_exec.sh ps5-linux-warm-reboot.sh install.sh; do
        if [ -e "ps5-linux-tools/$f" ]; then
            cp -a "ps5-linux-tools/$f" "$APPDIR/usr/lib/ps5tools/ps5-linux-tools/"
        fi
    done
    if [ -d "ps5-linux-tools/systemd" ]; then
        cp -r "ps5-linux-tools/systemd" "$APPDIR/usr/lib/ps5tools/ps5-linux-tools/"
    fi
fi

echo "==> Создание AppRun и usr/bin/ps5tools..."
cat > "$APPDIR/AppRun" << 'EOF'
#!/usr/bin/env bash
SELF="$(readlink -f "$0")"
HERE="$(dirname "$SELF")"

export PATH="$HERE/usr/lib/ps5tools/ps5-linux-tools:$PATH"
export PYTHONPATH="$HERE/usr/lib/ps5tools:${PYTHONPATH:-}"

exec python3 "$HERE/usr/lib/ps5tools/ps5tools_gui.py" "$@"
EOF
chmod +x "$APPDIR/AppRun"
ln -sf ../../AppRun "$APPDIR/usr/bin/ps5tools"

echo "==> Создание .desktop файла..."
cat > "$APPDIR/ps5tools.desktop" << 'EOF'
[Desktop Entry]
Name=PS5 Linux Tools
Name[ru]=Инструменты PS5 Linux
Comment=GUI for ps5-linux-tools
Comment[ru]=Управление вентилятором, Boost, кривой охлаждения, дисплеем и M.2 SSD PS5
Exec=ps5tools
Icon=ps5tools
Terminal=false
Type=Application
Categories=System;HardwareSettings;
Keywords=ps5;fan;boost;m2;nvme;
StartupNotify=true
EOF
cp "$APPDIR/ps5tools.desktop" "$APPDIR/usr/share/applications/"

if command -v desktop-file-validate &>/dev/null; then
    desktop-file-validate "$APPDIR/ps5tools.desktop"
    echo "    ✓  desktop-file-validate OK"
fi

echo "==> Генерация иконки в палитре KDE Breeze / OxygenDark..."
python3 - << 'PYEOF'
import os, shutil, subprocess

svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <rect x="8" y="8" width="240" height="240" rx="42" ry="42" fill="#1b2823" stroke="#00d3b8" stroke-width="5"/>
  <ellipse cx="128" cy="142" rx="88" ry="56" fill="#2e3933" stroke="#455049" stroke-width="3"/>
  <ellipse cx="64" cy="172" rx="34" ry="24" fill="#243b30" transform="rotate(-16,64,172)"/>
  <ellipse cx="192" cy="172" rx="34" ry="24" fill="#243b30" transform="rotate(16,192,172)"/>
  <rect x="72" y="124" width="10" height="30" rx="3" fill="#00d3b8"/>
  <rect x="62" y="134" width="30" height="10" rx="3" fill="#00d3b8"/>
  <circle cx="176" cy="128" r="6" fill="#00d3b8"/>
  <circle cx="192" cy="140" r="6" fill="#efeee1"/>
  <circle cx="160" cy="140" r="6" fill="#efeee1"/>
  <circle cx="176" cy="152" r="6" fill="#00d3b8"/>
  <circle cx="128" cy="138" r="18" fill="#13211d" stroke="#00d3b8" stroke-width="2.5"/>
  <text x="128" y="143" text-anchor="middle" font-family="sans-serif" font-size="13" font-weight="bold" fill="#efeee1">PS5</text>
  <circle cx="96" cy="160" r="13" fill="#13211d" stroke="#455049" stroke-width="2"/>
  <circle cx="160" cy="160" r="13" fill="#13211d" stroke="#455049" stroke-width="2"/>
  <rect x="82" y="70" width="92" height="5" rx="2.5" fill="#00d3b8"/>
</svg>
'''

appdir = "./PS5LinuxTools.AppDir"
svg_targets = [
    os.path.join(appdir, "ps5tools.svg"),
    os.path.join(appdir, "usr", "lib", "ps5tools", "ps5tools.svg"),
    os.path.join(appdir, "usr", "share", "icons", "hicolor", "scalable", "apps", "ps5tools.svg"),
]
for dst in svg_targets:
    with open(dst, "w", encoding="utf-8") as f:
        f.write(svg_content)

png_256 = os.path.join(appdir, "usr", "share", "icons", "hicolor", "256x256", "apps", "ps5tools.png")
root_png = os.path.join(appdir, "ps5tools.png")
converted = False

if shutil.which("rsvg-convert"):
    r = subprocess.run(["rsvg-convert", "-w", "256", "-h", "256", svg_targets[0], "-o", png_256], capture_output=True)
    if r.returncode == 0:
        shutil.copy(png_256, root_png)
        converted = True
elif shutil.which("inkscape"):
    r = subprocess.run(["inkscape", "--export-type=png", f"--export-filename={png_256}", "--export-width=256", "--export-height=256", svg_targets[0]], capture_output=True)
    if r.returncode == 0:
        shutil.copy(png_256, root_png)
        converted = True

if converted:
    print("    ✓  SVG + PNG 256x256 созданы")
else:
    print("    ✓  SVG иконка создана")
PYEOF

echo ""
echo "==> Поиск appimagetool..."
if command -v appimagetool &>/dev/null; then
    APPIMAGETOOL="appimagetool"
elif [ -x "/tmp/appimagetool" ]; then
    APPIMAGETOOL="/tmp/appimagetool"
else
    echo "    ℹ  Скачиваем appimagetool..."
    APPTOOL_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
    if command -v curl &>/dev/null; then
        curl -fL --progress-bar "$APPTOOL_URL" -o /tmp/appimagetool
    elif command -v wget &>/dev/null; then
        wget -q --show-progress "$APPTOOL_URL" -O /tmp/appimagetool
    else
        echo "✗  Нет curl или wget для загрузки appimagetool."
        exit 1
    fi
    chmod +x /tmp/appimagetool
    APPIMAGETOOL="/tmp/appimagetool"
fi

echo "==> Сборка AppImage ($OUTPUT)..."
rm -f "$OUTPUT"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"

echo ""
echo "✓  Готово: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
