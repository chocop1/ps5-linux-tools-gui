# -*- coding: utf-8 -*-
"""
Display / Peripherals tab:
- dynamic list of DRM connectors with real modes and refresh rates from kscreen-doctor -j
- mode, scale, brightness, VRR/HDR switching with a 15-second auto-revert dialog
- PCIe, HDCP SRM, VBIOS, reset_method, and GPU firmware version (fw_version/*) details
- Bluetooth (MAC, rfkill) and Wi-Fi interface states
"""

import subprocess
from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtWidgets import (
    QComboBox, QGridLayout, QGroupBox, QHBoxLayout, QLabel,
    QMessageBox, QPushButton, QScrollArea, QSlider, QVBoxLayout, QWidget,
)

from ..config import KDETheme, tr


class DisplayRevertDialog(QMessageBox):
    """Display mode confirmation dialog with a 15-second auto-revert timer."""

    def __init__(self, summary: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr('Подтвердите настройки дисплея', 'Confirm Display Settings'))
        self.setIcon(QMessageBox.Icon.Question)
        self._summary = summary
        self._seconds = 15
        self.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        self.setDefaultButton(QMessageBox.StandardButton.No)
        self.button(QMessageBox.StandardButton.Yes).setText(tr('Сохранить', 'Keep Changes'))
        self.button(QMessageBox.StandardButton.No).setText(tr('Вернуть как было', 'Revert'))
        self._update_text()

        self._timer = QTimer(self)
        self._timer.setInterval(1000)
        self._timer.timeout.connect(self._tick)
        self._timer.start()

    def _update_text(self):
        self.setText(tr(
            f'Настройки применены:\n{self._summary}\n\n'
            f'Автоматический возврат к прежним параметрам через {self._seconds} с.',
            f'Display settings applied:\n{self._summary}\n\n'
            f'Automatically reverting to previous settings in {self._seconds} s.',
        ))

    def _tick(self):
        self._seconds -= 1
        if self._seconds <= 0:
            self._timer.stop()
            self.reject()
        else:
            self._update_text()

    def done(self, result: int):
        self._timer.stop()
        super().done(result)


