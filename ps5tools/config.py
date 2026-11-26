# -*- coding: utf-8 -*-
"""
Configuration, i18n localization, tool path resolution (with AppImage FUSE staging protection),
and KDE/Qt style & palette management.
"""

import configparser
import hashlib
import os
import shutil
from pathlib import Path

from PyQt6.QtCore import QLocale, QSettings
from PyQt6.QtGui import QColor, QPalette
from PyQt6.QtWidgets import QApplication, QStyleFactory

APP_NAME = 'PS5 Linux Tools'
ORG_NAME = 'ps5linuxtools'
APP_SETTINGS_NAME = 'PS5ToolsGUI'

# Project root (parent of the ps5tools/ package)
PACKAGE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = PACKAGE_DIR.parent

TOOLS_CACHE_DIR = Path.home() / '.local' / 'share' / 'ps5tools' / 'bin'
HELPER_INSTALL_PATH = '/usr/local/libexec/ps5tools-helper'
POLICY_INSTALL_PATH = '/usr/share/polkit-1/actions/org.ps5linuxtools.policy'

# Raw service definitions: (unit, ru_desc, en_desc)
_PS5_SERVICE_DEFS = [
    ('ps5fan.service', 'Контроль вентилятора PS5 (--fan on)', 'PS5 fan control (--fan on)'),
    ('ps5boost.service', 'Режим производительности Boost (--boost on)', 'Performance Boost mode (--boost on)'),
    ('ps5-bt-quiet.service', 'Отключение битого hci и запуск рабочего Bluetooth', 'Disable faulty HCI and start working Bluetooth'),
    ('ps5-bt-sync.service', 'Синхронизация сопряжений DualSense с PS5 OS', 'Sync DualSense pairings with PS5 OS'),
    ('ps5-stage-firmware.service', 'Загрузка прошивки Wi-Fi NXP IW620 с EFI', 'Stage NXP IW620 Wi-Fi firmware from EFI'),
    ('ps5-stage-gpu-firmware.service', 'Загрузка прошивки GPU PS5 Pro (Viola) с EFI', 'Stage PS5 Pro (Viola) GPU firmware from EFI'),
    ('ps5-fan-curve.service', 'Пользовательская кривая вентилятора (ps5_fan)', 'Custom fan curve (ps5_fan)'),
]

ALLOWED_UNITS = {unit for unit, _ru, _en in _PS5_SERVICE_DEFS}

# Active language cache ('ru' or 'en')
_ACTIVE_LANG = 'en'

# Captured initial KDE/Qt platform style and palette before any override
_INITIAL_STYLE_NAME: str = ''
_INITIAL_PALETTE: QPalette | None = None


def _normalize_lang_code(raw: str) -> str:
    """Resolve primary language subtag: 'ru' -> 'ru', anything else -> 'en'."""
    if not raw:
        return ''
    first = raw.split(':', 1)[0].split(',', 1)[0].strip()
    primary = first.split('_', 1)[0].split('-', 1)[0].split('.', 1)[0].lower()
    if not primary or primary in ('c', 'posix'):
        return ''
    return 'ru' if primary == 'ru' else 'en'


def detect_system_language() -> tuple[str, str]:
    """
    Detect system UI language (KDE Plasma -> POSIX env -> QLocale).
    Returns (resolved_lang ['ru'|'en'], source_description).
    """
    plasma_rc = Path.home() / '.config' / 'plasma-localerc'
    if plasma_rc.is_file():
        try:
            cfg = configparser.RawConfigParser()
            cfg.optionxform = str
            cfg.read(plasma_rc, encoding='utf-8')
            for sec, keys in (('Translations', ('Language', 'LANGUAGE')), ('Formats', ('LANG', 'LC_MESSAGES'))):
                if cfg.has_section(sec):
                    for k, v in cfg.items(sec):
                        if k in keys and v.strip():
                            code = _normalize_lang_code(v)
                            if code:
                                return code, f'plasma-localerc ({v.strip()})'
        except Exception:
            pass

    kde_globals = Path.home() / '.config' / 'kdeglobals'
    if kde_globals.is_file():
        try:
            cfg = configparser.RawConfigParser()
            cfg.optionxform = str
            cfg.read(kde_globals, encoding='utf-8')
            if cfg.has_section('Translations'):
                for k, v in cfg.items('Translations'):
                    if k.lower() == 'language' and v.strip():
                        code = _normalize_lang_code(v)
                        if code:
                            return code, f'kdeglobals ({v.strip()})'
        except Exception:
            pass

    for var in ('LANGUAGE', 'LC_ALL', 'LC_MESSAGES', 'LANG'):
        val = os.environ.get(var, '').strip()
        if val:
            code = _normalize_lang_code(val)
            if code:
                return code, f'{var}={val}'

    qloc = QLocale.system().name()
    code = _normalize_lang_code(qloc) or 'en'
    return code, f'QLocale ({qloc})'


