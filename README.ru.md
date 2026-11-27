# PS5 Linux Tools GUI

**Язык:** [English](README.md) | Русский

Нативный графический интерфейс для управления PlayStation 5 / PS5 Pro под управлением Linux (с поддержкой возможностей ядра **7.3.3+**), оснащённый автономным сверхлёгким Wayland-оверлеем на C (`ps5overlayd`).

> Апстрим CLI-утилиты: [ps5-linux/ps5-linux-tools](https://github.com/ps5-linux/ps5-linux-tools) (подключён как `git submodule`).

---

## Ветки репозитория

| Ветка | Стек GUI | Оверлей | Память GUI (`VmRSS` / `RssAnon`) | Память оверлея (`ps5overlayd`) |
| --- | --- | --- | --- | --- |
| **`main`** | C++17 + Qt6 | C (`wlroots` layer-shell + PangoCairo) | ~77.7 MiB / ~15.6 MiB | ~18.4 MiB / ~4.2 MiB |
| **`python-pyqt6`** | Python 3 + PyQt6 | C (`wlroots` layer-shell + PangoCairo) | ~105 MiB / ~29.7 MiB | ~18.4 MiB / ~4.2 MiB |

Обе ветки предоставляют одинаковый функционал, используют один и тот же C-демон оверлея `ps5overlayd` (который продолжает работать на экране даже после полного закрытия GUI) и единый Polkit-хелпер `ps5tools-root-helper`.

---

## Основные возможности

- **Управление PS5 и ядром 7.3.3+**: работа с `ps5_control` (PS5 и PS5 Pro / *Viola*), ручной и автоматический режим вентилятора, редактор кривой охлаждения (`hwmon` `ps5_fan`) с сохранением в `ps5-fan-curve.service`, режимы Boost и `runtime PM`.
- **Мониторинг в реальном времени**: температуры SoC/GPU и датчиков, загрузка, `VmRSS`, генерация конфига MangoHud.
- **Автономный HUD-оверлей (`ps5overlayd`)**: работает поверх полноэкранных игр и окон через протокол `wlr-layer-shell-unstable-v1` (Overlay layer), настраивается по позиции, прозрачности и набору метрик (на X11 предусмотрен запасной Qt-виджет).
- **Дисплей и периферия**: выбор разрешений вместе с частотой обновления (`kscreen-doctor`), диагностика HDMI (чипы *Flava3* / *Verde*, линк FRL / 4K@120Hz, VRR/HDR, hotplug), статус Bluetooth/DualSense (`MT7921` / `IW620`).
- **M.2 SSD**: обзор блочных устройств (`lsblk`), инициализация (`m2_init`), монтирование и установка/запуск образов (`m2_install.sh`, `m2_exec.sh`).
- **Системные службы и журнал**: управление `ps5fan.service`, `ps5boost.service`, `ps5-bt-quiet.service`, `ps5-bt-sync.service`, `ps5-stage-firmware.service`, `ps5-stage-gpu-firmware.service`, `ps5-fan-curve.service` и просмотр `journalctl`.
- **Локализация и темы (`Настройки`)**: русский и английский интерфейс (автоопределение из локали KDE/системы с откатом на английский для других языков), переключение тем и палитр на лету (**Breeze**, **Oxygen**, **Union**, **Qt Fusion**, **Microsoft Windows 9x**), а также режим **«Как в системе»**.
- **Безопасная работа с root через Polkit**: интерфейс работает без прав суперпользователя, вызывая `/usr/local/libexec/ps5tools-helper` через `pkexec` и политику `org.ps5linuxtools.policy`.

---

## Установка

### Способ 1: Готовый AppImage (быстрый запуск)

1. Скачайте нужный пакет из раздела **Releases**:
   - `PS5LinuxTools-x86_64.AppImage` — версия Python + PyQt6
   - `PS5LinuxTools-Cpp-x86_64.AppImage` — версия C++ + Qt6
2. Выдайте права на запуск и откройте:
   ```bash
   chmod +x PS5LinuxTools-*.AppImage
   ./PS5LinuxTools-*.AppImage
   ```

> **Примечание:** AppImage использует системные библиотеки Qt6 (на Arch / CachyOS с KDE Plasma: `python-pyqt6` для Python-версии или `qt6-base qt6-wayland layer-shell-qt` для C++-версии). Для управления дисплеями на KDE нужен пакет `kscreen` (`kscreen-doctor`).

---

### Способ 2: Arch Linux / CachyOS через `PKGBUILD`

#### Ветка `main` (C++ + Qt6)
```bash
git clone --recursive https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui
BUILDDIR=/tmp/makepkg-ps5 makepkg -si
```
*(Использование отдельного `BUILDDIR` для `main` защищает каталог исходников `src/` при очистке сборки `makepkg -c`.)*

#### Ветка `python-pyqt6` (Python + PyQt6)
```bash
git clone --recursive -b python-pyqt6 https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui
makepkg -si
```

> **Про сабмодуль `ps5-linux-tools`:** клонируйте репозиторий с флагом `--recursive` (если забыли — выполните `git submodule update --init --recursive` внутри папки). Если качаете исходники архивом без `git`, берите файлы `*-src.tar.gz` или `*-src.zip` из раздела **Releases** (автоархив GitHub `Source code` оставляет `ps5-linux-tools/` пустым).

---

### Способ 3: Сборка и запуск из исходников

#### Зависимости (Arch Linux / CachyOS)

**Общие (для `ps5overlayd`, `ps5-linux-tools` и Polkit):**
```bash
sudo pacman -S --needed git base-devel pkgconf zlib wayland pango cairo polkit
```

**Для ветки `main` (C++ + Qt6):**
```bash
sudo pacman -S --needed qt6-base qt6-wayland layer-shell-qt
```

**Для ветки `python-pyqt6` (Python + PyQt6):**
```bash
sudo pacman -S --needed python python-pyqt6
```

**Опционально (для полной функциональности в KDE Plasma):**
```bash
sudo pacman -S --needed kscreen mangohud dolphin
```

#### Сборка ветки `main` (C++ + Qt6 + C overlay)

```bash
git clone --recursive https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui

# 1. Сборка ps5-linux-tools, ps5overlayd и ps5tools-qt6 одной командой
./build.sh

# 2. Запуск GUI
./ps5tools-qt6

# Либо сборка AppImage:
./build_appimage.sh
```

#### Сборка ветки `python-pyqt6` (Python + C overlay)

```bash
git clone --recursive -b python-pyqt6 https://github.com/chocop1/ps5-linux-tools-gui.git
cd ps5-linux-tools-gui

# 1. Сборка CLI-утилит ps5_control и m2_init
make -C ps5-linux-tools

# 2. Сборка микро-оверлея ps5overlayd
make -C overlay_c

# 3. Запуск GUI напрямую
python3 ps5tools_gui.py

# Либо сборка AppImage (потребуется curl или wget для appimagetool):
./build_appimage.sh
```

---

## Интеграция с Polkit (при запуске без `makepkg`)

Привилегированные действия (управление вентилятором, Boost, M.2 и systemd) требуют установки `ps5tools-helper` и политики Polkit:

- Нажмите кнопку установки системной интеграции внутри GUI (вкладка сервисов), либо
- Установите вручную:
  ```bash
  sudo install -Dm755 ps5tools-root-helper /usr/local/libexec/ps5tools-helper
  sudo install -Dm644 org.ps5linuxtools.policy /usr/share/polkit-1/actions/org.ps5linuxtools.policy
  ```
Если утилиты `ps5-linux-tools` ещё не установлены в `/usr/local/sbin/`, также установите их:
```bash
sudo ./ps5-linux-tools/install.sh
# и при использовании M.2:
sudo install -Dm755 ps5-linux-tools/m2_init ps5-linux-tools/m2_install.sh ps5-linux-tools/m2_exec.sh ps5-linux-tools/ps5-linux-warm-reboot.sh /usr/local/sbin/
```

---

## Лицензия и атрибуция

- Графический интерфейс (`ps5tools`, `src/`), микро-оверлей `ps5overlayd`, `ps5tools-root-helper`, политика Polkit и сборочные скрипты распространяются по лицензии **GNU General Public License v3.0 or later (GPL-3.0-or-later)** — подробнее в файле [LICENSE](LICENSE).
- Консольные утилиты в `ps5-linux-tools/` принадлежат проекту **[ps5-linux/ps5-linux-tools](https://github.com/ps5-linux/ps5-linux-tools)** и подключаются как внешний `git submodule`.

---

## Контрибьюторы

- **[chocop1](https://github.com/chocop1)** — автор и мейнтейнер проекта
- **Antigravity (Google DeepMind)** — ИИ-ассистент разработки (PyQt6 и C++17/Qt6 GUI, C-оверлей `ps5overlayd` для Wayland, локализация, адаптация тем и сборка пакетов)