class DisplayTab(QScrollArea):
    """Tab for monitoring and configuring displays and wireless peripherals."""

    def __init__(self, theme: KDETheme, refresh_cb, log_cb, parent=None):
        super().__init__(parent)
        self._theme = theme
        self._refresh_cb = refresh_cb
        self._log = log_cb
        self._last_data: dict = {}
        self._last_connectors: dict[str, dict] = {}
        self._connector_widgets: dict[str, dict] = {}
        self._known_mode_signature: tuple = ()

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        inner = QWidget()
        self._outer = QVBoxLayout(inner)
        self._outer.setSpacing(10)
        self._outer.setContentsMargins(12, 12, 12, 12)
        self.setWidget(inner)

        # ── 1. DRM connectors ────────────────────────────────────────────────
        self._grp_drm = QGroupBox()
        self._drm_layout = QVBoxLayout(self._grp_drm)
        self._lbl_drm_empty = QLabel()
        self._drm_layout.addWidget(self._lbl_drm_empty)
        self._outer.addWidget(self._grp_drm)

        # ── 2. Display configuration (kscreen-doctor) ────────────────────────
        self._grp_ctrl = QGroupBox()
        g_ctrl = QGridLayout(self._grp_ctrl)
        g_ctrl.setColumnMinimumWidth(0, 120)

        self.cmb_output = QComboBox()
        self.cmb_output.currentIndexChanged.connect(self._on_output_selected)
        self.cmb_mode = QComboBox()
        self.cmb_scale = QComboBox()
        self.cmb_scale.addItems(['1.0', '1.25', '1.5', '1.75', '2.0'])

        bright_row = QHBoxLayout()
        self.sld_bright = QSlider(Qt.Orientation.Horizontal)
        self.sld_bright.setRange(10, 100)
        self.sld_bright.setValue(100)
        self.lbl_bright = QLabel('100%')
        self.lbl_bright.setFixedWidth(40)
        self.sld_bright.valueChanged.connect(lambda v: self.lbl_bright.setText(f'{v}%'))
        bright_row.addWidget(self.sld_bright, stretch=1)
        bright_row.addWidget(self.lbl_bright)

        self.cmb_vrr = QComboBox()
        self.cmb_hdr = QComboBox()

        self._lbl_out_cap = QLabel()
        self._lbl_mode_cap = QLabel()
        self._lbl_scale_cap = QLabel()
        self._lbl_bright_cap = QLabel()
        g_ctrl.addWidget(self._lbl_out_cap, 0, 0)
        g_ctrl.addWidget(self.cmb_output, 0, 1)
        g_ctrl.addWidget(self._lbl_mode_cap, 1, 0)
        g_ctrl.addWidget(self.cmb_mode, 1, 1)
        g_ctrl.addWidget(self._lbl_scale_cap, 2, 0)
        g_ctrl.addWidget(self.cmb_scale, 2, 1)
        g_ctrl.addWidget(self._lbl_bright_cap, 3, 0)
        g_ctrl.addLayout(bright_row, 3, 1)
        g_ctrl.addWidget(QLabel('VRR / HDR:'), 4, 0)

        vh_row = QHBoxLayout()
        vh_row.addWidget(self.cmb_vrr)
        vh_row.addWidget(self.cmb_hdr)
        g_ctrl.addLayout(vh_row, 4, 1)

        btn_apply_row = QHBoxLayout()
        self.btn_apply_disp = QPushButton()
        self.btn_apply_disp.clicked.connect(self._on_apply_display)
        self._btn_refresh = QPushButton()
        self._btn_refresh.clicked.connect(self._refresh_cb)
        btn_apply_row.addWidget(self.btn_apply_disp, stretch=1)
        btn_apply_row.addWidget(self._btn_refresh)
        g_ctrl.addLayout(btn_apply_row, 5, 0, 1, 2)
        self._outer.addWidget(self._grp_ctrl)

        # ── 3. GPU / PCIe / HDCP / Firmware ──────────────────────────────────
        self._grp_pcie = QGroupBox()
        g_pcie = QGridLayout(self._grp_pcie)
        g_pcie.setColumnMinimumWidth(0, 130)

        self.lbl_pcie = QLabel('—')
        self.lbl_hdcp = QLabel('—')
        self.lbl_vbios = QLabel('—')
        self.lbl_reset = QLabel('—')
        self.lbl_fw = QLabel('—')
        self.lbl_fw.setWordWrap(True)
        self.lbl_fw.setStyleSheet('font-family: monospace; font-size: 10px;')

        self._lbl_pcie_cap = QLabel()
        g_pcie.addWidget(self._lbl_pcie_cap, 0, 0)
        g_pcie.addWidget(self.lbl_pcie, 0, 1)
        g_pcie.addWidget(QLabel('HDCP SRM:'), 1, 0)
        g_pcie.addWidget(self.lbl_hdcp, 1, 1)
        g_pcie.addWidget(QLabel('VBIOS:'), 2, 0)
        g_pcie.addWidget(self.lbl_vbios, 2, 1)
        g_pcie.addWidget(QLabel('GPU Reset:'), 3, 0)
        g_pcie.addWidget(self.lbl_reset, 3, 1)
        g_pcie.addWidget(QLabel('Firmware:'), 4, 0)
        g_pcie.addWidget(self.lbl_fw, 4, 1)
        self._outer.addWidget(self._grp_pcie)

        # ── 4. Bluetooth and Wi-Fi ───────────────────────────────────────────
        grp_bt = QGroupBox('📶  Bluetooth')
        bt_v = QVBoxLayout(grp_bt)
        self.lbl_bt_info = QLabel('…')
        self.lbl_bt_info.setWordWrap(True)
        bt_v.addWidget(self.lbl_bt_info)
        self._outer.addWidget(grp_bt)

        grp_wifi = QGroupBox('📡  Wi-Fi')
        wifi_v = QVBoxLayout(grp_wifi)
        self.lbl_wifi_info = QLabel('…')
        self.lbl_wifi_info.setWordWrap(True)
        wifi_v.addWidget(self.lbl_wifi_info)
        self._outer.addWidget(grp_wifi)

        self._outer.addStretch()
        self.retranslate_ui()

    def retranslate_ui(self):
        self._grp_drm.setTitle(tr('🖥️  Дисплеи (kscreen-doctor + DRM)', '🖥️  Displays (kscreen-doctor + DRM)'))
        if not self._last_connectors:
            self._lbl_drm_empty.setText(tr('Опрос коннекторов…', 'Polling connectors…'))

        self._grp_ctrl.setTitle(tr('🎛️  Настройка вывода (kscreen-doctor)', '🎛️  Output Configuration (kscreen-doctor)'))
        self._lbl_out_cap.setText(tr('Выход:', 'Output:'))
        self._lbl_mode_cap.setText(tr('Режим (Гц):', 'Mode (Hz):'))
        self._lbl_scale_cap.setText(tr('Масштаб:', 'Scale:'))
        self._lbl_bright_cap.setText(tr('Яркость:', 'Brightness:'))
        self.cmb_mode.setToolTip(tr('Выберите разрешение и частоту обновления экрана', 'Select display resolution and refresh rate'))
        self.cmb_scale.setToolTip(tr('Масштаб интерфейса (на Wayland поддерживается дробное масштабирование)', 'UI scale factor (fractional scaling supported on Wayland)'))
        self.cmb_vrr.setToolTip(tr('Режим VRR / FreeSync (если поддерживается монитором)', 'VRR / FreeSync policy (if supported by display)'))
        self.cmb_hdr.setToolTip(tr('Переключение HDR (если поддерживается монитором)', 'HDR toggle (if supported by display)'))

        no_change = tr('(не менять)', '(keep unchanged)')
        for cmb, items in ((self.cmb_vrr, [no_change, 'automatic', 'always', 'never']),
                           (self.cmb_hdr, [no_change, 'enable', 'disable'])):
            cur = max(0, cmb.currentIndex())
            cmb.blockSignals(True)
            cmb.clear()
            cmb.addItems(items)
            cmb.setCurrentIndex(cur if cur < cmb.count() else 0)
            cmb.blockSignals(False)

        self.btn_apply_disp.setText(tr('✓  Применить параметры дисплея (с откатом 15 с)', '✓  Apply Display Settings (15s auto-revert)'))
        self.btn_apply_disp.setToolTip(tr(
            'Применяет выбранный режим через <code>kscreen-doctor</code>.<br>'
            'Если экран погаснет — через 15 секунд настройки вернутся автоматически.',
            'Applies the selected mode via <code>kscreen-doctor</code>.<br>'
            'If signal is lost, settings automatically revert after 15 seconds.',
        ))
        self._btn_refresh.setText(tr('↺  Обновить', '↺  Refresh'))
        self._btn_refresh.setToolTip(tr('Обновить сведения о дисплеях и периферии', 'Refresh display and peripheral info'))

        self._grp_pcie.setTitle(tr('⚡  GPU / PCIe / HDCP / Прошивки (7.3.3)', '⚡  GPU / PCIe / HDCP / Firmware (7.3.3+)'))
        self._lbl_pcie_cap.setText(tr('PCIe линк:', 'PCIe link:'))
        self.lbl_hdcp.setToolTip(tr('Поддержка HDCP (по наличию /sys/class/drm/card*/device/hdcp_srm)', 'HDCP support (via /sys/class/drm/card*/device/hdcp_srm)'))
        self.lbl_reset.setToolTip(tr('Методы аппаратного сброса GPU при зависании (GPU Hang Recovery)', 'Hardware GPU reset methods for GPU Hang Recovery'))
        self.lbl_bt_info.setToolTip(tr(
            'Адаптеры из <code>/sys/class/bluetooth/</code> и состояние <code>rfkill</code>.',
            'Adapters from <code>/sys/class/bluetooth/</code> and <code>rfkill</code> state.',
        ))

        for w in self._connector_widgets.values():
            w['cap_status'].setText(tr('Статус:', 'Status:'))
            w['cap_cur'].setText(tr('Активный режим:', 'Active mode:'))
            w['cap_modes'].setText(tr('Все режимы:', 'All modes:'))

        if self._last_data:
            self.update_display_data(self._last_data)
            self._on_output_selected()
        else:
            self.lbl_bt_info.setText(tr('Опрос…', 'Polling…'))
            self.lbl_wifi_info.setText(tr('Опрос…', 'Polling…'))

    def refresh_theme(self):
        if self._last_data:
            self.update_display_data(self._last_data)

    def _ensure_connector_card(self, drm_name: str) -> dict:
        if drm_name in self._connector_widgets:
            return self._connector_widgets[drm_name]

        self._lbl_drm_empty.hide()
        grp = QGroupBox(drm_name)
        grp.setFlat(True)
        g = QGridLayout(grp)
        g.setColumnMinimumWidth(0, 120)

        cap_status = QLabel(tr('Статус:', 'Status:'))
        cap_cur = QLabel(tr('Активный режим:', 'Active mode:'))
        cap_modes = QLabel(tr('Все режимы:', 'All modes:'))

        lbl_status = QLabel('—')
        lbl_dpms = QLabel('—')
        lbl_current = QLabel('—')
        lbl_vh = QLabel('—')
        lbl_modes = QLabel('—')
        lbl_modes.setWordWrap(True)
        lbl_modes.setStyleSheet('font-size: 10px;')

        g.addWidget(cap_status, 0, 0)
        g.addWidget(lbl_status, 0, 1)
        g.addWidget(QLabel('DPMS:'), 1, 0)
        g.addWidget(lbl_dpms, 1, 1)
        g.addWidget(cap_cur, 2, 0)
        g.addWidget(lbl_current, 2, 1)
        g.addWidget(QLabel('VRR / HDR:'), 3, 0)
        g.addWidget(lbl_vh, 3, 1)
        g.addWidget(cap_modes, 4, 0)
        g.addWidget(lbl_modes, 4, 1)

        self._drm_layout.addWidget(grp)
        widgets = {
            'grp': grp,
            'cap_status': cap_status,
            'cap_cur': cap_cur,
            'cap_modes': cap_modes,
            'status': lbl_status,
            'dpms': lbl_dpms,
            'current': lbl_current,
            'vh': lbl_vh,
            'modes': lbl_modes,
        }
        self._connector_widgets[drm_name] = widgets
        return widgets

    def update_display_data(self, data: dict):
        self._last_data = data
        connectors = data.get('connectors', {})
        self._last_connectors = connectors

        if not connectors:
            self._lbl_drm_empty.setText(tr('Коннекторы DRM не обнаружены', 'No DRM connectors detected'))
            self._lbl_drm_empty.show()

        for drm_name, info in connectors.items():
            w = self._ensure_connector_card(drm_name)
            st = info.get('status', 'unknown')
            dpms = info.get('dpms', '—')
            current = info.get('current', '—')
            vrr = info.get('vrr', '—')
            hdr = info.get('hdr', '—')
            modes = info.get('modes', [])

            st_c = self._theme.positive() if st == 'connected' else self._theme.muted()
            w['status'].setText(st)
            w['status'].setStyleSheet(f'color: {st_c}; font-weight: bold;')

            dpms_c = self._theme.positive() if dpms == 'On' else self._theme.neutral()
            w['dpms'].setText(dpms)
            w['dpms'].setStyleSheet(f'color: {dpms_c};')

            w['current'].setText(current)
            w['current'].setStyleSheet(f'color: {self._theme.accent()}; font-weight: bold;')

            vrr_c = self._theme.positive() if vrr not in ('incapable', '—', 'never', '') else self._theme.muted()
            hdr_c = self._theme.positive() if hdr not in ('incapable', '—', 'disabled', '') else self._theme.muted()
            w['vh'].setText(
                f'VRR: <span style="color:{vrr_c}">{vrr}</span>  |  '
                f'HDR: <span style="color:{hdr_c}">{hdr}</span>'
            )

            modes_txt = '   '.join(
                (f"<b>{m['label']} ✓</b>" if m.get('active') else m['label'])
                for m in modes[:14]
            )
            w['modes'].setText(modes_txt or '—')

        # Only rebuild control comboboxes when the output/mode set has changed
        connected = [
            (drm, inf) for drm, inf in connectors.items() if inf.get('connected')
        ]
        sig = tuple(
            (drm, inf.get('current_mode_id'), tuple(m['id'] for m in inf.get('modes', [])))
            for drm, inf in connected
        )
        can_ctrl = data.get('kscreen_available', False) and bool(connected)
        self._grp_ctrl.setEnabled(can_ctrl)
        if sig != self._known_mode_signature:
            self._known_mode_signature = sig
            prev_out = self.cmb_output.currentData()
            self.cmb_output.blockSignals(True)
            self.cmb_output.clear()
            for drm, inf in connected:
                self.cmb_output.addItem(f"{inf.get('short_name', drm)} ({inf.get('current', '—')})", drm)
            idx = self.cmb_output.findData(prev_out)
            if idx >= 0:
                self.cmb_output.setCurrentIndex(idx)
            self.cmb_output.blockSignals(False)
            self._on_output_selected()

        # GPU / PCIe / Firmware
        speed = data.get('pcie_speed', '—')
        width = data.get('pcie_width', '—')
        self.lbl_pcie.setText(f'{speed} × {width} lanes')

        hdcp = data.get('hdcp_supported', False)
        self.lbl_hdcp.setText(tr('✓ поддерживается', '✓ supported') if hdcp else tr('✗ не обнаружен', '✗ not detected'))
        self.lbl_hdcp.setStyleSheet(
            f'color: {self._theme.positive() if hdcp else self._theme.muted()};'
        )
        self.lbl_vbios.setText(data.get('vbios_version', '—'))
        self.lbl_reset.setText(data.get('reset_method', '—'))
        fws = data.get('fw_versions', {})
        if fws:
            fw_str = '  '.join(f'{k.replace("_fw_version", "")}:{v}' for k, v in fws.items())
            self.lbl_fw.setText(fw_str)
        else:
            self.lbl_fw.setText('—')

        # Bluetooth (always reset label style)
        bt = data.get('bluetooth', [])
        if bt:
            lines = []
            for dev in bt:
                sb = dev.get('soft_blocked')
                hb = dev.get('hard_blocked')
                if hb:
                    rf_txt = f'<span style="color:{self._theme.negative()}">hard-blocked</span>'
                elif sb:
                    rf_txt = f'<span style="color:{self._theme.neutral()}">soft-blocked</span>'
                else:
                    rf_txt = f'<span style="color:{self._theme.positive()}">{tr("активен", "active")}</span>'
                lines.append(f'<b>{dev["name"]}</b> — {dev["address"]} ({rf_txt})')
            self.lbl_bt_info.setStyleSheet('')
            self.lbl_bt_info.setText('<br>'.join(lines))
        else:
            self.lbl_bt_info.setStyleSheet(f'color: {self._theme.muted()};')
            self.lbl_bt_info.setText(tr('Bluetooth-адаптеры не обнаружены', 'No Bluetooth adapters detected'))

        # Wi-Fi (always reset label style)
        wifi = data.get('wifi', [])
        if wifi:
            lines = []
            for w in wifi:
                st = w['state']
                c = self._theme.positive() if st == 'up' else self._theme.muted()
                lines.append(f'<b>{w["iface"]}</b>: <span style="color:{c}">{st}</span>')
            self.lbl_wifi_info.setStyleSheet('')
            self.lbl_wifi_info.setText('<br>'.join(lines))
        else:
            self.lbl_wifi_info.setStyleSheet(f'color: {self._theme.muted()};')
            self.lbl_wifi_info.setText(tr('Wi-Fi адаптеры не обнаружены', 'No Wi-Fi adapters detected'))

    def _on_output_selected(self):
        drm_key = self.cmb_output.currentData()
        info = self._last_connectors.get(drm_key)
        self.cmb_mode.clear()
        if not info:
            return
        cur_tag = tr(' [текущий]', ' [current]')
        pref_tag = tr(' [рек.]', ' [pref.]')
        for m in info.get('modes', []):
            suffix = cur_tag if m.get('active') else (pref_tag if m.get('preferred') else '')
            self.cmb_mode.addItem(f"{m['label']}{suffix}", m['id'])
            if m.get('active'):
                self.cmb_mode.setCurrentIndex(self.cmb_mode.count() - 1)

        scale_str = f"{info.get('scale', 1.0):.2g}"
        if '.' not in scale_str:
            scale_str += '.0'
        s_idx = self.cmb_scale.findText(scale_str)
        if s_idx >= 0:
            self.cmb_scale.setCurrentIndex(s_idx)

        bright_pct = max(10, min(100, round(info.get('brightness', 1.0) * 100)))
        self.sld_bright.setValue(bright_pct)

        vrr_supported = info.get('vrr', 'incapable') not in ('incapable', '—')
        hdr_supported = info.get('hdr', 'incapable') not in ('incapable', '—')
        self.cmb_vrr.setEnabled(vrr_supported)
        self.cmb_hdr.setEnabled(hdr_supported)

    def _on_apply_display(self):
        drm_key = self.cmb_output.currentData()
        info = self._last_connectors.get(drm_key)
        if not info:
            return

        out_name = info.get('short_name', info.get('name', ''))
        old_mode_id = info.get('current_mode_id', '')
        old_scale = info.get('scale', 1.0)
        old_bright = round(info.get('brightness', 1.0) * 100)

        new_mode_id = self.cmb_mode.currentData()
        new_mode_label = self.cmb_mode.currentText()
        new_scale = self.cmb_scale.currentText()
        new_bright = self.sld_bright.value()

        args = [
            f'output.{out_name}.mode.{new_mode_id}',
            f'output.{out_name}.scale.{new_scale}',
            f'output.{out_name}.brightness.{new_bright}',
        ]
        if self.cmb_vrr.isEnabled() and self.cmb_vrr.currentIndex() > 0:
            args.append(f'output.{out_name}.vrrpolicy.{self.cmb_vrr.currentText()}')
        if self.cmb_hdr.isEnabled() and self.cmb_hdr.currentIndex() > 0:
            args.append(f'output.{out_name}.hdr.{self.cmb_hdr.currentText()}')

        self._log(f'▶ kscreen-doctor {" ".join(args)}')
        r = subprocess.run(['kscreen-doctor', *args], capture_output=True, text=True, timeout=6)
        if r.returncode != 0:
            QMessageBox.warning(
                self,
                tr('Ошибка kscreen-doctor', 'kscreen-doctor Error'),
                r.stderr.strip() or r.stdout.strip() or tr('Не удалось применить настройки экрана.', 'Failed to apply display settings.'),
            )
            self._log(f'✗ kscreen-doctor error: {r.stderr.strip()}')
            return

        dlg = DisplayRevertDialog(
            tr(
                f'Выход: {out_name}\nРежим: {new_mode_label}\nМасштаб: {new_scale}x, Яркость: {new_bright}%',
                f'Output: {out_name}\nMode: {new_mode_label}\nScale: {new_scale}x, Brightness: {new_bright}%',
            ),
            self,
        )
        if dlg.exec() != QMessageBox.DialogCode.Yes:
            revert_args = []
            if old_mode_id:
                revert_args.append(f'output.{out_name}.mode.{old_mode_id}')
            revert_args.append(f'output.{out_name}.scale.{old_scale}')
            revert_args.append(f'output.{out_name}.brightness.{old_bright}')
            subprocess.run(['kscreen-doctor', *revert_args], capture_output=True, text=True, timeout=6)
            self._log(tr('↺ Параметры дисплея возвращены к предыдущим', '↺ Reverted display settings to previous values'))
        else:
            self._log(tr('✓ Новые параметры дисплея сохранены', '✓ Display settings saved'))
        self._refresh_cb()
