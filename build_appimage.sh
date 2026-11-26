#!/usr/bin/env bash
# build_appimage.sh — AppImage builder for the native C++ / Qt6 version of PS5 Linux Tools.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

./build.sh

APPDIR="./PS5LinuxTools-Cpp.AppDir"
OUTPUT="PS5LinuxTools-Cpp-x86_64.AppImage"

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/lib/ps5tools"
mkdir -p "$APPDIR/usr/share/applications"
mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"

cp ps5tools-qt6 "$APPDIR/usr/bin/ps5tools-qt6"
chmod +x "$APPDIR/usr/bin/ps5tools-qt6"
if [ -x "ps5overlayd" ]; then
    cp ps5overlayd "$APPDIR/usr/lib/ps5tools/ps5overlayd"
    chmod +x "$APPDIR/usr/lib/ps5tools/ps5overlayd"
fi
cp ps5tools-root-helper "$APPDIR/usr/lib/ps5tools/"
chmod +x "$APPDIR/usr/lib/ps5tools/ps5tools-root-helper"
cp org.ps5linuxtools.policy "$APPDIR/usr/lib/ps5tools/"
cp ps5tools.svg "$APPDIR/ps5tools.svg"
cp ps5tools.svg "$APPDIR/usr/lib/ps5tools/ps5tools.svg"
cp ps5tools.svg "$APPDIR/usr/share/icons/hicolor/scalable/apps/ps5tools.svg"

if [ -d "ps5-linux-tools" ]; then
    if [ -f "ps5-linux-tools/Makefile" ] && { [ ! -x "ps5-linux-tools/ps5_control" ] || [ ! -x "ps5-linux-tools/m2_init" ]; }; then
        if command -v make &>/dev/null && command -v gcc &>/dev/null; then
            make -C ps5-linux-tools
        fi
    fi
    mkdir -p "$APPDIR/usr/lib/ps5tools/ps5-linux-tools"
    for f in ps5_control m2_init m2_install.sh m2_exec.sh ps5-linux-warm-reboot.sh install.sh; do
        [ -e "ps5-linux-tools/$f" ] && cp -a "ps5-linux-tools/$f" "$APPDIR/usr/lib/ps5tools/ps5-linux-tools/"
    done
    [ -d "ps5-linux-tools/systemd" ] && cp -r "ps5-linux-tools/systemd" "$APPDIR/usr/lib/ps5tools/ps5-linux-tools/"
fi

cat > "$APPDIR/AppRun" << 'EOF'
#!/usr/bin/env bash
SELF="$(readlink -f "$0")"
HERE="$(dirname "$SELF")"
export PATH="$HERE/usr/lib/ps5tools/ps5-linux-tools:$PATH"
exec "$HERE/usr/bin/ps5tools-qt6" "$@"
EOF
chmod +x "$APPDIR/AppRun"

cat > "$APPDIR/ps5tools.desktop" << 'EOF'
[Desktop Entry]
Name=PS5 Linux Tools (Qt6 C++)
Name[ru]=Инструменты PS5 Linux (Qt6 C++)
Comment=Native C++ Qt6 GUI for ps5-linux-tools
Comment[ru]=Нативное Qt6 C++ приложение для управления PS5 Linux
Exec=ps5tools-qt6
Icon=ps5tools
Terminal=false
Type=Application
Categories=System;HardwareSettings;
Keywords=ps5;fan;boost;m2;nvme;
StartupNotify=true
EOF
cp "$APPDIR/ps5tools.desktop" "$APPDIR/usr/share/applications/"

if command -v appimagetool &>/dev/null; then
    APPIMAGETOOL="appimagetool"
elif [ -x "/tmp/appimagetool" ]; then
    APPIMAGETOOL="/tmp/appimagetool"
else
    curl -fL --progress-bar "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -o /tmp/appimagetool
    chmod +x /tmp/appimagetool
    APPIMAGETOOL="/tmp/appimagetool"
fi

rm -f "$OUTPUT"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"
echo "✓ Готово: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