def get_language_mode() -> str:
    s = QSettings(ORG_NAME, APP_SETTINGS_NAME)
    mode = str(s.value('app/language', 'system') or 'system').strip().lower()
    return mode if mode in ('system', 'ru', 'en') else 'system'


def resolve_effective_language(mode: str | None = None) -> str:
    if mode is None:
        mode = get_language_mode()
    if mode == 'ru':
        return 'ru'
    if mode == 'en':
        return 'en'
    lang, _src = detect_system_language()
    return lang


def set_active_language(mode: str, persist: bool = True) -> str:
    global _ACTIVE_LANG
    if mode not in ('system', 'ru', 'en'):
        mode = 'system'
    if persist:
        s = QSettings(ORG_NAME, APP_SETTINGS_NAME)
        s.setValue('app/language', mode)
        s.sync()
    _ACTIVE_LANG = resolve_effective_language(mode)
    QLocale.setDefault(QLocale('ru_RU' if _ACTIVE_LANG == 'ru' else 'en_US'))
    return _ACTIVE_LANG


def is_ru() -> bool:
    return _ACTIVE_LANG == 'ru'


def tr(ru: str, en: str) -> str:
    return ru if _ACTIVE_LANG == 'ru' else en


def get_ps5_services() -> list[tuple[str, str]]:
    """Return PS5 systemd service definitions localized for the active language."""
    return [(u, ru if _ACTIVE_LANG == 'ru' else en) for u, ru, en in _PS5_SERVICE_DEFS]


PS5_SERVICES = get_ps5_services()


def _is_fuse_path(path: Path) -> bool:
    """AppImage mounts at /tmp/.mount_*, which root via pkexec cannot access."""
    return str(path).startswith('/tmp/.mount_')


def _stage_to_ext4(src: Path) -> str:
    """
    Copy an executable or script from the AppImage FUSE mount to a regular
    user-writable directory (~/.local/share/ps5tools/bin) so pkexec/root can run it.
    """
    TOOLS_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    dst = TOOLS_CACHE_DIR / src.name
    src_hash = hashlib.sha256(src.read_bytes()).hexdigest()
    need_copy = True
    if dst.is_file():
        try:
            dst_hash = hashlib.sha256(dst.read_bytes()).hexdigest()
            need_copy = (dst_hash != src_hash)
        except OSError:
            need_copy = True
    if need_copy:
        shutil.copy2(src, dst)
    os.chmod(dst, 0o755)
    return str(dst)


def resolve_tool(name: str) -> str:
    """
    Locate a ps5-linux-tools binary, prioritizing system directories and avoiding FUSE issues under pkexec.
    Returns an absolute path or falls back to the bare command name.
    """
    # 1. System directories (always accessible to root under pkexec)
    for base in ('/usr/local/sbin', '/usr/local/bin', '/usr/local/libexec', '/usr/sbin', '/usr/bin'):
        candidate = Path(base) / name
        if candidate.is_file():
            return str(candidate)

    # 2. Directory alongside AppImage / original working directory
    env_dirs = []
    appimage_file = os.environ.get('APPIMAGE')
    if appimage_file:
        env_dirs.append(Path(appimage_file).resolve().parent)
    owd = os.environ.get('OWD')
    if owd:
        env_dirs.append(Path(owd).resolve())

    for base in env_dirs:
        for sub in (base / 'ps5-linux-tools' / name, base / name):
            if sub.is_file() and not _is_fuse_path(sub):
                return str(sub)

    # 3. Local project directory (if outside AppImage FUSE mount)
    local_candidates = [
        PROJECT_ROOT / 'ps5-linux-tools' / name,
        PROJECT_ROOT / name,
    ]
    for sub in local_candidates:
        if sub.is_file():
            if _is_fuse_path(sub):
                return _stage_to_ext4(sub)
            return str(sub)

    # 4. PATH
    found = shutil.which(name)
    if found:
        return found
    return name


