# -*- coding: utf-8 -*-
"""
Services & Kernel tab:
- management of all PS5 systemd units (fan, boost, bt-quiet, bt-sync, stage-firmware, stage-gpu-firmware, fan-curve)
- one-click Polkit system integration installer (auth_admin_keep) + sbin utilities
- Linux 7.3.3+ kernel diagnostics (journalctl -k with filters + per-unit journalctl -u)
"""

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (
    QApplication, QComboBox, QGroupBox, QHBoxLayout, QHeaderView,
    QLabel, QMessageBox, QPushButton, QSizePolicy, QSplitter,
    QTableWidget, QTableWidgetItem, QTextEdit, QVBoxLayout, QWidget,
)

from ..config import (
    KDETheme, get_ps5_services, get_system_tools_status,
    is_polkit_helper_installed, tr,
)
from ..runners import JournalPoller

KERNEL_FILTERS = [
    ('PS5 / GPU / HDMI / BT / Wi-Fi', 'PS5 / GPU / HDMI / BT / Wi-Fi', 'amdgpu|drm|hdmi|verde|flava|hdcp|ps5|icc|mp1|bluetooth|iw620|mt7921|wlan'),
    ('Только ошибки ядра (err+)', 'Kernel errors only (err+)', '__ERR__'),
    ('GPU и HDMI (amdgpu / drm / reset)', 'GPU & HDMI (amdgpu / drm / reset)', 'amdgpu|drm|hdmi|verde|flava|hdcp|reset'),
    ('Bluetooth и Wi-Fi', 'Bluetooth & Wi-Fi', 'bluetooth|hci|wlan|moal|iw620|mt7921'),
    ('PS5 Fan / ICC / MP1', 'PS5 Fan / ICC / MP1', 'ps5|icc|mp1|fan'),
    ('Все сообщения ядра (последние 200)', 'All kernel messages (last 200)', ''),
]


