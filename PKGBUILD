# Maintainer: PS5 Linux Tools GUI
pkgname=ps5-linux-tools-gui
pkgver=1.0.0
pkgrel=1
pkgdesc="Native KDE/PyQt6 GUI for ps5-linux-tools with C wlroots overlay, fan curve, sensors, display and M.2"
arch=('x86_64')
url="https://github.com/chocop1/ps5-linux-tools-gui"
license=('GPL-3.0-or-later')
depends=('python' 'python-pyqt6' 'polkit' 'wayland' 'pango' 'cairo' 'zlib')
makedepends=('gcc' 'make' 'pkgconf')
optdepends=(
    'kscreen: display mode, VRR and HDR management'
    'mangohud: in-game overlay profile generation'
    'dolphin: open mounted M.2 images'
)
conflicts=('ps5-linux-tools-gui-cpp')

build() {
    if [ -d "$startdir/overlay_c" ]; then
        make -C "$startdir/overlay_c"
    fi
    if [ -f "$startdir/ps5-linux-tools/Makefile" ]; then
        make -C "$startdir/ps5-linux-tools"
    fi
}

package() {
    install -d "$pkgdir/usr/lib/ps5tools"
    cp -r "$startdir/ps5tools" "$pkgdir/usr/lib/ps5tools/"
    find "$pkgdir/usr/lib/ps5tools/ps5tools" -name "__pycache__" -type d -exec rm -rf {} +
    install -Dm755 "$startdir/ps5tools_gui.py" "$pkgdir/usr/lib/ps5tools/ps5tools_gui.py"

    if [ -x "$startdir/ps5overlayd" ]; then
        install -Dm755 "$startdir/ps5overlayd" "$pkgdir/usr/bin/ps5overlayd"
    fi

    if [ -d "$startdir/ps5-linux-tools" ]; then
        install -d "$pkgdir/usr/lib/ps5tools/ps5-linux-tools"
        for f in ps5_control m2_init m2_install.sh m2_exec.sh ps5-linux-warm-reboot.sh install.sh; do
            if [ -e "$startdir/ps5-linux-tools/$f" ]; then
                install -Dm755 "$startdir/ps5-linux-tools/$f" \
                    "$pkgdir/usr/lib/ps5tools/ps5-linux-tools/$f"
            fi
        done
        if [ -d "$startdir/ps5-linux-tools/systemd" ]; then
            cp -r "$startdir/ps5-linux-tools/systemd" "$pkgdir/usr/lib/ps5tools/ps5-linux-tools/"
        fi
    fi

    install -d "$pkgdir/usr/bin"
    cat << 'EOF' > "$pkgdir/usr/bin/ps5tools-gui"
#!/usr/bin/env sh
exec python3 /usr/lib/ps5tools/ps5tools_gui.py "$@"
EOF
    chmod 755 "$pkgdir/usr/bin/ps5tools-gui"

    install -Dm755 "$startdir/ps5tools-root-helper" "$pkgdir/usr/local/libexec/ps5tools-helper"
    install -Dm644 "$startdir/org.ps5linuxtools.policy" "$pkgdir/usr/share/polkit-1/actions/org.ps5linuxtools.policy"

    if [ -f "$startdir/ps5tools.svg" ]; then
        install -Dm644 "$startdir/ps5tools.svg" \
            "$pkgdir/usr/share/icons/hicolor/scalable/apps/ps5tools.svg"
        install -Dm644 "$startdir/ps5tools.svg" \
            "$pkgdir/usr/share/ps5tools/ps5tools.svg"
    fi

    install -Dm644 "$startdir/ps5tools_gui.desktop" \
        "$pkgdir/usr/share/applications/ps5tools_gui.desktop"
    install -Dm644 "$startdir/LICENSE" \
        "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
