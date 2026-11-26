# -*- coding: utf-8 -*-
"""
Translucent always-on-top PS5 monitoring overlay.
Uses KDE LayerShellQt (zwlr_layer_shell_v1, LayerOverlay) on Wayland so the
overlay never drops behind other windows on focus changes (and stays visible
even above fullscreen apps), never steals keyboard focus, and drags smoothly
via margins. Falls back to WindowStaysOnTopHint on X11.
"""

import ctypes
import os
import signal
import subprocess
from pathlib import Path
from PyQt6 import sip
from PyQt6.QtCore import QMargins, QObject, QSettings, QSize, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QAction, QColor, QGuiApplication
from PyQt6.QtWidgets import (
    QCheckBox, QDialog, QDialogButtonBox, QFrame, QGridLayout,
    QHBoxLayout, QLabel, QMenu, QPushButton, QSlider, QVBoxLayout, QWidget,
)

from .config import APP_SETTINGS_NAME, KDETheme, ORG_NAME, is_ru, resolve_tool, tr

# Overlay metrics: (id, label, shown by default)
OVERLAY_METRICS = [
    ('soc',      'MainSoC:',  True),
    ('local1',   'Local1:',   False),
    ('local2',   'Local2:',   False),
    ('local3',   'Local3:',   False),
    ('gpu_t',    'GPU edge:', True),
    ('cpu_t',    'CPU Tctl:', True),
    ('cpu_mhz',  'CPU freq:', True),
    ('nvme',     'NVMe:',     True),
    ('gpu_busy', 'GPU load:', True),
    ('gpu_mhz',  'GPU freq:', True),
    ('vram',     'VRAM:',     True),
    ('fan',      'Fan PWM:',  True),
    ('gpu_pm',   'GPU PM:',   False),
]


class _LayerShellAdapter:
    """
    Wrapper around libLayerShellQtInterface.so.6 for a single QWindow.
    Enables zwlr_layer_shell_v1 (LayerOverlay) only on the overlay window without
    affecting the main application window.
    """

    LAYER_OVERLAY = 3
    ANCHOR_TOP_LEFT = 1 | 4  # AnchorTop | AnchorLeft
    KBD_NONE = 0

    def __init__(self):
        self._ptr = None
        self._qobj = None
        self._set_margins = None
        self._set_size = None

    def attach(self, qwindow) -> bool:
        if qwindow is None or not QGuiApplication.platformName().startswith('wayland'):
            return False
        try:
            lib = ctypes.CDLL('libLayerShellQtInterface.so.6')
            get_fn = lib['_ZN12LayerShellQt6Window3getEP7QWindow']
            get_fn.argtypes = [ctypes.c_void_p]
            get_fn.restype = ctypes.c_void_p

            set_layer = lib['_ZN12LayerShellQt6Window8setLayerENS0_5LayerE']
            set_layer.argtypes = [ctypes.c_void_p, ctypes.c_int]

            set_anchors = lib['_ZN12LayerShellQt6Window10setAnchorsE6QFlagsINS0_6AnchorEE']
            set_anchors.argtypes = [ctypes.c_void_p, ctypes.c_int]

            set_kbd = lib['_ZN12LayerShellQt6Window24setKeyboardInteractivityENS0_21KeyboardInteractivityE']
            set_kbd.argtypes = [ctypes.c_void_p, ctypes.c_int]

            set_excl = lib['_ZN12LayerShellQt6Window16setExclusiveZoneEi']
            set_excl.argtypes = [ctypes.c_void_p, ctypes.c_int]

            self._set_margins = lib['_ZN12LayerShellQt6Window10setMarginsERK8QMargins']
            self._set_margins.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

            self._set_size = lib['_ZN12LayerShellQt6Window14setDesiredSizeERK5QSize']
            self._set_size.argtypes = [ctypes.c_void_p, ctypes.c_void_p]

            ptr = get_fn(sip.unwrapinstance(qwindow))
            if not ptr:
                return False

            self._ptr = ptr
            self._qobj = sip.wrapinstance(ptr, QObject)
            set_layer(ptr, self.LAYER_OVERLAY)
            set_anchors(ptr, self.ANCHOR_TOP_LEFT)
            set_kbd(ptr, self.KBD_NONE)
            set_excl(ptr, -1)
            self._qobj.setProperty('activateOnShow', False)
            self._qobj.setProperty('wantsToBeOnActiveScreen', True)
            self._qobj.setProperty('scope', 'ps5tools-overlay')
            return True
        except Exception:
            self._ptr = None
            self._qobj = None
            return False

    @property
    def active(self) -> bool:
        return self._ptr is not None

    def set_geometry(self, x: int, y: int, size: QSize):
        if not self._ptr:
            return
        m = QMargins(x, y, 0, 0)
        s = QSize(size)
        self._set_margins(self._ptr, sip.unwrapinstance(m))
        self._set_size(self._ptr, sip.unwrapinstance(s))