def resolve_asset(name: str) -> str | None:
    """Locate a project asset (icon, policy file, etc.) locally or inside AppDir."""
    candidates = [
        PROJECT_ROOT / name,
        PROJECT_ROOT.parent.parent / name,  # when inside AppDir/usr/lib/ps5tools
        Path('/usr/share/icons/hicolor/scalable/apps') / name,
        Path('/usr/share/icons/hicolor/256x256/apps') / name,
        Path('/usr/share/ps5tools') / name,
    ]
    for p in candidates:
        if p.is_file():
            return str(p)
    return None


def is_polkit_helper_installed() -> bool:
    """Check whether the system helper and Polkit policy are installed."""
    return Path(HELPER_INSTALL_PATH).is_file() and Path(POLICY_INSTALL_PATH).is_file()


def get_system_tools_status() -> dict[str, bool]:
    """Check which tools are installed in standard system directories."""
    return {
        'ps5_control': Path('/usr/local/sbin/ps5_control').is_file(),
        'm2_init': Path('/usr/local/sbin/m2_init').is_file(),
        'm2_install.sh': Path('/usr/local/sbin/m2_install.sh').is_file(),
        'm2_exec.sh': Path('/usr/local/sbin/m2_exec.sh').is_file(),
        'ps5-linux-warm-reboot.sh': Path('/usr/local/sbin/ps5-linux-warm-reboot.sh').is_file(),
        'helper': Path(HELPER_INSTALL_PATH).is_file(),
        'policy': Path(POLICY_INSTALL_PATH).is_file(),
    }


# ── Qt Style & Color Scheme Management ─────────────────────────────────────

def _parse_kde_ini(path: Path) -> dict[str, str]:
    """Read color sections and General/KDE settings from kdeglobals or a .colors file."""
    out: dict[str, str] = {}
    if not path.is_file():
        return out
    try:
        cfg = configparser.RawConfigParser()
        cfg.optionxform = str
        cfg.read(path, encoding='utf-8')
        for sec in cfg.sections():
            if sec.startswith('Colors:') or sec in ('General', 'KDE'):
                for k, v in cfg.items(sec):
                    out[f'{sec}.{k}'] = v.strip()
    except Exception:
        pass
    return out


def _color_from_str(val: str | None, fallback: QColor) -> QColor:
    if not val:
        return QColor(fallback)
    val = val.strip()
    if val.startswith('#'):
        c = QColor(val)
        return c if c.isValid() else QColor(fallback)
    parts = [p.strip() for p in val.split(',')]
    if len(parts) >= 3:
        try:
            c = QColor(int(parts[0]), int(parts[1]), int(parts[2]))
            if c.isValid():
                return c
        except ValueError:
            pass
    return QColor(fallback)


def _derive_bevel_and_disabled(pal: QPalette, btn_bg: QColor, win_fg: QColor, text_fg: QColor, btn_fg: QColor, inactive_fg: QColor):
    """Fill 3D bevel shadow/light roles (important for Windows 9x / Oxygen) and Disabled states."""
    is_dark = btn_bg.lightness() < 128
    pal.setColor(QPalette.ColorRole.Light, btn_bg.lighter(165 if is_dark else 135))
    pal.setColor(QPalette.ColorRole.Midlight, btn_bg.lighter(125 if is_dark else 112))
    pal.setColor(QPalette.ColorRole.Dark, btn_bg.darker(160 if is_dark else 145))
    pal.setColor(QPalette.ColorRole.Mid, btn_bg.darker(125 if is_dark else 118))
    pal.setColor(QPalette.ColorRole.Shadow, btn_bg.darker(230 if is_dark else 260))

    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.WindowText, inactive_fg)
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Text, inactive_fg)
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.ButtonText, inactive_fg)
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.Highlight, pal.color(QPalette.ColorRole.Mid))
    pal.setColor(QPalette.ColorGroup.Disabled, QPalette.ColorRole.HighlightedText, win_fg)
    # Keep inactive slightly readable
    pal.setColor(QPalette.ColorGroup.Inactive, QPalette.ColorRole.WindowText, win_fg)
    pal.setColor(QPalette.ColorGroup.Inactive, QPalette.ColorRole.Text, text_fg)
    pal.setColor(QPalette.ColorGroup.Inactive, QPalette.ColorRole.ButtonText, btn_fg)


