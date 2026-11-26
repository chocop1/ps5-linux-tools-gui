# -*- coding: utf-8 -*-
"""
Application settings tab:
- Interface language selection (Follow system / English / Russian)
- Qt widget style & KDE/classic theme adaptation
  (Follow system, Breeze, Oxygen, Union, Fusion, Microsoft Windows 9x)
- Color scheme selection (Follow system kdeglobals, style default, .colors schemes, Win9x, Fusion)
- Tray behavior sync & quick reset
"""

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtGui import QPalette
from PyQt6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QGridLayout, QGroupBox,
    QHBoxLayout, QLabel, QPushButton, QScrollArea, QSizePolicy,
    QVBoxLayout, QWidget,
)

from ..config import (
    KDETheme, available_qt_styles, detect_system_language,
    detect_system_style_name, discover_color_schemes, find_color_scheme_by_stem,
    get_language_mode, get_theme_settings, is_ru, resolve_effective_language, tr,
)


class SettingsTab(QScrollArea):
    """Application settings tab for language, widget themes, color palettes, and tray behavior."""

    appearanceChanged = pyqtSignal(str, str, str)  # lang_mode, style_mode, scheme_mode
    minimizeToTrayToggled = pyqtSignal(bool)

    def __init__(self, theme: KDETheme, minimize_to_tray: bool = False, parent=None):
        super().__init__(parent)
        self._theme = theme
        self._updating_combos = False

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setSpacing(12)
        layout.setContentsMargins(12, 12, 12, 12)
        self.setWidget(inner)

        # ── 1. Interface Language ────────────────────────────────────────────
        self._grp_lang = QGroupBox()
        lang_v = QVBoxLayout(self._grp_lang)

        lang_row = QHBoxLayout()
        self._lbl_lang_cap = QLabel()
        self._lbl_lang_cap.setMinimumWidth(145)
        self.cmb_lang = QComboBox()
        self.cmb_lang.currentIndexChanged.connect(self._on_controls_changed)
        lang_row.addWidget(self._lbl_lang_cap)
        lang_row.addWidget(self.cmb_lang, stretch=1)
        lang_v.addLayout(lang_row)

        self._lbl_lang_info = QLabel()
        self._lbl_lang_info.setWordWrap(True)
        lang_v.addWidget(self._lbl_lang_info)
        layout.addWidget(self._grp_lang)

        # ── 2. Theme & Color Adaptation ──────────────────────────────────────
        self._grp_theme = QGroupBox()
        theme_v = QVBoxLayout(self._grp_theme)

        g_theme = QGridLayout()
        g_theme.setColumnMinimumWidth(0, 145)

        self._lbl_style_cap = QLabel()
        self.cmb_style = QComboBox()
        self.cmb_style.currentIndexChanged.connect(self._on_controls_changed)

        self._lbl_scheme_cap = QLabel()
        self.cmb_scheme = QComboBox()
        self.cmb_scheme.currentIndexChanged.connect(self._on_controls_changed)

        g_theme.addWidget(self._lbl_style_cap, 0, 0)
        g_theme.addWidget(self.cmb_style, 0, 1)
        g_theme.addWidget(self._lbl_scheme_cap, 1, 0)
        g_theme.addWidget(self.cmb_scheme, 1, 1)
        theme_v.addLayout(g_theme)

        self._lbl_presets_cap = QLabel()
        theme_v.addWidget(self._lbl_presets_cap)

        presets_row = QHBoxLayout()
        self._preset_btns: list[tuple[QPushButton, tuple[str, str, str, str]]] = []
        preset_specs = [
            ('🖥  Как в системе', '🖥  Follow system', 'system', 'system'),
            ('Breeze', 'Breeze', 'Breeze', 'system'),
            ('Oxygen', 'Oxygen', 'Oxygen', 'auto'),
            ('Union', 'Union', 'Union', 'system'),
            ('Fusion', 'Fusion', 'Fusion', 'builtin:fusion-dark'),
            ('🪟 Windows 9x', '🪟 Windows 9x', 'Windows', 'builtin:win9x'),
        ]
        for ru_t, en_t, st_mode, sc_mode in preset_specs:
            btn = QPushButton()
            btn.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            btn.clicked.connect(lambda _chk=False, s=st_mode, c=sc_mode: self._apply_preset(s, c))
            presets_row.addWidget(btn)
            self._preset_btns.append((btn, (ru_t, en_t, st_mode, sc_mode)))
        theme_v.addLayout(presets_row)

        self._lbl_palette_preview = QLabel()
        self._lbl_palette_preview.setWordWrap(True)
        theme_v.addWidget(self._lbl_palette_preview)

        self._lbl_theme_note = QLabel()
        self._lbl_theme_note.setWordWrap(True)
        theme_v.addWidget(self._lbl_theme_note)
        layout.addWidget(self._grp_theme)

        # ── 3. Behavior & Reset ──────────────────────────────────────────────
        self._grp_beh = QGroupBox()
        beh_v = QVBoxLayout(self._grp_beh)

        self.chk_min_tray = QCheckBox()
        self.chk_min_tray.setChecked(minimize_to_tray)
        self.chk_min_tray.toggled.connect(self.minimizeToTrayToggled.emit)
        beh_v.addWidget(self.chk_min_tray)

        reset_row = QHBoxLayout()
        self._btn_reset = QPushButton()
        self._btn_reset.clicked.connect(self._on_reset_defaults)
        reset_row.addWidget(self._btn_reset)
        reset_row.addStretch()
        beh_v.addLayout(reset_row)
        layout.addWidget(self._grp_beh)

        layout.addStretch()

        self._populate_combos()
        self.retranslate_ui()
        self.refresh_theme()

    def set_minimize_to_tray(self, checked: bool):
        self.chk_min_tray.blockSignals(True)
        self.chk_min_tray.setChecked(checked)
        self.chk_min_tray.blockSignals(False)

    def _populate_combos(self):
        self._updating_combos = True
        lang_mode = get_language_mode()
        style_mode, scheme_mode = get_theme_settings()
        sys_lang, _src = detect_system_language()
        sys_lang_disp = 'Русский' if sys_lang == 'ru' else 'English'

        self.cmb_lang.clear()
        self.cmb_lang.addItem(
            tr(f'Как в системе ({sys_lang_disp})', f'Follow system ({sys_lang_disp})'),
            'system',
        )
        self.cmb_lang.addItem('English', 'en')
        self.cmb_lang.addItem('Русский', 'ru')
        idx = self.cmb_lang.findData(lang_mode)
        self.cmb_lang.setCurrentIndex(idx if idx >= 0 else 0)

        self.cmb_style.clear()
        for key, label, avail in available_qt_styles():
            disp = label if avail else f'{label} ({tr("не установлен", "not installed")})'
            self.cmb_style.addItem(disp, key)
            if not avail:
                model_item = self.cmb_style.model().item(self.cmb_style.count() - 1)
                if model_item is not None:
                    model_item.setEnabled(False)
        idx = self.cmb_style.findData(style_mode)
        self.cmb_style.setCurrentIndex(idx if idx >= 0 else 0)

        self.cmb_scheme.clear()
        for sc_id, sc_label in discover_color_schemes():
            self.cmb_scheme.addItem(sc_label, sc_id)
        idx = self.cmb_scheme.findData(scheme_mode)
        self.cmb_scheme.setCurrentIndex(idx if idx >= 0 else 0)
        self._updating_combos = False

    def _on_controls_changed(self):
        if self._updating_combos:
            return
        lang_mode = self.cmb_lang.currentData() or 'system'
        style_mode = self.cmb_style.currentData() or 'system'
        scheme_mode = self.cmb_scheme.currentData() or 'system'
        self.appearanceChanged.emit(lang_mode, style_mode, scheme_mode)

    def _apply_preset(self, style_mode: str, scheme_mode: str):
        if style_mode == 'Oxygen' and scheme_mode == 'auto':
            # Prefer matching KDE Oxygen scheme when available
            is_dark_sys = QApplication.palette().color(QPalette.ColorRole.Window).lightness() < 128
            ox = find_color_scheme_by_stem('OxygenDark' if is_dark_sys else 'Oxygen')
            if ox:
                scheme_mode = ox
        elif style_mode == 'Breeze':
            is_dark_sys = QApplication.palette().color(QPalette.ColorRole.Window).lightness() < 128
            br = find_color_scheme_by_stem('BreezeDark' if is_dark_sys else 'BreezeLight')
            if br and scheme_mode == 'auto':
                scheme_mode = br
        lang_mode = self.cmb_lang.currentData() or 'system'
        self._updating_combos = True
        st_idx = self.cmb_style.findData(style_mode)
        if st_idx >= 0:
            self.cmb_style.setCurrentIndex(st_idx)
        sc_idx = self.cmb_scheme.findData(scheme_mode)
        if sc_idx >= 0:
            self.cmb_scheme.setCurrentIndex(sc_idx)
        self._updating_combos = False
        self.appearanceChanged.emit(lang_mode, style_mode, scheme_mode)

    def _on_reset_defaults(self):
        self._updating_combos = True
        self.cmb_lang.setCurrentIndex(0)
        self.cmb_style.setCurrentIndex(0)
        self.cmb_scheme.setCurrentIndex(0)
        self._updating_combos = False
        self.appearanceChanged.emit('system', 'system', 'system')

    def retranslate_ui(self):
        self._populate_combos()

        self._grp_lang.setTitle(tr('🌐  Язык интерфейса', '🌐  Interface Language'))
        self._lbl_lang_cap.setText(tr('Язык приложения:', 'Language:'))

        sys_lang, sys_src = detect_system_language()
        eff_lang = resolve_effective_language()
        sys_name = 'Русский' if sys_lang == 'ru' else 'English'
        eff_name = 'Русский' if eff_lang == 'ru' else 'English'
        self._lbl_lang_info.setText(tr(
            f'Определённый язык системы: <b>{sys_name}</b> (источник: <code>{sys_src}</code>)  •  '
            f'Активный язык: <b>{eff_name}</b>.<br>'
            'Правило: если в системе выбран русский — включается русский; для английского и любых других языков по умолчанию ставится английский.',
            f'Detected system language: <b>{sys_name}</b> (source: <code>{sys_src}</code>)  •  '
            f'Active language: <b>{eff_name}</b>.<br>'
            'Rule: Russian system locale defaults to Russian; English and all other locales default to English.',
        ))

        self._grp_theme.setTitle(tr('🎨  Тема оформления и цвета', '🎨  Theme & Color Palette'))
        self._lbl_style_cap.setText(tr('Стиль виджетов (тема):', 'Widget style (theme):'))
        self._lbl_scheme_cap.setText(tr('Цветовая палитра:', 'Color palette:'))
        self._lbl_presets_cap.setText(tr('Быстрые профили оформления:', 'Quick theme presets:'))

        for btn, (ru_t, en_t, _s, _c) in self._preset_btns:
            btn.setText(tr(ru_t, en_t))

        sys_st = detect_system_style_name()
        self._lbl_theme_note.setText(tr(
            f'Системный стиль KDE: <code>{sys_st}</code>. Поддерживаются KDE Breeze, Oxygen, Union, Fusion и классический Microsoft Windows 9x.',
            f'Detected KDE system style: <code>{sys_st}</code>. Supports KDE Breeze, Oxygen, Union, Fusion, and classic Microsoft Windows 9x.',
        ))

        self._grp_beh.setTitle(tr('⚙️  Поведение и сброс', '⚙️  Behavior & Reset'))
        self.chk_min_tray.setText(tr('Сворачивать приложение в системный трей при закрытии окна',
                                     'Minimize application to system tray when closing window'))
        self._btn_reset.setText(tr('↺  Сбросить тему и язык на «Как в системе»',
                                   '↺  Reset theme & language to "Follow system"'))

        self.refresh_theme()

    def refresh_theme(self):
        self._lbl_lang_info.setStyleSheet(f'color: {self._theme.muted()};')
        self._lbl_theme_note.setStyleSheet(f'color: {self._theme.muted()};')

        pal = QApplication.palette()
        win_c = pal.color(QPalette.ColorRole.Window).name()
        base_c = pal.color(QPalette.ColorRole.Base).name()
        acc_c = self._theme.accent()
        pos_c = self._theme.positive()
        neu_c = self._theme.neutral()
        neg_c = self._theme.negative()

        def chip(label: str, color: str) -> str:
            return f'<span style="color:{color}; font-weight:bold;">■ {label}</span>'

        self._lbl_palette_preview.setText(
            tr('Палитра: ', 'Active palette: ')
            + f'<code>Window {win_c}</code>  •  <code>Base {base_c}</code>  •  '
            + '  '.join([
                chip('Accent', acc_c),
                chip(tr('Норма', 'OK'), pos_c),
                chip(tr('Внимание', 'Warn'), neu_c),
                chip(tr('Критично', 'Hot'), neg_c),
            ])
        )