class OverlaySettingsDialog(QDialog):
    """Settings dialog for overlay opacity and displayed metrics."""

    def __init__(self, visible_keys: dict[str, bool], opacity_pct: int, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr('Настройка оверлея', 'Overlay Settings'))
        self.setModal(True)
        self.setMinimumWidth(290)

        layout = QVBoxLayout(self)
        layout.setSpacing(10)

        # Opacity
        op_row = QHBoxLayout()
        op_row.addWidget(QLabel(tr('Непрозрачность:', 'Opacity:')))
        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setRange(40, 100)
        self._slider.setValue(opacity_pct)
        self._lbl_op = QLabel(f'{opacity_pct}%')
        self._lbl_op.setFixedWidth(36)
        self._slider.valueChanged.connect(lambda v: self._lbl_op.setText(f'{v}%'))
        op_row.addWidget(self._slider, stretch=1)
        op_row.addWidget(self._lbl_op)
        layout.addLayout(op_row)

        layout.addWidget(QLabel(tr('Отображаемые датчики:', 'Visible sensors:')))

        self._checks: dict[str, QCheckBox] = {}
        grid = QGridLayout()
        for idx, (key, title, _default) in enumerate(OVERLAY_METRICS):
            cb = QCheckBox(title.rstrip(':'))
            cb.setChecked(visible_keys.get(key, True))
            self._checks[key] = cb
            grid.addWidget(cb, idx // 2, idx % 2)
        layout.addLayout(grid)

        btns = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel
        )
        btns.accepted.connect(self.accept)
        btns.rejected.connect(self.reject)
        layout.addWidget(btns)

    def get_values(self) -> tuple[dict[str, bool], int]:
        return (
            {k: cb.isChecked() for k, cb in self._checks.items()},
            self._slider.value(),
        )


