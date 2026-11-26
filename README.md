# PS5 Linux Tools GUI

**Language:** English | [Русский](README.ru.md)

A native graphical interface for managing and monitoring PlayStation 5 / PS5 Pro running Linux (with support for **Linux 7.3.3+** kernel features), paired with a standalone ultra-lightweight C Wayland HUD overlay (`ps5overlayd`).

> Upstream CLI tools: [ps5-linux/ps5-linux-tools](https://github.com/ps5-linux/ps5-linux-tools) (included as a `git submodule`).

---

## Repository Branches

| Branch | GUI Stack | Overlay | GUI Memory (`VmRSS` / `RssAnon`) | Overlay Memory (`ps5overlayd`) |
| --- | --- | --- | --- | --- |
| **`main`** | Python 3 + PyQt6 | C (`wlroots` layer-shell + PangoCairo) | ~105 MiB / ~29.7 MiB | ~18.4 MiB / ~4.2 MiB |
| **`cpp-qt6`** | C++17 + Qt6 | C (`wlroots` layer-shell + PangoCairo) | ~77.7 MiB / ~15.6 MiB | ~18.4 MiB / ~4.2 MiB |

Both branches provide identical features, share the same native C overlay daemon `ps5overlayd` (which keeps running on screen even after the main GUI is exited), and use the same Polkit helper `ps5tools-root-helper`.

---

## Key Features

- **PS5 & Kernel 7.3.3+ Control**: `ps5_control` integration (PS5 and PS5 Pro / *Viola*), manual and automatic fan control, visual 8-point fan curve editor (`hwmon` `ps5_fan`) with persistence via `ps5-fan-curve.service`, Boost mode, and GPU `runtime PM` monitoring.
- **Live Hardware Monitoring**: SoC/GPU and board thermal sensors, GPU/CPU load, clocks, VRAM/GTT usage, and a MangoHud profile generator.
- **Standalone HUD Overlay (`ps5overlayd`)**: stays above fullscreen games and windows using `wlr-layer-shell-unstable-v1` (`Overlay` layer), with configurable position, opacity, and visible metrics (a Qt widget fallback is available on X11).
- **Display & Peripherals**: display mode switching with explicit refresh rates (`kscreen-doctor`), HDMI diagnostics (*Flava3* / *Verde* chips, FRL link / 4K@120Hz, VRR/HDR, hotplug), and Bluetooth/DualSense/Wi-Fi (`MT7921` / `IW620`) status.
- **M.2 SSD Tools**: block device overview (`lsblk`), initialization (`m2_init`), image mounting, installation, and booting (`m2_install.sh`, `m2_exec.sh`, warm reboot) with rootfs overwrite protection.
- **System Services & Logs**: management of `ps5fan.service`, `ps5boost.service`, `ps5-bt-quiet.service`, `ps5-bt-sync.service`, `ps5-stage-firmware.service`, `ps5-stage-gpu-firmware.service`, `ps5-fan-curve.service`, plus filtered `journalctl` inspection.
- **Localization & Themes (`App Settings`)**: English & Russian UI (auto-selected from KDE/system locale with English fallback), live theme & color scheme switching (**Breeze**, **Oxygen**, **Union**, **Qt Fusion**, **Microsoft Windows 9x**), and **"Follow system"** defaults.
- **Unprivileged GUI with Polkit**: runs without root privileges, invoking `/usr/local/libexec/ps5tools-helper` through `pkexec` and `org.ps5linuxtools.policy`.

---

## Installation

### Method 1: Prebuilt AppImage (Quick Start)

1. Download the desired binary from **Releases**:
   - `PS5LinuxTools-x86_64.AppImage` — Python + PyQt6 version
   - `PS5LinuxTools-Cpp-x86_64.AppImage` — C++ + Qt6 version
2. Make it executable and launch:
   ```bash
   chmod +x PS5LinuxTools-*.AppImage
   ./PS5LinuxTools-*.AppImage
   ```

> **Note:** The AppImage uses your system Qt6/PyQt6 libraries (on Arch / CachyOS with KDE Plasma: `python-pyqt6` for the Python build, or `qt6-base qt6-wayland layer-shell-qt` for the C++ build). Display configuration on KDE requires `kscreen` (`kscreen-doctor`).

---

### Method 2: Arch Linux / CachyOS via `PKGBUILD`

#### Branch `main` (Python + PyQt6)
```bash
git clone --recursive https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui
makepkg -si
```

#### Branch `cpp-qt6` (C++ + Qt6)
```bash
git clone --recursive -b cpp-qt6 https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui
BUILDDIR=/tmp/makepkg-ps5 makepkg -si
```
*(Setting a separate `BUILDDIR` on `cpp-qt6` protects the local `./src/` C++ source folder if `makepkg -c` cleans `$srcdir`.)*

---

### Method 3: Build and Run from Source

#### Dependencies (Arch Linux / CachyOS)

**Common (for `ps5overlayd`, `ps5-linux-tools`, and Polkit):**
```bash
sudo pacman -S --needed base-devel pkgconf zlib wayland pango cairo polkit
```

**For `main` (Python + PyQt6):**
```bash
sudo pacman -S --needed python python-pyqt6
```

**For `cpp-qt6` (C++ + Qt6):**
```bash
sudo pacman -S --needed qt6-base qt6-wayland layer-shell-qt
```

**Optional (recommended on KDE Plasma):**
```bash
sudo pacman -S --needed kscreen mangohud dolphin
```

#### Building `main` (Python + C overlay)

```bash
git clone --recursive https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui

# 1. Build upstream CLI utilities (ps5_control and m2_init)
make -C ps5-linux-tools

# 2. Build the native C micro-overlay (ps5overlayd)
make -C overlay_c

# 3. Launch the GUI directly
python3 ps5tools_gui.py

# Or build an AppImage (requires curl or wget to fetch appimagetool):
./build_appimage.sh
```

#### Building `cpp-qt6` (C++ + Qt6 + C overlay)

```bash
git clone --recursive -b cpp-qt6 https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui

# 1. Build ps5-linux-tools, ps5overlayd, and ps5tools-qt6
./build.sh

# 2. Launch the GUI
./ps5tools-qt6

# Or build an AppImage:
./build_appimage.sh
```

---

## Polkit Integration (When Running Without `makepkg`)

Privileged operations (fan control, Boost, M.2 operations, and systemd units) require installing `ps5tools-helper` and the Polkit action policy:

- Click the system integration install button inside the GUI (Services tab), or
- Install manually:
  ```bash
  sudo install -Dm755 ps5tools-root-helper /usr/local/libexec/ps5tools-helper
  sudo install -Dm644 org.ps5linuxtools.policy /usr/share/polkit-1/actions/org.ps5linuxtools.policy
  ```
If the `ps5-linux-tools` binaries are not yet installed in `/usr/local/sbin/`, install them as well:
```bash
sudo ./ps5-linux-tools/install.sh
# and if using M.2 tools:
sudo install -Dm755 ps5-linux-tools/m2_init ps5-linux-tools/m2_install.sh ps5-linux-tools/m2_exec.sh ps5-linux-tools/ps5-linux-warm-reboot.sh /usr/local/sbin/
```

---

## License & Attribution

- The GUI code (`ps5tools/`, `src/`), micro-overlay `ps5overlayd`, `ps5tools-root-helper`, Polkit policy, and build scripts are licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)** — see [LICENSE](LICENSE).
- The upstream CLI utilities in `ps5-linux-tools/` belong to **[ps5-linux/ps5-linux-tools](https://github.com/ps5-linux/ps5-linux-tools)** and are referenced as an external `git submodule`.
