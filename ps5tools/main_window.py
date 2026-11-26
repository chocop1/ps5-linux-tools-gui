# -*- coding: utf-8 -*-
"""Main window for PS5 Linux Tools and KDE system tray integration."""

from PyQt6.QtCore import QSettings, QTimer
from PyQt6.QtGui import QAction, QIcon
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QMenu, QPushButton,
    QSystemTrayIcon, QTabWidget,
)

from .config import (
    APP_NAME, APP_SETTINGS_NAME, KDETheme, ORG_NAME,
    apply_app_appearance, resolve_asset, set_active_language, tr,
)
from .overlay import HybridOverlayManager
from .runners import (
    CommandRunner, DisplayPoller, M2Poller, SensorPoller, StatusChecker,
    build_install_integration_cmd, build_privileged_cmd,
)
from . import sysinfo
from .tabs.control_tab import ControlTab
from .tabs.display_tab import DisplayTab
from .tabs.log_tab import LogTab
from .tabs.m2_tab import M2Tab
from .tabs.monitor_tab import MonitorTab
from .tabs.services_tab import ServicesTab
from .tabs.settings_tab import SettingsTab


def load_app_icon() -> QIcon:
    svg_path = resolve_asset('ps5tools.svg')
    if svg_path:
        ico = QIcon(svg_path)
        if not ico.isNull():
            return ico
    return QIcon.fromTheme('preferences-system')