class MonitorOverlay(QWidget):
    """Compact translucent PS5 hardware monitoring overlay."""

    visibilityChanged = pyqtSignal(bool)

    def __init__(self, theme: KDETheme, settings_parent=None):
        is_wayland = QGuiApplication.platformName().startswith('wayland')
        flags = Qt.WindowType.FramelessWindowHint
        if not is_wayland:
            flags |= Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.Tool

        super().__init__(None, flags)
        self._theme = theme
        self._settings_parent = settings_parent
        self._settings = QSettings(ORG_NAME, APP_SETTINGS_NAME)
        self._drag_offset = None
        self._last_data: dict = {}
        self._pos_x = 0
        self._pos_y = 0

        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating, True)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        self.setObjectName('overlay_root')
        self.setWindowTitle('PS5MonitorOverlay')
        self.setMinimumWidth(205)
        self.setMaximumWidth(260)

        # Attach Wayland LayerShell before showing the window
        self._layer = _LayerShellAdapter()
        self.winId()
        if not self._layer.attach(self.windowHandle()) and is_wayland:
            # Keep window flags as fallback if libLayerShellQtInterface is unavailable
            self.setWindowFlags(flags | Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.Tool)

        # Load saved settings
        self._opacity_pct = int(self._settings.value('overlay/opacity', 85))
        self._visible_metrics: dict[str, bool] = {}
        for key, _title, default in OVERLAY_METRICS:
            val = self._settings.value(f'overlay/metric_{key}', default)
            if isinstance(val, str):
                val = val.lower() == 'true'
            self._visible_metrics[key] = bool(val)

        # Root layout
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)

        self._card = QWidget(self)
        self._card.setObjectName('overlay_bg')
        self._card.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        card_layout = QVBoxLayout(self._card)
        card_layout.setContentsMargins(10, 8, 10, 8)
        card_layout.setSpacing(3)
        root_layout.addWidget(self._card)

        # --- Header ---
        hdr = QHBoxLayout()
        hdr.setSpacing(4)
        self._title_lbl = QLabel('🎮  PS5 Monitor')
        hdr.addWidget(self._title_lbl)
        hdr.addStretch()

        self._btn_cfg = QPushButton('⚙')
        self._btn_cfg.setObjectName('ov_btn')
        self._btn_cfg.setFixedSize(18, 18)
        self._btn_cfg.clicked.connect(self.open_settings)
        hdr.addWidget(self._btn_cfg)

        self._btn_close = QPushButton('✕')
        self._btn_close.setObjectName('ov_close')
        self._btn_close.setFixedSize(18, 18)
        self._btn_close.clicked.connect(self.hide)
        hdr.addWidget(self._btn_close)
        card_layout.addLayout(hdr)

        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setObjectName('ov_sep')
        card_layout.addWidget(sep)

        # Metrics grid
        grid = QGridLayout()
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(2)
        grid.setColumnMinimumWidth(0, 82)

        self._rows: dict[str, tuple[QLabel, QLabel]] = {}
        for row_i, (key, title, _default) in enumerate(OVERLAY_METRICS):
            k_lbl = QLabel(title)
            v_lbl = QLabel('—')
            v_lbl.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
            grid.addWidget(k_lbl, row_i, 0)
            grid.addWidget(v_lbl, row_i, 1)
            self._rows[key] = (k_lbl, v_lbl)

        card_layout.addLayout(grid)

        self._hint_lbl = QLabel()
        self._hint_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        card_layout.addWidget(self._hint_lbl)

        self.retranslate_ui()
        self._apply_styles()
        self._apply_visibility()
        self._restore_position()

    def retranslate_ui(self):
        self._btn_cfg.setToolTip(tr('Настроить метрики и прозрачность оверлея', 'Configure overlay metrics and opacity'))
        self._btn_close.setToolTip(tr('Скрыть оверлей', 'Hide overlay'))
        self._hint_lbl.setText(tr('⠿ Потяните за карточку (ПКМ — настройки)', '⠿ Drag card (right-click — settings)'))

    def refresh_appearance(self):
        self.retranslate_ui()
        self._apply_styles()
        if self._last_data:
            self.update_sensors(self._last_data, force=True)

    def _apply_styles(self):
        accent = self._theme.overlay_accent()
        c_acc = QColor(accent)
        alpha = max(90, min(255, round(self._opacity_pct * 2.55)))
        self.setStyleSheet(f"""
            QWidget#overlay_root {{
                background: transparent;
            }}
            QWidget#overlay_bg {{
                background-color: rgba(16, 26, 22, {alpha});
                border: 1px solid rgba({c_acc.red()}, {c_acc.green()}, {c_acc.blue()}, 120);
                border-radius: 8px;
            }}
            QFrame#ov_sep {{
                background: rgba({c_acc.red()}, {c_acc.green()}, {c_acc.blue()}, 70);
                min-height: 1px;
                max-height: 1px;
                border: none;
            }}
            QPushButton#ov_btn, QPushButton#ov_close {{
                background: transparent;
                color: rgba(220, 230, 225, 175);
                border: none;
                font-size: 11px;
                padding: 0px;
            }}
            QPushButton#ov_btn:hover {{
                color: {accent};
            }}
            QPushButton#ov_close:hover {{
                color: {self._theme.negative()};
            }}
        """)
        self._title_lbl.setStyleSheet(f'color: {accent}; font-weight: bold; font-size: 11px;')
        self._hint_lbl.setStyleSheet('color: rgba(180, 200, 190, 120); font-size: 8px;')
        for k_lbl, v_lbl in self._rows.values():
            k_lbl.setStyleSheet('color: rgba(205, 222, 215, 200); font-size: 10px;')
            v_lbl.setStyleSheet('color: #efeee1; font-size: 10px; font-family: monospace;')

    def _sync_geometry(self):
        self.adjustSize()
        sz = self.sizeHint().expandedTo(QSize(self.minimumWidth(), 80))
        self.resize(sz)
        self._clamp_position(sz)
        if self._layer.active:
            self._layer.set_geometry(self._pos_x, self._pos_y, sz)
        else:
            self.move(self._pos_x, self._pos_y)

    def _clamp_position(self, sz: QSize):
        screen = QGuiApplication.primaryScreen()
        if not screen:
            return
        geo = screen.availableGeometry()
        max_x = max(0, geo.width() - sz.width())
        max_y = max(0, geo.height() - sz.height())
        self._pos_x = max(0, min(self._pos_x, max_x))
        self._pos_y = max(0, min(self._pos_y, max_y))

    def _apply_visibility(self):
        for key, (k_lbl, v_lbl) in self._rows.items():
            vis = self._visible_metrics.get(key, True)
            k_lbl.setVisible(vis)
            v_lbl.setVisible(vis)
        self._sync_geometry()

    def _restore_position(self):
        screen = QGuiApplication.primaryScreen()
        default_x, default_y = 20, 40
        if screen:
            geo = screen.availableGeometry()
            default_x = max(0, geo.width() - self.width() - 20)
            default_y = 40
        self._pos_x = int(self._settings.value('overlay/x', default_x))
        self._pos_y = int(self._settings.value('overlay/y', default_y))
        self._sync_geometry()

    def show_overlay(self):
        if self._last_data:
            self.update_sensors(self._last_data, force=True)
        self._sync_geometry()
        self.show()
        if not self._layer.active and not QGuiApplication.platformName().startswith('offscreen'):
            self.raise_()

    def open_settings(self):
        dlg_parent = (
            self._settings_parent
            if (self._settings_parent is not None and self._settings_parent.isVisible())
            else None
        )
        dlg = OverlaySettingsDialog(self._visible_metrics, self._opacity_pct, dlg_parent)
        if dlg.exec() == QDialog.DialogCode.Accepted:
            vis, op = dlg.get_values()
            self._visible_metrics = vis
            self._opacity_pct = op
            self._settings.setValue('overlay/opacity', op)
            for k, v in vis.items():
                self._settings.setValue(f'overlay/metric_{k}', v)
            self._apply_styles()
            self._apply_visibility()

    def contextMenuEvent(self, ev):
        menu = QMenu(self)
        act_cfg = QAction(tr('Настроить метрики и прозрачность…', 'Configure metrics & opacity…'), self)
        act_cfg.triggered.connect(self.open_settings)
        act_hide = QAction(tr('Скрыть оверлей', 'Hide overlay'), self)
        act_hide.triggered.connect(self.hide)
        menu.addAction(act_cfg)
        menu.addSeparator()
        menu.addAction(act_hide)
        menu.exec(ev.globalPos())

    def showEvent(self, ev):
        super().showEvent(ev)
        self.visibilityChanged.emit(True)

    def hideEvent(self, ev):
        super().hideEvent(ev)
        self.visibilityChanged.emit(False)

    # --- Dragging (LayerShell margins on Wayland + move() on X11) ---
    def mousePressEvent(self, ev):
        if ev.button() == Qt.MouseButton.LeftButton:
            if self._layer.active:
                self._drag_offset = ev.position().toPoint()
            else:
                self._drag_offset = ev.globalPosition().toPoint() - self.frameGeometry().topLeft()
            ev.accept()

    def mouseMoveEvent(self, ev):
        if self._drag_offset is not None and (ev.buttons() & Qt.MouseButton.LeftButton):
            if self._layer.active:
                delta = ev.position().toPoint() - self._drag_offset
                self._pos_x += delta.x()
                self._pos_y += delta.y()
                self._clamp_position(self.size())
                self._layer.set_geometry(self._pos_x, self._pos_y, self.size())
            else:
                pt = ev.globalPosition().toPoint() - self._drag_offset
                self._pos_x = pt.x()
                self._pos_y = pt.y()
                self.move(self._pos_x, self._pos_y)
            ev.accept()

    def mouseReleaseEvent(self, ev):
        if ev.button() == Qt.MouseButton.LeftButton and self._drag_offset is not None:
            self._drag_offset = None
            self._settings.setValue('overlay/x', self._pos_x)
            self._settings.setValue('overlay/y', self._pos_y)

    # --- Data updates ---
    def _set_val(self, key: str, text: str, color: str = '#efeee1'):
        if key in self._rows:
            _, v_lbl = self._rows[key]
            v_lbl.setText(text)
            v_lbl.setStyleSheet(f'color: {color}; font-size: 10px; font-family: monospace;')

    def _temp_color(self, val: float, warn: float = 70.0, hot: float = 85.0) -> str:
        if val >= hot:
            return self._theme.negative()
        if val >= warn:
            return self._theme.neutral()
        return '#efeee1'

    def update_sensors(self, data: dict, force: bool = False):
        self._last_data = data
        if not force and not self.isVisible():
            return
        na_str = tr('н/д', 'N/A')
        mhz_unit = tr('МГц', 'MHz')
        mb_unit = tr('МБ', 'MB')

        def upd_temp(ov_key: str, data_key: str, warn=70.0, hot=85.0):
            v = data.get(data_key)
            if v is not None:
                self._set_val(ov_key, f'{v:.1f} °C', self._temp_color(v, warn, hot))
            else:
                self._set_val(ov_key, na_str)

        upd_temp('soc', 'ps5fan_temp1', 75, 90)
        upd_temp('local1', 'ps5fan_temp2', 70, 85)
        upd_temp('local2', 'ps5fan_temp3', 50, 65)
        upd_temp('local3', 'ps5fan_temp4', 70, 85)
        upd_temp('gpu_t', 'gpu_temp', 75, 90)
        upd_temp('cpu_t', 'cpu_temp', 75, 90)
        upd_temp('nvme', 'nvme_temp1', 60, 70)

        cpu_mhz = data.get('cpu_freq_mhz')
        self._set_val('cpu_mhz', f'{cpu_mhz} {mhz_unit}' if cpu_mhz else '—')

        busy = data.get('gpu_busy')
        if busy is not None:
            c = self._theme.negative() if busy > 95 else self._theme.neutral() if busy > 70 else '#efeee1'
            self._set_val('gpu_busy', f'{busy}%', c)
        else:
            self._set_val('gpu_busy', na_str)

        freq = data.get('gpu_freq_mhz')
        self._set_val('gpu_mhz', f'{freq} {mhz_unit}' if freq else '—')

        v_used = data.get('vram_used_mb')
        v_tot = data.get('vram_total_mb')
        if v_used is not None and v_tot:
            ratio = v_used / v_tot
            c = self._theme.negative() if ratio > 0.9 else self._theme.neutral() if ratio > 0.75 else '#efeee1'
            self._set_val('vram', f'{v_used}/{v_tot} {mb_unit}', c)
        else:
            self._set_val('vram', '—')

        pct = data.get('fan_pwm_pct')
        self._set_val('fan', f'{pct}%' if pct is not None else '—')

        pm = data.get('gpu_pm', '—')
        pm_c = self._theme.positive() if pm == 'active' else self._theme.neutral()
        self._set_val('gpu_pm', pm, pm_c)


