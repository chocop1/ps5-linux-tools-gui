# Maintainer: PS5 Linux Tools GUI
pkgname=ps5-linux-tools-gui-cpp
pkgver=1.0.0
pkgrel=1
pkgdesc="Native C++17/Qt6 GUI for ps5-linux-tools with C wlroots overlay, fan curve, sensors, display and M.2"
arch=('x86_64')
url="https://github.com/chocop1/ps5-linux-tools-gui"
license=('GPL-3.0-or-later')
depends=('qt6-base' 'qt6-wayland' 'layer-shell-qt' 'polkit' 'wayland' 'pango' 'cairo' 'zlib')
makedepends=('gcc' 'make' 'pkgconf')
optdepends=(
    'kscreen: display mode, VRR and HDR management'
    'mangohud: in-game overlay profile generation'
    'dolphin: open mounted M.2 images'
)
conflicts=('ps5-linux-tools-gui')

build() {
    # Note: Do not run `makepkg -c` directly in this tree unless BUILDDIR is set outside the repo,
    # because the C++ sources live in ./src/.
    if [ -d "$startdir/overlay_c" ]; then
        make -C "$startdir/overlay_c"
    fi
    if [ -f "$startdir/ps5-linux-tools/Makefile" ]; then
        make -C "$startdir/ps5-linux-tools"
    fi
    mkdir -p "$startdir/build"
    (
        cd "$startdir/build"
        qmake6 "$startdir/ps5tools-cpp.pro"
        make -j"$(nproc)"
    )
    cp "$startdir/build/ps5tools-qt6" "$startdir/ps5tools-qt6"
    strip --strip-unneeded "$startdir/ps5tools-qt6"
}

package() {
    install -Dm755 "$startdir/ps5tools-qt6" "$pkgdir/usr/bin/ps5tools-qt6"
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