def _build_palette_from_ini(colors: dict[str, str], base_pal: QPalette | None = None) -> QPalette:
    pal = QPalette(base_pal) if base_pal is not None else QPalette()
    if not colors:
        return pal

    win_bg = _color_from_str(colors.get('Colors:Window.BackgroundNormal'), pal.color(QPalette.ColorRole.Window))
    win_fg = _color_from_str(colors.get('Colors:Window.ForegroundNormal'), pal.color(QPalette.ColorRole.WindowText))
    win_inact = _color_from_str(colors.get('Colors:Window.ForegroundInactive'), QColor('#7a8580' if win_bg.lightness() < 128 else '#6e7377'))

    view_bg = _color_from_str(colors.get('Colors:View.BackgroundNormal'), pal.color(QPalette.ColorRole.Base))
    view_alt = _color_from_str(colors.get('Colors:View.BackgroundAlternate'), view_bg.darker(106))
    view_fg = _color_from_str(colors.get('Colors:View.ForegroundNormal'), win_fg)
    view_inact = _color_from_str(colors.get('Colors:View.ForegroundInactive'), win_inact)
    link_fg = _color_from_str(colors.get('Colors:View.ForegroundLink'), QColor('#2980b9'))
    visit_fg = _color_from_str(colors.get('Colors:View.ForegroundVisited'), QColor('#8e44ad'))

    btn_bg = _color_from_str(colors.get('Colors:Button.BackgroundNormal'), win_bg)
    btn_fg = _color_from_str(colors.get('Colors:Button.ForegroundNormal'), win_fg)

    sel_bg = _color_from_str(colors.get('Colors:Selection.BackgroundNormal'), QColor('#3daee9'))
    sel_fg = _color_from_str(colors.get('Colors:Selection.ForegroundNormal'), QColor('#ffffff'))

    tip_bg = _color_from_str(colors.get('Colors:Tooltip.BackgroundNormal'), win_bg)
    tip_fg = _color_from_str(colors.get('Colors:Tooltip.ForegroundNormal'), win_fg)

    for grp in (QPalette.ColorGroup.Active, QPalette.ColorGroup.Inactive):
        pal.setColor(grp, QPalette.ColorRole.Window, win_bg)
        pal.setColor(grp, QPalette.ColorRole.WindowText, win_fg)
        pal.setColor(grp, QPalette.ColorRole.Base, view_bg)
        pal.setColor(grp, QPalette.ColorRole.AlternateBase, view_alt)
        pal.setColor(grp, QPalette.ColorRole.Text, view_fg)
        pal.setColor(grp, QPalette.ColorRole.PlaceholderText, view_inact)
        pal.setColor(grp, QPalette.ColorRole.Button, btn_bg)
        pal.setColor(grp, QPalette.ColorRole.ButtonText, btn_fg)
        pal.setColor(grp, QPalette.ColorRole.BrightText, QColor('#ffffff') if win_bg.lightness() < 128 else QColor('#000000'))
        pal.setColor(grp, QPalette.ColorRole.Highlight, sel_bg)
        pal.setColor(grp, QPalette.ColorRole.HighlightedText, sel_fg)
        pal.setColor(grp, QPalette.ColorRole.ToolTipBase, tip_bg)
        pal.setColor(grp, QPalette.ColorRole.ToolTipText, tip_fg)
        pal.setColor(grp, QPalette.ColorRole.Link, link_fg)
        pal.setColor(grp, QPalette.ColorRole.LinkVisited, visit_fg)

    _derive_bevel_and_disabled(pal, btn_bg, win_fg, view_fg, btn_fg, win_inact)
    return pal