class HybridOverlayManager(QObject):
    """
    Hybrid overlay controller:
    - On Wayland, when the native C daemon ps5overlayd (~4 MB private RAM) is available,
      launches and configures it as an independent process above all windows;
      the main GUI can then be closed while keeping the HUD on screen.
    - On X11 or when ps5overlayd is missing, uses the built-in MonitorOverlay Qt widget.
    """

    visibilityChanged = pyqtSignal(bool)

    def __init__(self, theme: KDETheme, settings_parent=None):
        super().__init__(settings_parent)
        self._theme = theme
        self._settings_parent = settings_parent
        self._conf_path = Path.home() / '.config' / 'ps5linuxtools' / 'overlay.conf'
        self._daemon_bin = self._find_daemon()
        self._use_daemon = bool(
            self._daemon_bin and QGuiApplication.platformName().startswith('wayland')
        )
        self._last_visible = False
        self._widget_overlay: MonitorOverlay | None = None

        if not self._use_daemon:
            self._widget_overlay = MonitorOverlay(theme, settings_parent=settings_parent)
            self._widget_overlay.visibilityChanged.connect(self._emit_vis)
        else:
            # Sync KDE accent & language to daemon config if config does not exist yet
            if not self._conf_path.is_file():
                vis = {k: default for k, _t, default in OVERLAY_METRICS}
                self._write_conf(vis, 85, accent=self._theme.overlay_accent())
            self._watch_timer = QTimer(self)
            self._watch_timer.setInterval(1500)
            self._watch_timer.timeout.connect(self._check_daemon_state)
            self._watch_timer.start()
            self._last_visible = self.isVisible()

    @staticmethod
    def _find_daemon() -> str | None:
        p = resolve_tool('ps5overlayd')
        if p and Path(p).is_file():
            return p
        return None

    @staticmethod
    def _pid_file() -> Path:
        xrd = os.environ.get('XDG_RUNTIME_DIR')
        if xrd:
            return Path(xrd) / 'ps5overlayd.pid'
        return Path(f'/tmp/ps5overlayd-{os.getuid()}.pid')

    def _get_daemon_pid(self) -> int | None:
        pf = self._pid_file()
        if not pf.is_file():
            return None
        try:
            pid = int(pf.read_text(encoding='utf-8').strip())
            os.kill(pid, 0)
            return pid
        except Exception:
            return None

    def is_daemon_mode(self) -> bool:
        return self._use_daemon

    def isVisible(self) -> bool:
        if self._use_daemon:
            return self._get_daemon_pid() is not None
        return bool(self._widget_overlay and self._widget_overlay.isVisible())

    def _emit_vis(self, vis: bool):
        if vis != self._last_visible:
            self._last_visible = vis
            self.visibilityChanged.emit(vis)

    def _check_daemon_state(self):
        self._emit_vis(self._get_daemon_pid() is not None)

    def _read_conf(self) -> tuple[dict[str, bool], int, dict[str, str]]:
        vis = {k: default for k, _t, default in OVERLAY_METRICS}
        opacity = 85
        extra: dict[str, str] = {}
        if self._conf_path.is_file():
            try:
                for line in self._conf_path.read_text(encoding='utf-8').splitlines():
                    if '=' not in line:
                        continue
                    k, v = line.split('=', 1)
                    if k == 'opacity':
                        opacity = max(40, min(100, int(v)))
                    elif k.startswith('metric_'):
                        mkey = k[len('metric_'):]
                        if mkey in vis:
                            vis[mkey] = (v.strip() != '0')
                    else:
                        extra[k] = v.strip()
            except Exception:
                pass
        return vis, opacity, extra

    def _overlay_bg_hex(self) -> str:
        from PyQt6.QtWidgets import QApplication
        from PyQt6.QtGui import QPalette
        win = QApplication.palette().color(QPalette.ColorRole.Window)
        if win.lightness() < 128:
            c = win.darker(145)
            return c.name()
        return '#161d24'

    def _write_conf(self, vis: dict[str, bool], opacity: int, accent: str | None = None):
        _, _, extra = self._read_conf()
        if accent:
            extra['accent'] = accent
        elif 'accent' not in extra:
            extra['accent'] = self._theme.overlay_accent()
        self._conf_path.parent.mkdir(parents=True, exist_ok=True)
        lines = []
        if 'x' in extra:
            lines.append(f'x={extra["x"]}')
        if 'y' in extra:
            lines.append(f'y={extra["y"]}')
        lines.append(f'opacity={opacity}')
        lines.append(f'lang={"ru" if is_ru() else "en"}')
        lines.append(f'accent={extra["accent"]}')
        lines.append(f'bg={self._overlay_bg_hex()}')
        lines.append('fg=#efeee1')
        for k in vis:
            lines.append(f'metric_{k}={1 if vis[k] else 0}')
        self._conf_path.write_text('\n'.join(lines) + '\n', encoding='utf-8')

    def refresh_appearance(self):
        if self._use_daemon:
            vis, op, _ = self._read_conf()
            self._write_conf(vis, op, accent=self._theme.overlay_accent())
            pid = self._get_daemon_pid()
            if pid:
                try:
                    os.kill(pid, signal.SIGUSR1)
                except OSError:
                    pass
        elif self._widget_overlay:
            self._widget_overlay.refresh_appearance()

    def show_overlay(self):
        if self._use_daemon:
            if not self._get_daemon_pid() and self._daemon_bin:
                # Pass current KDE accent & active language on startup
                vis, op, _ = self._read_conf()
                self._write_conf(vis, op, accent=self._theme.overlay_accent())
                subprocess.Popen(
                    [self._daemon_bin],
                    start_new_session=True,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            QTimer.singleShot(150, self._check_daemon_state)
        elif self._widget_overlay:
            self._widget_overlay.show_overlay()

    def hide(self):
        if self._use_daemon:
            pid = self._get_daemon_pid()
            if pid:
                try:
                    os.kill(pid, signal.SIGTERM)
                except OSError:
                    pass
            QTimer.singleShot(150, self._check_daemon_state)
        elif self._widget_overlay:
            self._widget_overlay.hide()

    def open_settings(self):
        if not self._use_daemon and self._widget_overlay:
            self._widget_overlay.open_settings()
            return
        vis, op, _ = self._read_conf()
        dlg_parent = (
            self._settings_parent
            if (self._settings_parent is not None and self._settings_parent.isVisible())
            else None
        )
        dlg = OverlaySettingsDialog(vis, op, dlg_parent)
        if dlg.exec() == QDialog.DialogCode.Accepted:
            new_vis, new_op = dlg.get_values()
            self._write_conf(new_vis, new_op, accent=self._theme.overlay_accent())
            pid = self._get_daemon_pid()
            if pid:
                try:
                    os.kill(pid, signal.SIGUSR1)
                except OSError:
                    pass

    def update_sensors(self, data: dict):
        if self._widget_overlay:
            self._widget_overlay.update_sensors(data)

    def shutdown(self, keep_daemon: bool = True):
        if self._use_daemon:
            self._watch_timer.stop()
            if not keep_daemon:
                self.hide()
        elif self._widget_overlay:
            self._widget_overlay.close()