class PS5ToolsWindow(QMainWindow):
    """Main application window with 7 tabs, overlay controller, and tray integration."""

    def __init__(self, theme: KDETheme | None = None):
        super().__init__()
        self._theme = theme if theme is not None else KDETheme()
        self._ps5_info = sysinfo.detect_ps5_info()
        self._settings = QSettings(ORG_NAME, APP_SETTINGS_NAME)
        self._runners: list = []
        self._force_quit = False
        self._busy = False

        self.setMinimumSize(780, 580)
        self.resize(880, 670)
        self.setWindowIcon(load_app_icon())

        # Overlay (hybrid controller: native ps5overlayd on Wayland or Qt widget fallback)
        self._overlay = HybridOverlayManager(self._theme, settings_parent=self)
        self._overlay.visibilityChanged.connect(self._on_overlay_visibility_changed)

        # Tabs
        self.tabs = QTabWidget()
        self.setCentralWidget(self.tabs)

        minimize_default = self._settings.value('ui/minimize_to_tray', False)
        if isinstance(minimize_default, str):
            minimize_default = minimize_default.lower() == 'true'

        self.tab_control = ControlTab(self._theme, self.run_action)
        self.tab_monitor = MonitorTab(
            self._theme,
            self._ps5_info,
            self._toggle_overlay,
            self._overlay.open_settings,
            self._detach_with_overlay,
            self._overlay.is_daemon_mode(),
            self.log,
        )
        self.tab_display = DisplayTab(self._theme, self._poll_display, self.log)
        self.tab_m2 = M2Tab(self._theme, self.run_action, self._poll_m2, self.log)
        self.tab_services = ServicesTab(
            self._theme,
            self._ps5_info,
            self.run_action,
            self._install_integration,
            self.refresh_statuses,
        )
        self.tab_settings = SettingsTab(self._theme, minimize_to_tray=bool(minimize_default))
        self.tab_settings.appearanceChanged.connect(self._on_appearance_changed)
        self.tab_settings.minimizeToTrayToggled.connect(self._on_minimize_setting_changed)
        self.tab_log = LogTab()

        self.tabs.addTab(self.tab_control, '')
        self.tabs.addTab(self.tab_monitor, '')
        self.tabs.addTab(self.tab_display, '')
        self.tabs.addTab(self.tab_m2, '')
        self.tabs.addTab(self.tab_services, '')
        self.tabs.addTab(self.tab_settings, '')
        self.tabs.addTab(self.tab_log, '')
        self.tabs.currentChanged.connect(self._on_tab_changed)

        # Overlay toggle button in the status bar
        self._btn_overlay = QPushButton()
        self._btn_overlay.setCheckable(True)
        self._btn_overlay.setFixedHeight(22)
        self._btn_overlay.clicked.connect(self._toggle_overlay)
        self.statusBar().addPermanentWidget(self._btn_overlay)

        # System tray icon
        self._tray = None
        self._tray_menu = None
        self._act_tray_show = None
        self._act_tray_overlay = None
        self._act_tray_settings = None
        self._menu_tray_fan = None
        self._menu_tray_boost = None
        self._act_tray_minimize = None
        self._act_tray_quit = None
        self._act_tray_quit_all = None
        self._init_tray(bool(minimize_default))

        self.retranslate_ui()

        # Polling timers
        self._sensor_timer = QTimer(self)
        self._sensor_timer.setInterval(2000)
        self._sensor_timer.timeout.connect(self._on_sensor_tick)
        self._sensor_timer.start()

        self._display_timer = QTimer(self)
        self._display_timer.setInterval(5000)
        self._display_timer.timeout.connect(self._on_display_tick)
        self._display_timer.start()

        # Initial poll
        self.refresh_statuses()
        self._poll_sensors()
        self._poll_display()
        self._poll_m2()
        self.tab_services.refresh_journal()
        self._on_overlay_visibility_changed(self._overlay.isVisible())

    # ── Appearance & Localization ───────────────────────────────────────────

    def _on_appearance_changed(self, lang_mode: str, style_mode: str, scheme_mode: str):
        set_active_language(lang_mode, persist=True)
        app = QApplication.instance()
        if isinstance(app, QApplication):
            apply_app_appearance(app, self._theme, style_mode=style_mode, scheme_mode=scheme_mode, persist=True)
        self._ps5_info = sysinfo.detect_ps5_info()
        self._overlay.refresh_appearance()
        self.retranslate_ui()
        self.refresh_theme()

    def _on_minimize_setting_changed(self, checked: bool):
        self._settings.setValue('ui/minimize_to_tray', checked)
        if self._act_tray_minimize is not None:
            self._act_tray_minimize.blockSignals(True)
            self._act_tray_minimize.setChecked(checked)
            self._act_tray_minimize.blockSignals(False)

    def retranslate_ui(self):
        self.setWindowTitle(f'{APP_NAME} — {self._ps5_info["model"]}')

        self.tabs.setTabText(0, tr('🌀  Охлаждение / Boost', '🌀  Cooling / Boost'))
        self.tabs.setTabText(1, tr('🌡️  Мониторинг', '🌡️  Monitoring'))
        self.tabs.setTabText(2, tr('🖥️  Дисплей / Перифер.', '🖥️  Display / Devices'))
        self.tabs.setTabText(3, '💾  M.2 SSD')
        self.tabs.setTabText(4, tr('⚙️  Службы и Ядро', '⚙️  Services & Kernel'))
        self.tabs.setTabText(5, tr('🎨  Настройки', '🎨  Settings'))
        self.tabs.setTabText(6, tr('📋  Лог', '📋  Log'))

        self._btn_overlay.setText(tr('📊  Оверлей', '📊  Overlay'))
        self._btn_overlay.setToolTip(tr(
            'Показать/скрыть полупрозрачный оверлей мониторинга поверх всех окон',
            'Show/hide translucent monitoring overlay above all windows',
        ))
        self._update_status_bar()

        if self._tray is not None:
            self._act_tray_show.setText(tr('Показать / скрыть окно', 'Show / Hide Window'))
            self._act_tray_overlay.setText(tr('Оверлей мониторинга', 'Monitoring Overlay'))
            if self._act_tray_settings is not None:
                self._act_tray_settings.setText(tr('Настройки темы и языка…', 'Theme & Language Settings…'))
            self._menu_tray_fan.setTitle(tr('🌀 Вентилятор', '🌀 Fan'))
            self._menu_tray_boost.setTitle('⚡ Boost')
            self._act_tray_minimize.setText(tr('Сворачивать в трей при закрытии', 'Minimize to tray on close'))
            self._act_tray_quit.setText(tr('Выход', 'Quit'))
            if self._act_tray_quit_all is not None:
                self._act_tray_quit_all.setText(tr('Выход (вместе с оверлеем)', 'Quit (including overlay)'))

        self.tab_control.retranslate_ui()
        self.tab_monitor.retranslate_ui()
        self.tab_display.retranslate_ui()
        self.tab_m2.retranslate_ui()
        self.tab_services.retranslate_ui()
        self.tab_settings.retranslate_ui()
        self.tab_log.retranslate_ui()

    def refresh_theme(self):
        self.tab_control.refresh_theme()
        self.tab_monitor.refresh_theme()
        self.tab_display.refresh_theme()
        self.tab_m2.refresh_theme()
        self.tab_services.refresh_theme()
        self.tab_settings.refresh_theme()
        self.tab_log.refresh_theme()

    def _update_status_bar(self):
        if self._busy:
            self.statusBar().showMessage(tr('⏳  Выполнение команды…', '⏳  Running command…'))
        else:
            self.statusBar().showMessage(tr(
                f'Готов  •  Ядро {self._ps5_info["kernel"]}',
                f'Ready  •  Kernel {self._ps5_info["kernel"]}',
            ))

    # ── System tray ──────────────────────────────────────────────────────────

    def _init_tray(self, minimize_default: bool):
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return
        self._tray = QSystemTrayIcon(load_app_icon(), self)
        self._tray.setToolTip(APP_NAME)

        menu = QMenu(self)
        self._tray_menu = menu
        self._act_tray_show = QAction(self)
        self._act_tray_show.triggered.connect(self._toggle_window)
        menu.addAction(self._act_tray_show)

        self._act_tray_overlay = QAction(self)
        self._act_tray_overlay.setCheckable(True)
        self._act_tray_overlay.triggered.connect(self._toggle_overlay)
        menu.addAction(self._act_tray_overlay)

        self._act_tray_settings = QAction(self)
        self._act_tray_settings.triggered.connect(self._open_app_settings)
        menu.addAction(self._act_tray_settings)

        menu.addSeparator()

        self._menu_tray_fan = menu.addMenu('🌀')
        self._menu_tray_fan.addAction('Fan ON', lambda: self.run_action('fan', ['on'], 'ps5_control --fan on'))
        self._menu_tray_fan.addAction('Fan OFF', lambda: self.run_action('fan', ['off'], 'ps5_control --fan off'))
        self._menu_tray_fan.addAction('Fan MAX', lambda: self.run_action('fan', ['max'], 'ps5_control --fan max'))

        self._menu_tray_boost = menu.addMenu('⚡ Boost')
        self._menu_tray_boost.addAction('Boost ON', lambda: self.run_action('boost', ['on'], 'ps5_control --boost on'))
        self._menu_tray_boost.addAction('Boost OFF', lambda: self.run_action('boost', ['off'], 'ps5_control --boost off'))

        menu.addSeparator()

        self._act_tray_minimize = QAction(self)
        self._act_tray_minimize.setCheckable(True)
        self._act_tray_minimize.setChecked(minimize_default)
        self._act_tray_minimize.toggled.connect(self._on_tray_minimize_toggled)
        menu.addAction(self._act_tray_minimize)

        self._act_tray_quit = QAction(self)
        self._act_tray_quit.triggered.connect(lambda: self.quit_app(keep_daemon=True))
        menu.addAction(self._act_tray_quit)

        if self._overlay.is_daemon_mode():
            self._act_tray_quit_all = QAction(self)
            self._act_tray_quit_all.triggered.connect(lambda: self.quit_app(keep_daemon=False))
            menu.addAction(self._act_tray_quit_all)

        self._tray.setContextMenu(menu)
        self._tray.activated.connect(self._on_tray_activated)
        self._tray.show()

    def _open_app_settings(self):
        self.showNormal()
        self.activateWindow()
        self.raise_()
        self.tabs.setCurrentWidget(self.tab_settings)

    def _on_tray_minimize_toggled(self, v: bool):
        self._settings.setValue('ui/minimize_to_tray', v)
        self.tab_settings.set_minimize_to_tray(v)

    def _on_tray_activated(self, reason):
        if reason in (QSystemTrayIcon.ActivationReason.Trigger, QSystemTrayIcon.ActivationReason.DoubleClick):
            self._toggle_window()

    def _toggle_window(self):
        if self.isVisible() and not self.isMinimized():
            self.hide()
        else:
            self.showNormal()
            self.activateWindow()
            self.raise_()

    # ── Log and busy state ───────────────────────────────────────────────────

    def log(self, text: str):
        self.tab_log.append(text)

    def _set_busy(self, busy: bool):
        self._busy = busy
        widgets = (
            self.tab_control.action_widgets()
            + self.tab_m2.action_widgets()
            + self.tab_services.action_widgets()
        )
        for w in widgets:
            w.setEnabled(not busy)
        self._update_status_bar()
        if not busy and self.tab_m2._m2_state:
            # Re-apply M.2 rootfs protection (if nvme == /)
            self.tab_m2.update_m2_data(self.tab_m2._m2_state)

    # ── Privileged command execution ─────────────────────────────────────────

    def _track_runner(self, thread):
        self._runners.append(thread)
        thread.finished.connect(lambda: self._runners.remove(thread) if thread in self._runners else None)
        return thread

    def run_action(self, action: str, args: list[str], label: str, on_done=None):
        try:
            cmd = build_privileged_cmd(action, args)
        except Exception as exc:
            self.log(f'✗ {label}: {exc}')
            return

        self.log(f'▶  {label}')
        self._set_busy(True)
        runner = CommandRunner(cmd, needs_root=True)
        runner.finished.connect(
            lambda out, err, rc: self._on_cmd_done(out, err, rc, label, on_done)
        )
        self._track_runner(runner)
        runner.start()

    def _install_integration(self, on_done=None):
        try:
            cmd = build_install_integration_cmd()
        except Exception as exc:
            self.log(tr(f'✗ Установка интеграции: {exc}', f'✗ Install integration: {exc}'))
            return
        self.log(tr(
            '▶  Установка системной интеграции (helper + polkit + sbin)',
            '▶  Installing system integration (helper + polkit + sbin)',
        ))
        self._set_busy(True)
        runner = CommandRunner(cmd, needs_root=True)
        lbl = tr('Системная интеграция', 'System integration')
        runner.finished.connect(
            lambda out, err, rc: self._on_cmd_done(out, err, rc, lbl, on_done)
        )
        self._track_runner(runner)
        runner.start()

    def _on_cmd_done(self, stdout: str, stderr: str, rc: int, label: str, on_done):
        if stdout:
            self.log(stdout)
        if stderr:
            self.log(f'[stderr] {stderr}')
        if rc == 0:
            self.log(tr(f'✓  {label} — успешно (код 0)', f'✓  {label} — succeeded (exit 0)'))
        else:
            self.log(tr(f'✗  {label} — ошибка (код {rc})', f'✗  {label} — failed (exit {rc})'))
        self._set_busy(False)
        self.refresh_statuses()
        if on_done:
            on_done()

    # ── Background polling ───────────────────────────────────────────────────

    def refresh_statuses(self):
        checker = StatusChecker()
        checker.result.connect(self._on_statuses)
        self._track_runner(checker)
        checker.start()

    def _on_statuses(self, statuses: dict):
        self.tab_control.update_service_status(statuses)
        self.tab_services.update_statuses(statuses)

    def _on_tab_changed(self, idx: int):
        w = self.tabs.widget(idx)
        if w is self.tab_display:
            self._poll_display()
        elif w is self.tab_m2:
            self._poll_m2()
        elif w is self.tab_services:
            self.refresh_statuses()

    def _on_sensor_tick(self):
        cur = self.tabs.currentWidget()
        # In daemon mode ps5overlayd reads sysfs itself; Python poller is only needed when tabs are visible
        need_widget_overlay = (not self._overlay.is_daemon_mode()) and self._overlay.isVisible()
        need_sensors = (
            need_widget_overlay
            or (self.isVisible() and cur in (self.tab_monitor, self.tab_control))
        )
        if need_sensors:
            self._poll_sensors()

    def _poll_sensors(self):
        poller = SensorPoller()
        poller.result.connect(self._on_sensor_data)
        self._track_runner(poller)
        poller.start()

    def _on_sensor_data(self, data: dict):
        self.tab_monitor.update_sensors(data)
        self.tab_control.update_live_sensors(data)
        if self._overlay.isVisible():
            self._overlay.update_sensors(data)

    def _on_display_tick(self):
        if self.isVisible() and self.tabs.currentWidget() is self.tab_display:
            self._poll_display()

    def _poll_display(self):
        poller = DisplayPoller()
        poller.result.connect(self.tab_display.update_display_data)
        self._track_runner(poller)
        poller.start()

    def _poll_m2(self):
        poller = M2Poller()
        poller.result.connect(self.tab_m2.update_m2_data)
        self._track_runner(poller)
        poller.start()

    # ── Overlay ──────────────────────────────────────────────────────────────

    def _toggle_overlay(self, checked: bool):
        if checked:
            self._overlay.show_overlay()
            self._poll_sensors()
        else:
            self._overlay.hide()

    def _on_overlay_visibility_changed(self, visible: bool):
        self._btn_overlay.setChecked(visible)
        self.tab_monitor.set_overlay_checked(visible)
        if self._act_tray_overlay:
            self._act_tray_overlay.setChecked(visible)

    # ── Window closing and thread shutdown ───────────────────────────────────

    def _detach_with_overlay(self):
        """Launch standalone ps5overlayd and completely exit the Python/Qt6 GUI."""
        self._overlay.show_overlay()
        self._force_quit = True
        self.close()
        QApplication.quit()

    def quit_app(self, keep_daemon: bool = True):
        self._force_quit = True
        self._keep_daemon_on_quit = keep_daemon
        self.close()
        QApplication.quit()

    def closeEvent(self, ev):
        if (
            not self._force_quit
            and self._act_tray_minimize
            and self._act_tray_minimize.isChecked()
            and self._tray
            and self._tray.isVisible()
        ):
            self.hide()
            ev.ignore()
            return

        self._sensor_timer.stop()
        self._display_timer.stop()
        keep = getattr(self, '_keep_daemon_on_quit', True)
        self._overlay.shutdown(keep_daemon=keep)
        if self._tray:
            self._tray.hide()

        self.tab_services.wait_pollers()
        for t in list(self._runners):
            if t.isRunning():
                t.wait(1500)

        super().closeEvent(ev)