def _builtin_win9x_scheme() -> tuple[QPalette, dict[str, str]]:
    colors = {
        'Colors:Window.BackgroundNormal': '212,208,200',
        'Colors:Window.ForegroundNormal': '0,0,0',
        'Colors:Window.ForegroundInactive': '90,90,90',
        'Colors:Window.ForegroundPositive': '0,122,0',
        'Colors:Window.ForegroundNeutral': '154,91,0',
        'Colors:Window.ForegroundNegative': '176,0,0',
        'Colors:View.BackgroundNormal': '255,255,255',
        'Colors:View.BackgroundAlternate': '242,242,240',
        'Colors:View.ForegroundNormal': '0,0,0',
        'Colors:View.ForegroundInactive': '110,110,110',
        'Colors:View.ForegroundLink': '0,0,192',
        'Colors:View.ForegroundVisited': '128,0,128',
        'Colors:Button.BackgroundNormal': '212,208,200',
        'Colors:Button.ForegroundNormal': '0,0,0',
        'Colors:Selection.BackgroundNormal': '0,0,128',
        'Colors:Selection.ForegroundNormal': '255,255,255',
        'Colors:Tooltip.BackgroundNormal': '255,255,225',
        'Colors:Tooltip.ForegroundNormal': '0,0,0',
        'General.AccentColor': '0,0,128',
    }
    pal = _build_palette_from_ini(colors)
    return pal, colors


def _builtin_fusion_dark_scheme() -> tuple[QPalette, dict[str, str]]:
    colors = {
        'Colors:Window.BackgroundNormal': '43,43,43',
        'Colors:Window.ForegroundNormal': '238,238,238',
        'Colors:Window.ForegroundInactive': '150,155,160',
        'Colors:Window.ForegroundPositive': '46,204,113',
        'Colors:Window.ForegroundNeutral': '243,156,18',
        'Colors:Window.ForegroundNegative': '231,76,60',
        'Colors:View.BackgroundNormal': '30,30,30',
        'Colors:View.BackgroundAlternate': '38,38,38',
        'Colors:View.ForegroundNormal': '238,238,238',
        'Colors:View.ForegroundInactive': '145,150,155',
        'Colors:View.ForegroundLink': '61,174,233',
        'Colors:View.ForegroundVisited': '155,89,182',
        'Colors:Button.BackgroundNormal': '53,53,53',
        'Colors:Button.ForegroundNormal': '238,238,238',
        'Colors:Selection.BackgroundNormal': '48,140,198',
        'Colors:Selection.ForegroundNormal': '255,255,255',
        'Colors:Tooltip.BackgroundNormal': '35,35,35',
        'Colors:Tooltip.ForegroundNormal': '238,238,238',
        'General.AccentColor': '61,174,233',
    }
    return _build_palette_from_ini(colors), colors


def _builtin_fusion_light_scheme() -> tuple[QPalette, dict[str, str]]:
    colors = {
        'Colors:Window.BackgroundNormal': '239,239,239',
        'Colors:Window.ForegroundNormal': '24,26,28',
        'Colors:Window.ForegroundInactive': '105,112,119',
        'Colors:Window.ForegroundPositive': '28,132,72',
        'Colors:Window.ForegroundNeutral': '180,92,0',
        'Colors:Window.ForegroundNegative': '196,43,58',
        'Colors:View.BackgroundNormal': '255,255,255',
        'Colors:View.BackgroundAlternate': '245,246,248',
        'Colors:View.ForegroundNormal': '24,26,28',
        'Colors:View.ForegroundInactive': '110,115,122',
        'Colors:View.ForegroundLink': '41,128,185',
        'Colors:View.ForegroundVisited': '142,68,173',
        'Colors:Button.BackgroundNormal': '232,233,235',
        'Colors:Button.ForegroundNormal': '24,26,28',
        'Colors:Selection.BackgroundNormal': '48,140,198',
        'Colors:Selection.ForegroundNormal': '255,255,255',
        'Colors:Tooltip.BackgroundNormal': '255,255,235',
        'Colors:Tooltip.ForegroundNormal': '24,26,28',
        'General.AccentColor': '41,128,185',
    }
    return _build_palette_from_ini(colors), colors


