# -*- coding: utf-8 -*-
"""
Monitoring tab:
- PS5 model, Linux kernel version, session type (Wayland/X11)
- ps5_fan thermal sensors (MainSoC, Local1..3) and fan PWM
- amdgpu telemetry (temperature, load %, sclk frequency, VRAM, GTT, Runtime PM / D-state)
- CPU (k10temp + MHz clock) and NVMe SSD temperature
- quick overlay controls and MangoHud profile generator
"""

import shutil
from pathlib import Path
from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QDialog, QDialogButtonBox, QGridLayout, QGroupBox, QHBoxLayout,
    QLabel, QMessageBox, QPushButton, QScrollArea, QTextEdit,
    QVBoxLayout, QWidget,
)

from ..config import KDETheme, tr
from ..widgets import TempBar, level_color, temp_color

MANGOHUD_PS5_CONF = """### MangoHud profile for PS5 Linux
fps
frametime
gpu_stats
gpu_temp
gpu_core_clock
gpu_mem_clock
vram
cpu_stats
cpu_temp
cpu_mhz
ram
fan
custom_text=PS5 Linux
toggle_hud=Shift_R+F12
toggle_hud_position=Shift_R+F11
font_size=20
background_alpha=0.55
round_corners=8
"""


class MonitorTab(QScrollArea):
    """Live PS5 hardware sensor monitoring tab."""

    def __init__(
        self,
        theme: KDETheme,
        ps5_info: dict,
        toggle_overlay_cb,
        open_overlay_cfg_cb,
        detach_overlay_cb,
        is_daemon_mode: bool,
        log_cb,
        parent=None,
    ):
        super().__init__(parent)
        self._theme = theme
        self._ps5_info = ps5_info
        self._toggle_overlay = toggle_overlay_cb
        self._open_overlay_cfg = open_overlay_cfg_cb
        self._detach_overlay = detach_overlay_cb
        self._is_daemon_mode = is_daemon_mode
        self._log = log_cb
        self._last_sensors: dict = {}
        self._overlay_visible = False

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        inner = QWidget()
        outer = QVBoxLayout(inner)
        outer.setSpacing(10)
        outer.setContentsMargins(12, 12, 12, 12)
        self.setWidget(inner)

        # ── System summary header + overlay / MangoHud buttons ──────────────
        self._top_box = QGroupBox()
        top_v = QVBoxLayout(self._top_box)

        self._sys_lbl = QLabel()
        self._sys_lbl.setWordWrap(True)
        top_v.addWidget(self._sys_lbl)

        ov_row = QHBoxLayout()
        self.btn_overlay = QPushButton()
        self.btn_overlay.setCheckable(True)
        self.btn_overlay.clicked.connect(self._toggle_overlay)

        self._btn_ov_cfg = QPushButton()
        self._btn_ov_cfg.clicked.connect(self._open_overlay_cfg)

        self._btn_mango = QPushButton()
        self._btn_mango.clicked.connect(self._on_mangohud_config)

        ov_row.addWidget(self.btn_overlay)
        ov_row.addWidget(self._btn_ov_cfg)
        self._btn_detach = None
        if self._is_daemon_mode:
            self._btn_detach = QPushButton()
            self._btn_detach.clicked.connect(self._detach_overlay)
            ov_row.addWidget(self._btn_detach)
        ov_row.addWidget(self._btn_mango)
        ov_row.addStretch()
        top_v.addLayout(ov_row)

        self._mode_lbl = None
        if self._is_daemon_mode:
            self._mode_lbl = QLabel()
            self._mode_lbl.setWordWrap(True)
            top_v.addWidget(self._mode_lbl)
        outer.addWidget(self._top_box)

        # ── PS5 thermal sensors (ps5_fan) ───────────────────────────────────
        self._grp_ps5 = QGroupBox()
        grid_ps5 = QGridLayout(self._grp_ps5)
        grid_ps5.setColumnMinimumWidth(0, 115)
        grid_ps5.setColumnStretch(1, 1)

        self._ps5_rows: dict[str, tuple[QLabel, QLabel, TempBar]] = {}
        for idx, key in enumerate(['ps5fan_temp1', 'ps5fan_temp2', 'ps5fan_temp3', 'ps5fan_temp4']):
            name_lbl = QLabel('—')
            bar = TempBar(100)
            val_lbl = QLabel('—')
            val_lbl.setFixedWidth(72)
            val_lbl.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            grid_ps5.addWidget(name_lbl, idx, 0)
            grid_ps5.addWidget(bar, idx, 1)
            grid_ps5.addWidget(val_lbl, idx, 2)
            self._ps5_rows[key] = (name_lbl, val_lbl, bar)
        outer.addWidget(self._grp_ps5)

        # ── Fan PWM ─────────────────────────────────────────────────────────
        self._grp_fan = QGroupBox()
        fan_v = QVBoxLayout(self._grp_fan)
        fan_row = QHBoxLayout()
        self.lbl_fan_pct = QLabel('—')
        self.lbl_fan_pct.setStyleSheet('font-size: 18px; font-weight: bold;')
        self.lbl_fan_pct.setFixedWidth(60)
        self.bar_fan = TempBar(100)
        self.lbl_fan_meta = QLabel('pwm1: —')
        fan_row.addWidget(self.lbl_fan_pct)
        fan_row.addWidget(self.bar_fan, stretch=1)
        fan_row.addWidget(self.lbl_fan_meta)
        fan_v.addLayout(fan_row)
        outer.addWidget(self._grp_fan)

        # ── GPU (amdgpu) ─────────────────────────────────────────────────────
        self._grp_gpu = QGroupBox('🎮  GPU (amdgpu)')
        g_gpu = QGridLayout(self._grp_gpu)
        g_gpu.setColumnMinimumWidth(0, 115)
        g_gpu.setColumnStretch(1, 1)

        self.bar_gpu_temp = TempBar(105)
        self.lbl_gpu_temp = QLabel('—')
        self.lbl_gpu_temp.setFixedWidth(72)
        self.lbl_gpu_temp.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        g_gpu.addWidget(QLabel('Edge:'), 0, 0)
        g_gpu.addWidget(self.bar_gpu_temp, 0, 1)
        g_gpu.addWidget(self.lbl_gpu_temp, 0, 2)

        self._lbl_gpu_load_cap = QLabel()
        self.bar_gpu_busy = TempBar(100, show_text=True)
        g_gpu.addWidget(self._lbl_gpu_load_cap, 1, 0)
        g_gpu.addWidget(self.bar_gpu_busy, 1, 1, 1, 2)

        self.bar_vram = TempBar(100, show_text=True)
        g_gpu.addWidget(QLabel('VRAM:'), 2, 0)
        g_gpu.addWidget(self.bar_vram, 2, 1, 1, 2)

        self.lbl_gpu_meta = QLabel('sclk: —  |  GTT: —  |  PM: —')
        self.lbl_gpu_meta.setWordWrap(True)
        g_gpu.addWidget(self.lbl_gpu_meta, 3, 0, 1, 3)
        outer.addWidget(self._grp_gpu)

        # ── CPU & NVMe ───────────────────────────────────────────────────────
        self._grp_sys = QGroupBox()
        g_sys = QGridLayout(self._grp_sys)
        g_sys.setColumnMinimumWidth(0, 115)
        g_sys.setColumnStretch(1, 1)

        self.bar_cpu = TempBar(100)
        self.lbl_cpu = QLabel('—')
        self.lbl_cpu.setFixedWidth(130)
        self.lbl_cpu.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        g_sys.addWidget(QLabel('CPU Tctl / MHz:'), 0, 0)
        g_sys.addWidget(self.bar_cpu, 0, 1)
        g_sys.addWidget(self.lbl_cpu, 0, 2)

        self._nvme_rows: dict[str, tuple[QLabel, QLabel, TempBar]] = {}
        for row_i, key in enumerate(['nvme_temp1', 'nvme_temp2'], start=1):
            name_lbl = QLabel('—')
            bar = TempBar(80)
            val_lbl = QLabel('—')
            val_lbl.setFixedWidth(130)
            val_lbl.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            g_sys.addWidget(name_lbl, row_i, 0)
            g_sys.addWidget(bar, row_i, 1)
            g_sys.addWidget(val_lbl, row_i, 2)
            self._nvme_rows[key] = (name_lbl, val_lbl, bar)
        outer.addWidget(self._grp_sys)

        outer.addStretch()
        self.retranslate_ui()
        self.refresh_theme()

    def retranslate_ui(self):
        self._top_box.setTitle(tr('ℹ️  Система и оверлеи', 'ℹ️  System & Overlays'))
        self._sys_lbl.setText(
            f'<b>{self._ps5_info.get("model", "PS5")}</b>  •  '
            f'{tr("Ядро", "Kernel")}: <code>{self._ps5_info.get("kernel", "—")}</code>  •  '
            f'{tr("Сессия", "Session")}: <code>{self._ps5_info.get("session_type", "—")}</code>  •  '
            f'VBIOS: <code>{self._ps5_info.get("vbios", "—")}</code>'
        )
        self.set_overlay_checked(self._overlay_visible)
        self.btn_overlay.setToolTip(
            tr(
                'Запускает/останавливает независимый микро-оверлей <code>ps5overlayd</code> (Wayland LayerOverlay, ~4 МБ RAM).',
                'Starts/stops standalone micro-overlay <code>ps5overlayd</code> (Wayland LayerOverlay, ~4 MB RAM).',
            )
            if self._is_daemon_mode else
            tr(
                'Включает/скрывает плавающий полупрозрачный оверлей мониторинга.',
                'Shows/hides the translucent monitoring overlay.',
            )
        )
        self._btn_ov_cfg.setText(tr('⚙  Настроить оверлей', '⚙  Overlay Settings'))
        self._btn_ov_cfg.setToolTip(tr('Выбор отображаемых датчиков и прозрачности оверлея', 'Configure visible metrics and opacity'))
        self._btn_mango.setText(tr('🎮  Профиль MangoHud', '🎮  MangoHud Profile'))
        self._btn_mango.setToolTip(tr(
            'Создать конфигурацию <code>~/.config/MangoHud/MangoHud.conf</code><br>'
            'для отображения температур, частот, VRAM и FPS в играх (переключатель Shift+F12).',
            'Generate <code>~/.config/MangoHud/MangoHud.conf</code><br>'
            'to show temps, clocks, VRAM, and FPS in games (toggle with Shift+F12).',
        ))
        if self._btn_detach is not None:
            self._btn_detach.setText(tr('🚀  Закрыть GUI (оставить оверлей)', '🚀  Exit GUI (Keep Overlay)'))
            self._btn_detach.setToolTip(tr(
                'Запускает оверлей <code>ps5overlayd</code> и полностью выгружает главное окно из памяти.',
                'Starts <code>ps5overlayd</code> and exits the main GUI to free RAM.',
            ))
        if self._mode_lbl is not None:
            self._mode_lbl.setText(tr(
                f'<span style="color:{self._theme.positive()}">● Гибридный режим активен:</span> '
                'оверлей работает в отдельном нативном процессе <code>ps5overlayd</code> (C + Wayland + PangoCairo).',
                f'<span style="color:{self._theme.positive()}">● Hybrid mode active:</span> '
                'overlay runs in a standalone C process <code>ps5overlayd</code> (Wayland + PangoCairo).',
            ))

        self._grp_ps5.setTitle(tr('🌡️  Датчики платы PS5 (ps5_fan)', '🌡️  PS5 Board Thermal Sensors (ps5_fan)'))
        for _key, (_nl, _vl, bar) in self._ps5_rows.items():
            bar.setToolTip(tr('Текущая температура датчика (0–100 °C)', 'Current sensor temperature (0–100 °C)'))

        self._grp_fan.setTitle(tr('💨  Охлаждение', '💨  Cooling'))
        self.bar_fan.setToolTip(tr('Текущий ШИМ вентилятора (0–100%)', 'Current fan PWM duty (0–100%)'))

        self._lbl_gpu_load_cap.setText(tr('Загрузка GPU:', 'GPU load:'))
        self.bar_vram.setToolTip(tr('Использование выделенной видеопамяти VRAM', 'Dedicated VRAM usage'))
        self._grp_sys.setTitle(tr('🖥️  CPU и NVMe SSD', '🖥️  CPU & NVMe SSD'))

        if self._last_sensors:
            self.update_sensors(self._last_sensors)

    def refresh_theme(self):
        self.lbl_fan_meta.setStyleSheet(f'color: {self._theme.muted()};')
        for _k, (_nl, _vl, bar) in self._ps5_rows.items():
            bar.reset_theme()
        for bar in (self.bar_fan, self.bar_gpu_temp, self.bar_gpu_busy, self.bar_vram, self.bar_cpu):
            bar.reset_theme()
        for _k, (_nl, _vl, bar) in self._nvme_rows.items():
            bar.reset_theme()
        if self._mode_lbl is not None:
            self._mode_lbl.setText(tr(
                f'<span style="color:{self._theme.positive()}">● Гибридный режим активен:</span> '
                'оверлей работает в отдельном нативном процессе <code>ps5overlayd</code> (C + Wayland + PangoCairo).',
                f'<span style="color:{self._theme.positive()}">● Hybrid mode active:</span> '
                'overlay runs in a standalone C process <code>ps5overlayd</code> (Wayland + PangoCairo).',
            ))
        if self._last_sensors:
            self.update_sensors(self._last_sensors)

    def set_overlay_checked(self, visible: bool):
        self._overlay_visible = visible
        self.btn_overlay.setChecked(visible)
        self.btn_overlay.setText(
            tr('📊  Скрыть оверлей', '📊  Hide Overlay')
            if visible else
            tr('📊  Показать оверлей', '📊  Show Overlay')
        )

    def update_sensors(self, data: dict):
        self._last_sensors = data
        na_str = tr('н/д', 'N/A')
        mhz = tr('МГц', 'MHz')
        mb = tr('МБ', 'MB')

        # ps5_fan
        for key, (name_lbl, val_lbl, bar) in self._ps5_rows.items():
            t = data.get(key)
            lbl = data.get(f'{key}_label', '—')
            fault = data.get(f'{key}_fault', False)
            name_lbl.setText(f'{lbl}:')
            if t is not None:
                c = temp_color(t, self._theme)
                val_lbl.setText(f'{t:.1f} °C' + (' ⚠' if fault else ''))
                val_lbl.setStyleSheet(f'color: {c}; font-weight: bold;')
                bar.set_colored_value(round(t), c, 100)
            else:
                val_lbl.setText(na_str)
                val_lbl.setStyleSheet('')

        # Fan
        pct = data.get('fan_pwm_pct')
        raw = data.get('fan_pwm')
        en = data.get('fan_pwm_enable')
        mode_str = {0: 'Full/Off', 1: tr('Ручной', 'Manual'), 2: tr('Авто', 'Auto')}.get(en, '—')
        if pct is not None:
            self.lbl_fan_pct.setText(f'{pct}%')
            self.bar_fan.set_colored_value(pct, level_color(pct / 100, self._theme), 100)
            self.lbl_fan_meta.setText(f'pwm1: {raw}/255 ({mode_str})')
        else:
            self.lbl_fan_pct.setText(na_str)
            self.lbl_fan_meta.setText('pwm1: —')

        # GPU
        gpu_t = data.get('gpu_temp')
        if gpu_t is not None:
            c = temp_color(gpu_t, self._theme, 70, 88)
            self.lbl_gpu_temp.setText(f'{gpu_t:.1f} °C')
            self.lbl_gpu_temp.setStyleSheet(f'color: {c}; font-weight: bold;')
            self.bar_gpu_temp.set_colored_value(round(gpu_t), c, 105)
        else:
            self.lbl_gpu_temp.setText(na_str)
            self.lbl_gpu_temp.setStyleSheet('')

        busy = data.get('gpu_busy')
        if busy is not None:
            c = level_color(busy / 100, self._theme)
            self.bar_gpu_busy.setFormat(f'{busy}%')
            self.bar_gpu_busy.set_colored_value(busy, c, 100)

        v_used = data.get('vram_used_mb')
        v_tot = data.get('vram_total_mb')
        if v_used is not None and v_tot:
            ratio = v_used / v_tot
            c = level_color(ratio, self._theme)
            self.bar_vram.setFormat(f'{v_used} / {v_tot} {mb} ({round(ratio * 100)}%)')
            self.bar_vram.set_colored_value(v_used, c, v_tot)

        freq = data.get('gpu_freq_mhz')
        gtt_u = data.get('gtt_used_mb')
        gtt_t = data.get('gtt_total_mb')
        pm = data.get('gpu_pm', '—')
        pstate = data.get('gpu_power_state', '—')
        freq_s = f'{freq} {mhz}' if freq else na_str
        gtt_s = f'{gtt_u}/{gtt_t} {mb}' if (gtt_u is not None and gtt_t) else '—'
        pm_c = self._theme.positive() if pm == 'active' else self._theme.neutral()
        sclk_cap = tr('Частота sclk', 'sclk clock')
        self.lbl_gpu_meta.setText(
            f'{sclk_cap}: <b>{freq_s}</b>  |  GTT: <b>{gtt_s}</b>  |  '
            f'PM: <span style="color:{pm_c}"><b>{pm}</b></span> ({pstate})'
        )

        # CPU
        cpu_t = data.get('cpu_temp')
        cpu_mhz = data.get('cpu_freq_mhz')
        mhz_s = f' ({cpu_mhz} {mhz})' if cpu_mhz else ''
        if cpu_t is not None:
            c = temp_color(cpu_t, self._theme, 70, 88)
            self.lbl_cpu.setText(f'{cpu_t:.1f} °C{mhz_s}')
            self.lbl_cpu.setStyleSheet(f'color: {c}; font-weight: bold;')
            self.bar_cpu.set_colored_value(round(cpu_t), c, 100)
        else:
            self.lbl_cpu.setText(f'{na_str}{mhz_s}')
            self.lbl_cpu.setStyleSheet('')

        # NVMe
        for key, (name_lbl, val_lbl, bar) in self._nvme_rows.items():
            t = data.get(key)
            lbl = data.get(f'{key}_label', '—')
            name_lbl.setText(f'{lbl}:')
            if t is not None:
                c = temp_color(t, self._theme, 58, 70)
                val_lbl.setText(f'{t:.1f} °C')
                val_lbl.setStyleSheet(f'color: {c}; font-weight: bold;')
                bar.set_colored_value(round(t), c, 80)
            else:
                val_lbl.setText(na_str)
                val_lbl.setStyleSheet('')

    def _on_mangohud_config(self):
        cfg_dir = Path.home() / '.config' / 'MangoHud'
        cfg_file = cfg_dir / 'MangoHud.conf'

        dlg = QDialog(self)
        dlg.setWindowTitle(tr('Генерация профиля MangoHud для PS5', 'Generate MangoHud Profile for PS5'))
        dlg.resize(460, 380)
        v = QVBoxLayout(dlg)
        v.addWidget(QLabel(tr(
            f'Будет записан файл: <code>{cfg_file}</code><br>'
            '(если конфиг уже существует, создастся резервная копия <code>MangoHud.conf.bak</code>)',
            f'File to be written: <code>{cfg_file}</code><br>'
            '(if the file already exists, a backup <code>MangoHud.conf.bak</code> will be created)',
        )))
        edit = QTextEdit()
        edit.setPlainText(MANGOHUD_PS5_CONF)
        v.addWidget(edit)
        btns = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel
        )
        btns.accepted.connect(dlg.accept)
        btns.rejected.connect(dlg.reject)
        v.addWidget(btns)

        if dlg.exec() != QDialog.DialogCode.Accepted:
            return

        try:
            cfg_dir.mkdir(parents=True, exist_ok=True)
            if cfg_file.exists():
                shutil.copy2(cfg_file, cfg_dir / 'MangoHud.conf.bak')
            cfg_file.write_text(edit.toPlainText(), encoding='utf-8')
            self._log(tr(f'✓ Профиль MangoHud сохранён: {cfg_file}', f'✓ MangoHud profile saved: {cfg_file}'))
            QMessageBox.information(
                self,
                tr('MangoHud готов', 'MangoHud Ready'),
                tr(
                    f'Конфигурация сохранена в {cfg_file}.\n'
                    'Для запуска игр с оверлеем используйте mangohud %command%.',
                    f'Configuration saved to {cfg_file}.\n'
                    'Use mangohud %command% to launch games with MangoHud.',
                ),
            )
        except OSError as exc:
            QMessageBox.critical(self, tr('Ошибка сохранения', 'Save Error'), str(exc))