class ServicesTab(QWidget):
    """Tab for PS5 systemd services, Polkit integration, and kernel diagnostics."""

    def __init__(
        self,
        theme: KDETheme,
        ps5_info: dict,
        run_action_cb,
        install_integration_cb,
        refresh_statuses_cb,
        parent=None,
    ):
        super().__init__(parent)
        self._theme = theme
        self._ps5_info = ps5_info
        self._run_action = run_action_cb
        self._install_integration = install_integration_cb
        self._refresh_statuses = refresh_statuses_cb
        self._pollers: list[JournalPoller] = []
        self._action_buttons: list[QWidget] = []
        self._last_statuses: dict = {}

        layout = QVBoxLayout(self)
        layout.setSpacing(10)
        layout.setContentsMargins(12, 12, 12, 12)

        splitter = QSplitter(Qt.Orientation.Vertical)
        layout.addWidget(splitter)

        # ── Top section: Services + Polkit ──────────────────────────────────
        top_w = QWidget()
        top_v = QVBoxLayout(top_w)
        top_v.setContentsMargins(0, 0, 0, 0)
        top_v.setSpacing(8)

        # Polkit integration
        self._grp_polkit = QGroupBox()
        pk_h = QHBoxLayout(self._grp_polkit)
        self.lbl_polkit = QLabel('…')
        self.lbl_polkit.setWordWrap(True)
        self.btn_install_polkit = QPushButton()
        self.btn_install_polkit.clicked.connect(self._on_install_integration)
        self._action_buttons.append(self.btn_install_polkit)
        pk_h.addWidget(self.lbl_polkit, stretch=1)
        pk_h.addWidget(self.btn_install_polkit)
        top_v.addWidget(self._grp_polkit)

        # Services table
        self._grp_srv = QGroupBox()
        srv_v = QVBoxLayout(self._grp_srv)

        svcs = get_ps5_services()
        self.tbl_services = QTableWidget(len(svcs), 4)
        self.tbl_services.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        self.tbl_services.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        self.tbl_services.horizontalHeader().setSectionResizeMode(2, QHeaderView.ResizeMode.ResizeToContents)
        self.tbl_services.horizontalHeader().setSectionResizeMode(3, QHeaderView.ResizeMode.Stretch)
        self.tbl_services.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.tbl_services.setSelectionMode(QTableWidget.SelectionMode.SingleSelection)
        self.tbl_services.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.tbl_services.verticalHeader().setVisible(False)
        for row_i, (unit, desc) in enumerate(svcs):
            self.tbl_services.setItem(row_i, 0, QTableWidgetItem(unit))
            self.tbl_services.setItem(row_i, 1, QTableWidgetItem('…'))
            self.tbl_services.setItem(row_i, 2, QTableWidgetItem('…'))
            self.tbl_services.setItem(row_i, 3, QTableWidgetItem(desc))
        self.tbl_services.selectRow(0)
        srv_v.addWidget(self.tbl_services)

        srv_btns = QHBoxLayout()
        self.btn_srv_restart = QPushButton()
        self.btn_srv_stop = QPushButton()
        self.btn_srv_enable = QPushButton()
        self.btn_srv_disable = QPushButton()
        self.btn_srv_log = QPushButton()
        self._btn_srv_refresh = QPushButton('↺')
        self._btn_srv_refresh.setFixedWidth(34)
        self._btn_srv_refresh.clicked.connect(self._refresh_statuses)

        for b in (self.btn_srv_restart, self.btn_srv_stop, self.btn_srv_enable, self.btn_srv_disable):
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            srv_btns.addWidget(b)
            self._action_buttons.append(b)
        srv_btns.addWidget(self.btn_srv_log)
        srv_btns.addWidget(self._btn_srv_refresh)
        srv_v.addLayout(srv_btns)
        top_v.addWidget(self._grp_srv)

        splitter.addWidget(top_w)

        # ── Bottom section: Kernel diagnostics (journalctl) ──────────────────
        self._grp_diag = QGroupBox()
        diag_v = QVBoxLayout(self._grp_diag)

        flt_row = QHBoxLayout()
        self._lbl_flt = QLabel()
        flt_row.addWidget(self._lbl_flt)
        self.cmb_filter = QComboBox()
        self.cmb_filter.currentIndexChanged.connect(self.refresh_journal)

        self._btn_diag_refresh = QPushButton()
        self._btn_diag_refresh.clicked.connect(self.refresh_journal)
        self._btn_copy_report = QPushButton()
        self._btn_copy_report.clicked.connect(self._on_copy_report)

        flt_row.addWidget(self.cmb_filter, stretch=1)
        flt_row.addWidget(self._btn_diag_refresh)
        flt_row.addWidget(self._btn_copy_report)
        diag_v.addLayout(flt_row)

        self.txt_journal = QTextEdit()
        self.txt_journal.setReadOnly(True)
        self.txt_journal.setFont(QFont('Monospace', 9))
        self.txt_journal.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        diag_v.addWidget(self.txt_journal)

        splitter.addWidget(self._grp_diag)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 1)

        # Service button signals
        self.btn_srv_restart.clicked.connect(lambda: self._act_selected('restart'))
        self.btn_srv_stop.clicked.connect(lambda: self._act_selected('stop'))
        self.btn_srv_enable.clicked.connect(lambda: self._act_selected('enable'))
        self.btn_srv_disable.clicked.connect(lambda: self._act_selected('disable'))
        self.btn_srv_log.clicked.connect(self._on_view_unit_log)

        self.retranslate_ui()
        self.refresh_integration_status()

    def retranslate_ui(self):
        self._grp_polkit.setTitle(tr('🔐  Системная интеграция и Polkit (auth_admin_keep)', '🔐  System Integration & Polkit (auth_admin_keep)'))
        self.btn_install_polkit.setToolTip(tr(
            '<b>Установить системную интеграцию</b><br>'
            'Копирует <code>ps5tools-helper</code> в <code>/usr/local/libexec</code>,<br>'
            'политику <code>org.ps5linuxtools.policy</code> (кэш пароля на 5 мин)<br>'
            'и недостающие утилиты M.2 в <code>/usr/local/sbin</code>.',
            '<b>Install System Integration</b><br>'
            'Installs <code>ps5tools-helper</code> to <code>/usr/local/libexec</code>,<br>'
            '<code>org.ps5linuxtools.policy</code> (5 min password cache),<br>'
            'and missing M.2 utilities to <code>/usr/local/sbin</code>.',
        ))

        self._grp_srv.setTitle(tr('⚙️  Системные службы PS5', '⚙️  PS5 Systemd Services'))
        self.tbl_services.setHorizontalHeaderLabels([
            tr('Служба', 'Service'),
            tr('Состояние', 'State'),
            tr('Автозапуск', 'Autostart'),
            tr('Назначение', 'Description'),
        ])
        for row_i, (_unit, desc) in enumerate(get_ps5_services()):
            self.tbl_services.setItem(row_i, 3, QTableWidgetItem(desc))

        self.btn_srv_restart.setText(tr('▶  Запуск / Перезапуск', '▶  Start / Restart'))
        self.btn_srv_stop.setText(tr('■  Стоп', '■  Stop'))
        self.btn_srv_enable.setText(tr('✓  Вкл. автозапуск', '✓  Enable Autostart'))
        self.btn_srv_disable.setText(tr('✗  Откл. автозапуск', '✗  Disable Autostart'))
        self.btn_srv_log.setText(tr('📄  Лог службы', '📄  Unit Log'))
        self._btn_srv_refresh.setToolTip(tr('Обновить статусы служб', 'Refresh service states'))

        self._grp_diag.setTitle(tr('🩺  Диагностика ядра Linux / служб (journalctl)', '🩺  Linux Kernel / Unit Diagnostics (journalctl)'))
        self._lbl_flt.setText(tr('Фильтр:', 'Filter:'))

        cur_idx = self.cmb_filter.currentIndex()
        self.cmb_filter.blockSignals(True)
        self.cmb_filter.clear()
        for ru_t, en_t, rx in KERNEL_FILTERS:
            self.cmb_filter.addItem(tr(ru_t, en_t), rx)
        self.cmb_filter.setCurrentIndex(cur_idx if cur_idx >= 0 else 0)
        self.cmb_filter.blockSignals(False)

        self._btn_diag_refresh.setText(tr('⟳  Прочитать журнал', '⟳  Read Journal'))
        self._btn_copy_report.setText(tr('📋  Копировать отчёт', '📋  Copy Report'))
        self._btn_copy_report.setToolTip(tr('Копирует сведения о системе и журнал в буфер обмена', 'Copies system info and journal log to clipboard'))

        self.refresh_theme()

    def refresh_theme(self):
        self.refresh_integration_status()
        if self._last_statuses:
            self.update_statuses(self._last_statuses)

    def action_widgets(self) -> list[QWidget]:
        return self._action_buttons

    def refresh_integration_status(self):
        st = get_system_tools_status()
        polkit_ok = is_polkit_helper_installed()
        missing_sbin = [k for k in ('ps5_control', 'm2_init', 'm2_install.sh', 'm2_exec.sh', 'ps5-linux-warm-reboot.sh') if not st.get(k)]
        if polkit_ok and not missing_sbin:
            self.lbl_polkit.setText(tr(
                f'<span style="color:{self._theme.positive()}"><b>✓ Установлена</b></span> '
                '(helper + polkit <code>auth_admin_keep</code> + все утилиты в <code>/usr/local/sbin</code>)',
                f'<span style="color:{self._theme.positive()}"><b>✓ Installed</b></span> '
                '(helper + polkit <code>auth_admin_keep</code> + all tools in <code>/usr/local/sbin</code>)',
            ))
            self.btn_install_polkit.setText(tr('🔐  Переустановить / Обновить интеграцию', '🔐  Reinstall / Update Integration'))
        else:
            details = []
            if not polkit_ok:
                details.append(tr('нет helper/polkit', 'missing helper/polkit'))
            if missing_sbin:
                details.append(tr('в /usr/local/sbin отсутствуют: ', 'missing in /usr/local/sbin: ') + ', '.join(missing_sbin))
            self.lbl_polkit.setText(tr(
                f'<span style="color:{self._theme.neutral()}"><b>○ Не установлена полностью</b></span> '
                f'({"; ".join(details)})',
                f'<span style="color:{self._theme.neutral()}"><b>○ Not fully installed</b></span> '
                f'({"; ".join(details)})',
            ))
            self.btn_install_polkit.setText(tr('🔐  Установить / Обновить интеграцию', '🔐  Install / Update Integration'))

    def update_statuses(self, statuses: dict):
        self._last_statuses = statuses
        for row_i, (unit, _desc) in enumerate(get_ps5_services()):
            info = statuses.get(unit, {})
            act = info.get('active')
            act_str = info.get('active_state', 'unknown')
            if act is True:
                act_txt, act_c = f'● {act_str}', self._theme.positive()
            elif act is False:
                act_txt, act_c = f'○ {act_str}', self._theme.muted()
            else:
                act_txt, act_c = '? unknown', self._theme.neutral()

            it_act = QTableWidgetItem(act_txt)
            it_act.setForeground(QColor(act_c))
            self.tbl_services.setItem(row_i, 1, it_act)

            uf = info.get('unit_file_state', '—')
            en = info.get('enabled')
            en_c = self._theme.positive() if en else self._theme.muted()
            it_en = QTableWidgetItem(uf)
            it_en.setForeground(QColor(en_c))
            self.tbl_services.setItem(row_i, 2, it_en)

    def _selected_unit(self) -> str | None:
        row = self.tbl_services.currentRow()
        if row < 0:
            return None
        item = self.tbl_services.item(row, 0)
        return item.text() if item else None

    def _act_selected(self, op: str):
        unit = self._selected_unit()
        if not unit:
            return
        self._run_action('systemctl', [op, unit], f'systemctl {op} {unit}')

    def _on_view_unit_log(self):
        unit = self._selected_unit()
        if not unit:
            return
        self._start_journal_poller(mode='unit', unit=unit)

    def refresh_journal(self):
        rx = self.cmb_filter.currentData() or ''
        if rx == '__ERR__':
            self._start_journal_poller(mode='kernel_err')
        else:
            self._start_journal_poller(mode='kernel', filter_regex=rx)

    def _start_journal_poller(self, mode: str, unit: str = '', filter_regex: str = ''):
        self.txt_journal.setPlainText(tr('Загрузка журнала…', 'Loading journal…'))
        poller = JournalPoller(mode=mode, unit=unit, filter_regex=filter_regex)
        poller.result.connect(self._on_journal_ready)
        poller.finished.connect(lambda: self._pollers.remove(poller) if poller in self._pollers else None)
        poller.start()
        self._pollers.append(poller)

    def _on_journal_ready(self, title: str, text: str):
        self.txt_journal.setPlainText(f'=== {title} ===\n\n{text}')

    def _on_copy_report(self):
        header = (
            f"Model: {self._ps5_info.get('model')}\n"
            f"Kernel: {self._ps5_info.get('kernel')}\n"
            f"Session: {self._ps5_info.get('session_type')}\n"
            f"VBIOS: {self._ps5_info.get('vbios')}\n"
            + '-' * 50 + '\n'
        )
        QApplication.clipboard().setText(header + self.txt_journal.toPlainText())
        QMessageBox.information(
            self,
            tr('Скопировано', 'Copied'),
            tr('Диагностический отчёт скопирован в буфер обмена.', 'Diagnostic report copied to clipboard.'),
        )

    def _on_install_integration(self):
        reply = QMessageBox.question(
            self,
            tr('Установка системной интеграции', 'Install System Integration'),
            tr(
                'Будут установлены в систему (через один запрос pkexec):\n'
                '• /usr/local/libexec/ps5tools-helper\n'
                '• /usr/share/polkit-1/actions/org.ps5linuxtools.policy (auth_admin_keep)\n'
                '• Недостающие утилиты ps5-linux-tools в /usr/local/sbin/\n\n'
                'Продолжить?',
                'The following files will be installed system-wide (via one pkexec prompt):\n'
                '• /usr/local/libexec/ps5tools-helper\n'
                '• /usr/share/polkit-1/actions/org.ps5linuxtools.policy (auth_admin_keep)\n'
                '• Missing ps5-linux-tools binaries in /usr/local/sbin/\n\n'
                'Continue?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.Yes,
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._install_integration(on_done=self.refresh_integration_status)

    def wait_pollers(self):
        for p in list(self._pollers):
            p.wait(1000)