def capture_initial_theme(app: QApplication):
    """Capture original KDE/Qt style and palette at startup before custom overrides."""
    global _INITIAL_STYLE_NAME, _INITIAL_PALETTE
    if not _INITIAL_STYLE_NAME:
        _INITIAL_STYLE_NAME = app.style().objectName() if app.style() else ''
    if _INITIAL_PALETTE is None:
        _INITIAL_PALETTE = QPalette(app.palette())


def detect_system_style_name() -> str:
    """Resolve the active system/KDE widget style name."""
    if _INITIAL_STYLE_NAME:
        return _INITIAL_STYLE_NAME
    kde = _parse_kde_ini(Path.home() / '.config' / 'kdeglobals')
    ws = kde.get('KDE.widgetStyle')
    if ws:
        return ws
    lnf = kde.get('KDE.LookAndFeelPackage', '').lower()
    if 'oxygen' in lnf:
        return 'Oxygen'
    if 'union' in lnf:
        return 'Union'
    return 'Breeze'


def available_qt_styles() -> list[tuple[str, str, bool]]:
    """
    Return [(key, display_label, is_available), ...] for selectable Qt styles.
    """
    keys_map = {k.lower(): k for k in QStyleFactory.keys()}
    sys_style = detect_system_style_name()
    sys_disp = sys_style.capitalize() if sys_style else 'KDE'
    entries = [
        ('system', tr(f'Как в системе ({sys_disp})', f'Follow system ({sys_disp})'), True),
        ('Breeze', 'KDE Breeze', 'breeze' in keys_map),
        ('Oxygen', 'KDE Oxygen', 'oxygen' in keys_map),
        ('Union', 'KDE Union', 'union' in keys_map),
        ('Fusion', 'Qt Fusion', 'fusion' in keys_map),
        ('Windows', 'Microsoft Windows 9x', 'windows' in keys_map),
    ]
    return entries


def discover_color_schemes() -> list[tuple[str, str]]:
    """Return [(scheme_id, display_name), ...] including system KDE, built-ins, and .colors files."""
    kde = _parse_kde_ini(Path.home() / '.config' / 'kdeglobals')
    sys_scheme = kde.get('General.ColorScheme') or 'kdeglobals'

    items: list[tuple[str, str]] = [
        ('system', tr(f'Как в системе ({sys_scheme})', f'Follow system ({sys_scheme})')),
        ('auto', tr('Авто (палитра стиля)', 'Auto (style default)')),
        ('builtin:win9x', 'Microsoft Windows 9x Classic'),
        ('builtin:fusion-dark', 'Fusion Dark'),
        ('builtin:fusion-light', 'Fusion Light'),
    ]

    seen_names: set[str] = set()
    for directory in (
        Path.home() / '.local' / 'share' / 'color-schemes',
        Path('/usr/share/color-schemes'),
    ):
        if not directory.is_dir():
            continue
        for p in sorted(directory.glob('*.colors')):
            stem = p.stem
            if stem in seen_names:
                continue
            seen_names.add(stem)
            ini = _parse_kde_ini(p)
            name = ini.get('General.Name[ru]' if is_ru() else 'General.Name') or ini.get('General.Name') or stem
            items.append((f'file:{p}', name))
    return items


def find_color_scheme_by_stem(stem: str) -> str | None:
    """Locate a KDE .colors scheme ID matching a stem (case-insensitive)."""
    for directory in (
        Path.home() / '.local' / 'share' / 'color-schemes',
        Path('/usr/share/color-schemes'),
    ):
        if not directory.is_dir():
            continue
        for p in directory.glob('*.colors'):
            if p.stem.lower() == stem.lower():
                return f'file:{p}'
    return None


def _match_factory_key(target: str) -> str | None:
    keys_map = {k.lower(): k for k in QStyleFactory.keys()}
    return keys_map.get(target.lower())


