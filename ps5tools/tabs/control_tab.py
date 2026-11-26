# -*- coding: utf-8 -*-
"""
Cooling / Boost tab:
- ps5_control management (--fan on/off/max, --boost on/off)
- ps5fan.service / ps5boost.service autostart management with confirmation
- live CPU/GPU clock readings to verify Boost behavior
- visual 8-point ps5_fan curve editor + manual PWM control
"""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QCheckBox, QComboBox, QGridLayout, QGroupBox, QHBoxLayout,
    QLabel, QMessageBox, QPushButton, QScrollArea, QSizePolicy,
    QSlider, QSpinBox, QVBoxLayout, QWidget,
)

from ..config import KDETheme, is_polkit_helper_installed, tr
from .. import sysinfo
from ..widgets import FanCurveChart

FAN_PRESETS = [
    (
        'quiet',
        'Тихий (до 70 °C минимум шума)',
        'Quiet (minimal noise below 70 °C)',
        48, [(55, 26), (62, 51), (68, 77), (73, 102), (78, 128), (82, 166), (86, 210), (88, 255)],
    ),
    (
        'balanced',
        'Сбалансированный (близко к стоку)',
        'Balanced (close to stock)',
        45, [(50, 26), (60, 77), (65, 102), (70, 128), (75, 166), (80, 204), (85, 242), (87, 255)],
    ),
    (
        'gaming',
        'Игровой / Холодный',
        'Gaming / Cool',
        40, [(45, 51), (53, 89), (60, 128), (66, 166), (72, 204), (77, 230), (82, 255), (85, 255)],
    ),
]