def get_theme_settings() -> tuple[str, str]:
    s = QSettings(ORG_NAME, APP_SETTINGS_NAME)
    style = str(s.value('app/theme_style', 'system') or 'system').strip()
    scheme = str(s.value('app/color_scheme', 'system') or 'system').strip()
    return style, scheme


def apply_app_appearance(
    app: QApplication,
    theme_obj: 'KDETheme',
    style_mode: str | None = None,
    scheme_mode: str | None = None,
    persist: bool = True,
) -> tuple[str, str]:
    """
    Apply Qt widget style and color palette live, and refresh KDETheme semantic colors.
    Returns (applied_style_mode, applied_scheme_mode).
    """
    if style_mode is None or scheme_mode is None:
        cur_st, cur_sc = get_theme_settings()
        if style_mode is None:
            style_mode = cur_st
        if scheme_mode is None:
            scheme_mode = cur_sc

    if persist:
        s = QSettings(ORG_NAME, APP_SETTINGS_NAME)
        s.setValue('app/theme_style', style_mode)
        s.setValue('app/color_scheme', scheme_mode)
        s.sync()

    # 1. Create and set QStyle
    if style_mode == 'system':
        target_name = detect_system_style_name()
        factory_key = _match_factory_key(target_name) or _match_factory_key('Breeze') or _match_factory_key('Fusion')
    else:
        factory_key = _match_factory_key(style_mode) or _match_factory_key('Breeze') or _match_factory_key('Fusion')

    style_obj = QStyleFactory.create(factory_key) if factory_key else None
    if style_obj is not None:
        app.setStyle(style_obj)

    # 2. Resolve QPalette and semantic colors map
    kde_ini = _parse_kde_ini(Path.home() / '.config' / 'kdeglobals')
    colors_map: dict[str, str] = {}
    pal: QPalette

    if scheme_mode == 'system':
        colors_map = kde_ini
        if kde_ini:
            pal = _build_palette_from_ini(kde_ini, _INITIAL_PALETTE)
        elif _INITIAL_PALETTE is not None:
            pal = QPalette(_INITIAL_PALETTE)
        else:
            pal = app.palette()
    elif scheme_mode == 'auto':
        fk_low = (factory_key or '').lower()
        if fk_low == 'windows':
            pal, colors_map = _builtin_win9x_scheme()
        elif fk_low == 'fusion':
            is_dark_sys = (_INITIAL_PALETTE.color(QPalette.ColorRole.Window).lightness() < 128) if _INITIAL_PALETTE else True
            pal, colors_map = _builtin_fusion_dark_scheme() if is_dark_sys else _builtin_fusion_light_scheme()
        elif fk_low == 'oxygen':
            ox_id = find_color_scheme_by_stem('OxygenDark' if (_INITIAL_PALETTE and _INITIAL_PALETTE.color(QPalette.ColorRole.Window).lightness() < 128) else 'Oxygen')
            if ox_id:
                p = Path(ox_id[len('file:'):])
                colors_map = _parse_kde_ini(p)
                pal = _build_palette_from_ini(colors_map, style_obj.standardPalette() if style_obj else None)
            else:
                colors_map = kde_ini
                pal = _build_palette_from_ini(kde_ini, _INITIAL_PALETTE)
        else:
            colors_map = kde_ini
            if kde_ini:
                pal = _build_palette_from_ini(kde_ini, _INITIAL_PALETTE)
            elif style_obj is not None:
                pal = style_obj.standardPalette()
            else:
                pal = app.palette()
    elif scheme_mode == 'builtin:win9x':
        pal, colors_map = _builtin_win9x_scheme()
    elif scheme_mode == 'builtin:fusion-dark':
        pal, colors_map = _builtin_fusion_dark_scheme()
    elif scheme_mode == 'builtin:fusion-light':
        pal, colors_map = _builtin_fusion_light_scheme()
    elif scheme_mode.startswith('file:'):
        path = Path(scheme_mode[len('file:'):])
        colors_map = _parse_kde_ini(path)
        if colors_map:
            pal = _build_palette_from_ini(colors_map, style_obj.standardPalette() if style_obj else None)
        else:
            colors_map = kde_ini
            pal = QPalette(_INITIAL_PALETTE) if _INITIAL_PALETTE else app.palette()
    else:
        colors_map = kde_ini
        pal = QPalette(_INITIAL_PALETTE) if _INITIAL_PALETTE else app.palette()

    app.setPalette(pal)
    theme_obj.reload(colors_map, pal)
    return style_mode, scheme_mode


class KDETheme:
    """
    Provides semantic colors (positive/negative/neutral/accent/muted) adapted
    for the active KDE/Qt color palette across both dark and light backgrounds.
    """

    def __init__(self):
        self._colors: dict[str, str] = {}
        self._palette: QPalette | None = None
        self.reload()

    def reload(self, colors_override: dict[str, str] | None = None, palette_override: QPalette | None = None):
        if colors_override is not None:
            self._colors = dict(colors_override)
        else:
            self._colors = _parse_kde_ini(Path.home() / '.config' / 'kdeglobals')
        self._palette = QPalette(palette_override) if palette_override is not None else None

    def _active_palette(self) -> QPalette:
        if self._palette is not None:
            return self._palette
        return QApplication.palette()

    @staticmethod
    def _rgb_to_hex(rgb: str, fallback: str) -> str:
        try:
            parts = [int(x.strip()) for x in rgb.split(',')]
            if len(parts) >= 3:
                return f'#{parts[0]:02x}{parts[1]:02x}{parts[2]:02x}'
        except Exception:
            pass
        return fallback

    def _ensure_readable(self, hex_color: str, fallback: str) -> str:
        """
        Keep semantic colors legible on both dark and light window backgrounds.
        """
        c = QColor(hex_color)
        if not c.isValid():
            return fallback
        win_bg = self._active_palette().color(QPalette.ColorRole.Window)
        h, s, l, a = c.getHsl()
        if win_bg.lightness() < 128:
            if l < 130:
                c.setHsl(h, max(s, 140), 155, a)
        else:
            if l > 125:
                c.setHsl(h, max(s, 150), 95, a)
        return c.name()

    def positive(self) -> str:
        raw = self._colors.get('Colors:Window.ForegroundPositive')
        hex_c = self._rgb_to_hex(raw, '#27ae60') if raw else '#27ae60'
        return self._ensure_readable(hex_c, '#2ecc71')

    def negative(self) -> str:
        raw = self._colors.get('Colors:Window.ForegroundNegative')
        hex_c = self._rgb_to_hex(raw, '#da4453') if raw else '#da4453'
        return self._ensure_readable(hex_c, '#e74c3c')

    def neutral(self) -> str:
        raw = self._colors.get('Colors:Window.ForegroundNeutral')
        hex_c = self._rgb_to_hex(raw, '#f67400') if raw else '#f67400'
        return self._ensure_readable(hex_c, '#f39c12')

    def accent(self) -> str:
        raw = (
            self._colors.get('General.AccentColor')
            or self._colors.get('Colors:Window.DecorationFocus')
            or self._colors.get('Colors:Selection.BackgroundNormal')
        )
        if raw:
            hex_c = self._rgb_to_hex(raw, '#3daee9')
        else:
            hex_c = self._active_palette().color(QPalette.ColorRole.Highlight).name()
        return self._ensure_readable(hex_c, '#3daee9')

    def overlay_accent(self) -> str:
        """Accent color guaranteed readable on dark translucent HUD overlay."""
        c = QColor(self.accent())
        if not c.isValid():
            return '#3daee9'
        h, s, l, a = c.getHsl()
        if l < 135:
            c.setHsl(h, max(s, 140), 165, a)
        return c.name()

    def muted(self) -> str:
        raw = self._colors.get('Colors:Window.ForegroundInactive')
        win_bg = self._active_palette().color(QPalette.ColorRole.Window)
        if raw:
            c = _color_from_str(raw, QColor('#8c9691' if win_bg.lightness() < 128 else '#5a5f64'))
            return c.name()
        return '#94a09a' if win_bg.lightness() < 128 else '#555b60'