class ControlTab(QScrollArea):
    """Tab for fan control, Boost mode, and custom cooling curve."""

    def __init__(self, theme: KDETheme, run_action_cb, parent=None):
        super().__init__(parent)
        self._theme = theme
        self._run_action = run_action_cb
        self._action_buttons: list[QWidget] = []
        self._last_statuses: dict = {}
        self._last_sensors: dict = {}
        self._curve_available = True

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setSpacing(12)
        layout.setContentsMargins(12, 12, 12, 12)
        self.setWidget(inner)

        # ── 1. Fan control (ps5_control) ────────────────────────────────────
        self._grp_fan = QGroupBox()
        fan_v = QVBoxLayout(self._grp_fan)

        row_fan = QHBoxLayout()
        self.btn_fan_on = QPushButton('🌀  Fan ON')
        self.btn_fan_off = QPushButton('🔇  Fan OFF')
        self.btn_fan_max = QPushButton('💨  Fan MAX')
        for btn in (self.btn_fan_on, self.btn_fan_off, self.btn_fan_max):
            btn.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            row_fan.addWidget(btn)
            self._action_buttons.append(btn)
        fan_v.addLayout(row_fan)

        fan_srv_row = QHBoxLayout()
        self.lbl_fan_status = QLabel('ps5fan.service: …')
        self.lbl_fan_live = QLabel('PWM: —')
        fan_srv_row.addWidget(self.lbl_fan_status)
        fan_srv_row.addStretch()
        fan_srv_row.addWidget(self.lbl_fan_live)
        fan_v.addLayout(fan_srv_row)

        self.chk_fan_autostart = QCheckBox()
        fan_v.addWidget(self.chk_fan_autostart)
        self._action_buttons.append(self.chk_fan_autostart)
        layout.addWidget(self._grp_fan)

        # ── 2. Boost mode ───────────────────────────────────────────────────
        self._grp_boost = QGroupBox()
        boost_v = QVBoxLayout(self._grp_boost)

        row_boost = QHBoxLayout()
        self.btn_boost_on = QPushButton('⚡  Boost ON')
        self.btn_boost_off = QPushButton('💤  Boost OFF')
        for btn in (self.btn_boost_on, self.btn_boost_off):
            btn.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            row_boost.addWidget(btn)
            self._action_buttons.append(btn)
        boost_v.addLayout(row_boost)

        boost_srv_row = QHBoxLayout()
        self.lbl_boost_status = QLabel('ps5boost.service: …')
        self.lbl_boost_clocks = QLabel('CPU: — | GPU: —')
        boost_srv_row.addWidget(self.lbl_boost_status)
        boost_srv_row.addStretch()
        boost_srv_row.addWidget(self.lbl_boost_clocks)
        boost_v.addLayout(boost_srv_row)

        self.chk_boost_autostart = QCheckBox()
        boost_v.addWidget(self.chk_boost_autostart)
        self._action_buttons.append(self.chk_boost_autostart)
        layout.addWidget(self._grp_boost)

        # ── 3. Fan curve and manual PWM (ps5_fan hwmon) ─────────────────────
        self.grp_curve = QGroupBox()
        curve_v = QVBoxLayout(self.grp_curve)

        # Manual PWM
        manual_row = QHBoxLayout()
        self._lbl_manual_pwm = QLabel()
        manual_row.addWidget(self._lbl_manual_pwm)
        self.sld_pwm = QSlider(Qt.Orientation.Horizontal)
        self.sld_pwm.setRange(0, 100)
        self.sld_pwm.setValue(30)
        self.lbl_pwm_val = QLabel('30%')
        self.lbl_pwm_val.setFixedWidth(40)
        self.btn_pwm_apply = QPushButton()
        self.btn_pwm_auto = QPushButton()
        self.sld_pwm.valueChanged.connect(lambda v: self.lbl_pwm_val.setText(f'{v}%'))
        manual_row.addWidget(self.sld_pwm, stretch=1)
        manual_row.addWidget(self.lbl_pwm_val)
        manual_row.addWidget(self.btn_pwm_apply)
        manual_row.addWidget(self.btn_pwm_auto)
        curve_v.addLayout(manual_row)
        self._action_buttons.extend([self.btn_pwm_apply, self.btn_pwm_auto])

        # Fan curve chart
        self.chart = FanCurveChart(self._theme)
        curve_v.addWidget(self.chart)

        # Presets + hysteresis
        preset_row = QHBoxLayout()
        self._lbl_preset = QLabel()
        preset_row.addWidget(self._lbl_preset)
        self.cmb_preset = QComboBox()
        self._btn_load_preset = QPushButton()
        self._btn_load_preset.clicked.connect(self._on_load_preset)

        preset_row.addWidget(self.cmb_preset, stretch=1)
        preset_row.addWidget(self._btn_load_preset)
        preset_row.addSpacing(12)
        self._lbl_hyst = QLabel()
        preset_row.addWidget(self._lbl_hyst)
        self.spn_hyst = QSpinBox()
        self.spn_hyst.setRange(25, 80)
        self.spn_hyst.setValue(45)
        preset_row.addWidget(self.spn_hyst)
        curve_v.addLayout(preset_row)

        # 8 curve points (2 rows of 4 points for compact layout)
        pts_grid = QGridLayout()
        pts_grid.setSpacing(6)
        self._pt_labels: list[QLabel] = []
        self._pt_spins: list[tuple[QSpinBox, QSpinBox]] = []
        for idx in range(8):
            col = (idx % 4) * 3
            row = (idx // 4) * 2
            lbl = QLabel()
            lbl.setStyleSheet('font-weight: bold; font-size: 10px;')
            spn_t = QSpinBox()
            spn_t.setRange(25, 100)
            spn_t.setSuffix(' °C')
            spn_p = QSpinBox()
            spn_p.setRange(0, 100)
            spn_p.setSuffix(' %')
            spn_t.valueChanged.connect(self._refresh_chart_from_inputs)
            spn_p.valueChanged.connect(self._refresh_chart_from_inputs)
            pts_grid.addWidget(lbl, row, col, 1, 2)
            pts_grid.addWidget(spn_t, row + 1, col)
            pts_grid.addWidget(spn_p, row + 1, col + 1)
            self._pt_labels.append(lbl)
            self._pt_spins.append((spn_t, spn_p))
        curve_v.addLayout(pts_grid)

        # Curve action buttons
        curve_btns = QHBoxLayout()
        self.btn_curve_apply = QPushButton()
        self.btn_curve_reset = QPushButton()
        self.btn_curve_reload = QPushButton()
        self.btn_curve_persist = QPushButton()

        for b in (self.btn_curve_apply, self.btn_curve_reset, self.btn_curve_reload, self.btn_curve_persist):
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            curve_btns.addWidget(b)
            self._action_buttons.append(b)
        curve_v.addLayout(curve_btns)
        layout.addWidget(self.grp_curve)

        layout.addStretch()

        # Signals
        self.btn_fan_on.clicked.connect(lambda: self._run_action('fan', ['on'], 'ps5_control --fan on'))
        self.btn_fan_off.clicked.connect(lambda: self._run_action('fan', ['off'], 'ps5_control --fan off'))
        self.btn_fan_max.clicked.connect(lambda: self._run_action('fan', ['max'], 'ps5_control --fan max'))
        self.btn_boost_on.clicked.connect(lambda: self._run_action('boost', ['on'], 'ps5_control --boost on'))
        self.btn_boost_off.clicked.connect(lambda: self._run_action('boost', ['off'], 'ps5_control --boost off'))

        self.chk_fan_autostart.stateChanged.connect(
            lambda st: self._confirm_autostart('ps5fan.service', st, self.chk_fan_autostart)
        )
        self.chk_boost_autostart.stateChanged.connect(
            lambda st: self._confirm_autostart('ps5boost.service', st, self.chk_boost_autostart)
        )

        self.btn_pwm_apply.clicked.connect(self._on_apply_manual_pwm)
        self.btn_pwm_auto.clicked.connect(
            lambda: self._run_action('fan-auto', [], 'pwm1_enable=2 (auto curve)')
        )
        self.btn_curve_apply.clicked.connect(self._on_apply_curve)
        self.btn_curve_reset.clicked.connect(
            lambda: self._run_action('fan-curve-reset', [], 'curve_reset', on_done=self.load_fan_curve)
        )
        self.btn_curve_reload.clicked.connect(self.load_fan_curve)
        self.btn_curve_persist.clicked.connect(self._on_persist_curve)

        self.retranslate_ui()
        self.load_fan_curve()

    def retranslate_ui(self):
        self._grp_fan.setTitle(tr('🌀  Вентилятор (/dev/icc)', '🌀  Fan Control (/dev/icc)'))
        self.btn_fan_on.setToolTip(tr(
            '<b>Fan ON</b><br>Переводит вентилятор PS5 в штатный режим охлаждения (servo pattern 1).<br>'
            'Выполняет: <code>ps5_control --fan on</code>',
            '<b>Fan ON</b><br>Sets PS5 fan to standard cooling curve mode (servo pattern 1).<br>'
            'Runs: <code>ps5_control --fan on</code>',
        ))
        self.btn_fan_off.setToolTip(tr(
            '<b>Fan OFF</b><br>Передаёт управление обратно базовому контроллеру PS5 (pattern 0).<br>'
            'Выполняет: <code>ps5_control --fan off</code>',
            '<b>Fan OFF</b><br>Hands fan control back to base PS5 controller (pattern 0).<br>'
            'Runs: <code>ps5_control --fan off</code>',
        ))
        self.btn_fan_max.setToolTip(tr(
            '<b>Fan MAX</b><br>Принудительно раскручивает вентилятор на максимум (pattern 3).<br>'
            'Выполняет: <code>ps5_control --fan max</code>',
            '<b>Fan MAX</b><br>Forces maximum fan speed (pattern 3).<br>'
            'Runs: <code>ps5_control --fan max</code>',
        ))
        self.chk_fan_autostart.setText(tr('Автозапуск ps5fan.service при загрузке', 'Autostart ps5fan.service at boot'))
        self.chk_fan_autostart.setToolTip(tr(
            'Включает/отключает службу <code>ps5fan.service</code> в автозапуске systemd.',
            'Enables/disables systemd unit <code>ps5fan.service</code> at boot.',
        ))

        self._grp_boost.setTitle(tr('⚡  Boost режим (/dev/mp1)', '⚡  Boost Mode (/dev/mp1)'))
        self.btn_boost_on.setToolTip(tr(
            '<b>Boost ON</b><br>Активирует повышенные частоты CPU/GPU через ioctl MP1_BOOST_ENTER.<br>'
            'Выполняет: <code>ps5_control --boost on</code>',
            '<b>Boost ON</b><br>Enables boosted CPU/GPU frequencies via MP1_BOOST_ENTER ioctl.<br>'
            'Runs: <code>ps5_control --boost on</code>',
        ))
        self.btn_boost_off.setToolTip(tr(
            '<b>Boost OFF</b><br>Отключает Boost-режим (MP1_BOOST_EXIT), снижая нагрев и энергопотребление.<br>'
            'Выполняет: <code>ps5_control --boost off</code>',
            '<b>Boost OFF</b><br>Disables Boost mode (MP1_BOOST_EXIT), reducing heat and power draw.<br>'
            'Runs: <code>ps5_control --boost off</code>',
        ))
        self.chk_boost_autostart.setText(tr('Автозапуск ps5boost.service при загрузке', 'Autostart ps5boost.service at boot'))
        self.chk_boost_autostart.setToolTip(tr(
            'Включает/отключает службу <code>ps5boost.service</code> в автозапуске systemd.',
            'Enables/disables systemd unit <code>ps5boost.service</code> at boot.',
        ))

        self._update_curve_group_title()
        self._lbl_manual_pwm.setText(tr('Ручной PWM:', 'Manual PWM:'))
        self.sld_pwm.setToolTip(tr('Выберите процент скорости вращения вентилятора (0–100%)', 'Select fan speed percentage (0–100%)'))
        self.btn_pwm_apply.setText(tr('Задать PWM', 'Set PWM'))
        self.btn_pwm_apply.setToolTip(tr(
            '<b>Ручной режим PWM</b><br>Переключает <code>pwm1_enable=1</code> и устанавливает фиксированную скорость.',
            '<b>Manual PWM Mode</b><br>Switches <code>pwm1_enable=1</code> and applies a fixed duty cycle.',
        ))
        self.btn_pwm_auto.setText(tr('Авто (кривая)', 'Auto (curve)'))
        self.btn_pwm_auto.setToolTip(tr(
            '<b>Автоматический режим</b><br>Возвращает управление температурной кривой (<code>pwm1_enable=2</code>).',
            '<b>Automatic Mode</b><br>Returns control to the temperature fan curve (<code>pwm1_enable=2</code>).',
        ))

        self._lbl_preset.setText(tr('Пресет:', 'Preset:'))
        cur_p = self.cmb_preset.currentIndex()
        self.cmb_preset.blockSignals(True)
        self.cmb_preset.clear()
        for pid, ru_n, en_n, _h, _pts in FAN_PRESETS:
            self.cmb_preset.addItem(tr(ru_n, en_n), pid)
        if 0 <= cur_p < self.cmb_preset.count():
            self.cmb_preset.setCurrentIndex(cur_p)
        self.cmb_preset.blockSignals(False)
        self.cmb_preset.setToolTip(tr(
            'Загрузить готовый профиль в редактор точек (для применения нажмите «Применить кривую»)',
            'Load preset into editor fields (click "Apply curve" to write to kernel)',
        ))

        self._btn_load_preset.setText(tr('Загрузить пресет', 'Load preset'))
        self._btn_load_preset.setToolTip(tr(
            'Заполняет таблицу выбранным профилем без немедленной записи в ядро',
            'Populates the curve points from the selected preset without applying immediately',
        ))
        self._lbl_hyst.setText(tr('Гистерезис (°C):', 'Hysteresis (°C):'))
        self.spn_hyst.setToolTip(tr(
            'Температура снижения оборотов для первой точки (<code>pwm1_auto_point1_temp_hyst</code>)',
            'Step-down hysteresis temperature for point 1 (<code>pwm1_auto_point1_temp_hyst</code>)',
        ))

        for idx, lbl in enumerate(self._pt_labels):
            lbl.setText(tr(f'Точка {idx + 1}:', f'Point {idx + 1}:'))
            spn_t, spn_p = self._pt_spins[idx]
            spn_t.setToolTip(tr(f'Порог температуры для точки {idx + 1}', f'Temperature threshold for point {idx + 1}'))
            spn_p.setToolTip(tr(f'Целевой ШИМ вентилятора для точки {idx + 1}', f'Target fan PWM duty for point {idx + 1}'))

        self.btn_curve_apply.setText(tr('✓  Применить кривую', '✓  Apply curve'))
        self.btn_curve_reset.setText(tr('↺  Сбросить (curve_reset)', '↺  Reset (curve_reset)'))
        self.btn_curve_reload.setText(tr('⟳  Прочитать текущую', '⟳  Reload current'))
        self.btn_curve_persist.setText(tr('📌  В автозапуск', '📌  Persist at boot'))

        self.btn_curve_apply.setToolTip(tr(
            '<b>Применить кривую</b><br>Записывает все 8 точек и гистерезис в <code>ps5_fan</code> одним запросом.',
            '<b>Apply Curve</b><br>Writes all 8 points and hysteresis to <code>ps5_fan</code> in one request.',
        ))
        self.btn_curve_reset.setToolTip(tr(
            '<b>Сбросить кривую</b><br>Записывает <code>1</code> в <code>curve_reset</code>, возвращая стоковую таблицу.',
            '<b>Reset Curve</b><br>Writes <code>1</code> to <code>curve_reset</code>, restoring the stock fan table.',
        ))
        self.btn_curve_reload.setToolTip(tr('Перечитать текущую таблицу точек из sysfs', 'Reload current curve table from sysfs'))
        self.btn_curve_persist.setToolTip(tr(
            '<b>Автозапуск кривой</b><br>Создаёт и включает <code>ps5-fan-curve.service</code> с выбранной кривой.<br>'
            'Требует установленного системного helper (см. вкладку «Службы и Ядро»).',
            '<b>Persist Curve at Boot</b><br>Creates and enables <code>ps5-fan-curve.service</code> with this curve.<br>'
            'Requires the system helper (see the "Services & Kernel" tab).',
        ))

        self.refresh_theme()
        if self._last_statuses:
            self.update_service_status(self._last_statuses)
        else:
            self.lbl_fan_status.setText(tr('ps5fan.service: ● проверка…', 'ps5fan.service: ● checking…'))
            self.lbl_boost_status.setText(tr('ps5boost.service: ● проверка…', 'ps5boost.service: ● checking…'))
        if self._last_sensors:
            self.update_live_sensors(self._last_sensors)
        self.chart.update()

    def refresh_theme(self):
        self.lbl_fan_live.setStyleSheet(f'color: {self._theme.muted()};')
        self.lbl_boost_clocks.setStyleSheet(f'color: {self._theme.muted()};')
        if self._last_statuses:
            self.update_service_status(self._last_statuses)
        self.chart.update()

    def _update_curve_group_title(self):
        if self._curve_available:
            self.grp_curve.setTitle(tr(
                '📈  Кривая вентилятора и ручной ШИМ (ps5_fan — ядро 7.3.3)',
                '📈  Fan Curve & Manual PWM (ps5_fan — kernel 7.3.3+)',
            ))
        else:
            self.grp_curve.setTitle(tr(
                '📈  Кривая вентилятора (ps5_fan hwmon не обнаружен)',
                '📈  Fan Curve (ps5_fan hwmon not detected)',
            ))

    def action_widgets(self) -> list[QWidget]:
        return self._action_buttons

    def update_service_status(self, statuses: dict):
        self._last_statuses = statuses
        fan = statuses.get('ps5fan.service', {})
        boost = statuses.get('ps5boost.service', {})

        self._set_srv_label(self.lbl_fan_status, 'ps5fan.service', fan)
        self._set_srv_label(self.lbl_boost_status, 'ps5boost.service', boost)

        self.chk_fan_autostart.blockSignals(True)
        if fan.get('enabled') is not None:
            self.chk_fan_autostart.setChecked(bool(fan['enabled']))
        self.chk_fan_autostart.blockSignals(False)

        self.chk_boost_autostart.blockSignals(True)
        if boost.get('enabled') is not None:
            self.chk_boost_autostart.setChecked(bool(boost['enabled']))
        self.chk_boost_autostart.blockSignals(False)

    def _set_srv_label(self, lbl: QLabel, name: str, info: dict):
        act = info.get('active')
        if act is True:
            color, txt = self._theme.positive(), tr('● активен', '● active')
        elif act is False:
            color, txt = self._theme.negative(), tr('● неактивен', '● inactive')
        else:
            color, txt = self._theme.neutral(), tr('● неизвестно', '● unknown')
        lbl.setText(f'{name}: {txt}')
        lbl.setStyleSheet(f'color: {color}; font-weight: bold;')

    def update_live_sensors(self, data: dict):
        self._last_sensors = data
        pct = data.get('fan_pwm_pct')
        raw = data.get('fan_pwm')
        en = data.get('fan_pwm_enable')
        mode_str = {
            0: 'Full/Off',
            1: tr('Ручной', 'Manual'),
            2: tr('Авто', 'Auto'),
        }.get(en, '—')
        if pct is not None:
            mode_lbl = tr('режим', 'mode')
            self.lbl_fan_live.setText(f'PWM: {pct}% ({raw}/255, {mode_lbl}: {mode_str})')
        cpu_mhz = data.get('cpu_freq_mhz')
        gpu_mhz = data.get('gpu_freq_mhz')
        mhz = tr('МГц', 'MHz')
        cpu_s = f'{cpu_mhz} {mhz}' if cpu_mhz else '—'
        gpu_s = f'{gpu_mhz} {mhz}' if gpu_mhz else '—'
        self.lbl_boost_clocks.setText(f'CPU: {cpu_s}  |  GPU: {gpu_s}')

        soc_t = data.get('ps5fan_temp1')
        self.chart.set_current_temp(soc_t)

    def _confirm_autostart(self, unit: str, state: int, chk: QCheckBox):
        enable = (state == Qt.CheckState.Checked.value)
        verb = tr('включить' if enable else 'отключить', 'enable' if enable else 'disable')
        reply = QMessageBox.question(
            self,
            tr(f'Автозапуск {unit}', f'Autostart {unit}'),
            tr(
                f'Вы действительно хотите {verb} автозапуск {unit} при старте системы?',
                f'Are you sure you want to {verb} {unit} autostart at boot?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply != QMessageBox.StandardButton.Yes:
            chk.blockSignals(True)
            chk.setChecked(not enable)
            chk.blockSignals(False)
            return
        op = 'enable' if enable else 'disable'
        self._run_action('systemctl', [op, unit], f'systemctl {op} {unit}')

    def load_fan_curve(self):
        info = sysinfo.read_fan_curve()
        self._curve_available = bool(info.get('available'))
        self._update_curve_group_title()
        if not self._curve_available:
            self.grp_curve.setEnabled(False)
            return
        self.grp_curve.setEnabled(True)
        self.spn_hyst.blockSignals(True)
        self.spn_hyst.setValue(info.get('hyst_c', 45))
        self.spn_hyst.blockSignals(False)

        points = info['points']
        for (spn_t, spn_p), (t_c, pwm) in zip(self._pt_spins, points):
            spn_t.blockSignals(True)
            spn_p.blockSignals(True)
            spn_t.setValue(t_c)
            spn_p.setValue(round(pwm / 255 * 100))
            spn_t.blockSignals(False)
            spn_p.blockSignals(False)

        pwm_now = info.get('pwm', 0)
        self.sld_pwm.setValue(round(pwm_now / 255 * 100))
        self.chart.set_data(points)

    def _on_load_preset(self):
        pid = self.cmb_preset.currentData()
        match = next((p for p in FAN_PRESETS if p[0] == pid), None)
        if not match:
            return
        _, _ru, _en, hyst_c, pts = match
        self.spn_hyst.setValue(hyst_c)
        for (spn_t, spn_p), (t_c, pwm) in zip(self._pt_spins, pts):
            spn_t.blockSignals(True)
            spn_p.blockSignals(True)
            spn_t.setValue(t_c)
            spn_p.setValue(round(pwm / 255 * 100))
            spn_t.blockSignals(False)
            spn_p.blockSignals(False)
        self._refresh_chart_from_inputs()

    def _collect_curve_args(self) -> list[str] | None:
        hyst_mc = self.spn_hyst.value() * 1000
        args = [str(hyst_mc)]
        prev_t = 0
        for idx, (spn_t, spn_p) in enumerate(self._pt_spins, start=1):
            t_c = spn_t.value()
            if t_c < prev_t:
                QMessageBox.warning(
                    self,
                    tr('Некорректная кривая', 'Invalid fan curve'),
                    tr(
                        f'Температура в точке {idx} ({t_c} °C) не может быть меньше предыдущей ({prev_t} °C).',
                        f'Point {idx} temperature ({t_c} °C) cannot be lower than previous point ({prev_t} °C).',
                    ),
                )
                return None
            prev_t = t_c
            pwm = round(spn_p.value() * 255 / 100)
            args.append(f'{t_c * 1000}:{pwm}')
        return args

    def _refresh_chart_from_inputs(self):
        pts = [
            (spn_t.value(), round(spn_p.value() * 255 / 100))
            for spn_t, spn_p in self._pt_spins
        ]
        self.chart.set_data(pts)

    def _on_apply_manual_pwm(self):
        pct = self.sld_pwm.value()
        pwm = round(pct * 255 / 100)
        self._run_action('fan-pwm', [str(pwm)], f'Manual PWM = {pct}% ({pwm}/255)')

    def _on_apply_curve(self):
        args = self._collect_curve_args()
        if args is None:
            return
        self._run_action('fan-curve', args, tr('Применение кривой ps5_fan', 'Apply ps5_fan curve'), on_done=self.load_fan_curve)

    def _on_persist_curve(self):
        if not is_polkit_helper_installed():
            QMessageBox.information(
                self,
                tr('Требуется системный helper', 'System helper required'),
                tr(
                    'Для создания автозапуска кривой (ps5-fan-curve.service) сначала '
                    'установите системную интеграцию на вкладке «⚙️ Службы и Ядро».',
                    'To persist the fan curve (ps5-fan-curve.service), first install '
                    'system integration in the "⚙️ Services & Kernel" tab.',
                ),
            )
            return
        args = self._collect_curve_args()
        if args is None:
            return
        self._run_action(
            'fan-curve-persist',
            ['enable', *args],
            'Enable ps5-fan-curve.service',
            on_done=self.load_fan_curve,
        )
