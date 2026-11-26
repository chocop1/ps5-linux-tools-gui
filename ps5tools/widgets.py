# -*- coding: utf-8 -*-
"""
Reusable KDE-compatible widgets:
- TempBar: native QProgressBar colored via QPalette (preserving native Breeze rendering)
- FanCurveChart: QPainter fan curve graph (Temp C x Fan PWM %)
- Status and threshold color helpers
"""

from PyQt6.QtCore import Qt, QPointF
from PyQt6.QtGui import QColor, QFont, QPainter, QPainterPath, QPalette, QPen
from PyQt6.QtWidgets import QApplication, QProgressBar, QSizePolicy, QWidget

from .config import KDETheme, tr


def level_color(ratio: float, theme: KDETheme) -> str:
    """Return hex color for load/temperature ratio: normal -> warn -> hot."""
    if ratio < 0.60:
        return theme.positive()
    if ratio < 0.80:
        return theme.neutral()
    return theme.negative()


def temp_color(temp_c: float, theme: KDETheme, warn: float = 65.0, hot: float = 82.0) -> str:
    if temp_c < warn:
        return theme.positive()
    if temp_c < hot:
        return theme.neutral()
    return theme.negative()


class TempBar(QProgressBar):
    """
    Progress bar preserving native KDE/Qt style drawing
    (updates only QPalette.Highlight instead of using QProgressBar::chunk stylesheets).
    """

    def __init__(self, max_val: int = 100, show_text: bool = False, parent=None):
        super().__init__(parent)
        self.setRange(0, max_val)
        self.setValue(0)
        self.setTextVisible(show_text)
        self.setFixedHeight(16 if show_text else 12)

    def set_colored_value(self, value: int, color_hex: str, max_val: int | None = None):
        if max_val is not None:
            self.setMaximum(max_val)
        self.setValue(max(0, min(value, self.maximum())))
        pal = QPalette(QApplication.palette())
        pal.setColor(QPalette.ColorRole.Highlight, QColor(color_hex))
        self.setPalette(pal)

    def reset_theme(self):
        self.setPalette(QPalette(QApplication.palette()))


class FanCurveChart(QWidget):
    """
    8-point fan curve plot (Temperature C x Fan Speed %).
    Painted via QPainter using active palette/theme colors.
    """

    def __init__(self, theme: KDETheme, parent=None):
        super().__init__(parent)
        self._theme = theme
        self._points: list[tuple[int, int]] = []  # (temp_c, pwm_0_255)
        self._current_temp: float | None = None
        self.setMinimumHeight(175)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)

    def set_data(self, points: list[tuple[int, int]], current_temp: float | None = None):
        self._points = list(points)
        if current_temp is not None:
            self._current_temp = current_temp
        self.update()

    def set_current_temp(self, temp: float | None):
        self._current_temp = temp
        self.update()

    def paintEvent(self, _ev):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        pal = QApplication.palette()
        bg = pal.color(QPalette.ColorRole.Base)
        fg = pal.color(QPalette.ColorRole.Text)
        muted = QColor(self._theme.muted())
        border = pal.color(QPalette.ColorRole.Mid)
        accent = QColor(self._theme.accent())
        warn_c = QColor(self._theme.neutral())

        w = self.width()
        h = self.height()
        ml, mr, mt, mb = 42, 16, 14, 26
        pw = max(10, w - ml - mr)
        ph = max(10, h - mt - mb)

        # Plot background
        painter.setPen(QPen(border, 1))
        painter.setBrush(bg)
        painter.drawRoundedRect(ml, mt, pw, ph, 4, 4)

        t_min, t_max = 30.0, 95.0

        def map_x(t: float) -> float:
            ratio = max(0.0, min(1.0, (t - t_min) / (t_max - t_min)))
            return ml + ratio * pw

        def map_y(pwm_pct: float) -> float:
            ratio = max(0.0, min(1.0, pwm_pct / 100.0))
            return mt + ph - ratio * ph

        font = QFont(self.font())
        font.setPointSize(8)
        painter.setFont(font)

        # Y grid (0%, 25%, 50%, 75%, 100%)
        grid_pen = QPen(border, 1, Qt.PenStyle.DotLine)
        for pct in (0, 25, 50, 75, 100):
            y = map_y(pct)
            painter.setPen(grid_pen)
            painter.drawLine(QPointF(ml, y), QPointF(ml + pw, y))
            painter.setPen(muted)
            painter.drawText(2, int(y) - 7, ml - 6, 14, Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter, f'{pct}%')

        # X grid (40..90 °C)
        for t in (40, 50, 60, 70, 80, 90):
            x = map_x(t)
            painter.setPen(grid_pen)
            painter.drawLine(QPointF(x, mt), QPointF(x, mt + ph))
            painter.setPen(muted)
            painter.drawText(int(x) - 18, mt + ph + 4, 36, 16, Qt.AlignmentFlag.AlignCenter, f'{t}°')

        # Current MainSoC temperature (vertical marker)
        if self._current_temp is not None:
            cx = map_x(self._current_temp)
            painter.setPen(QPen(warn_c, 1.5, Qt.PenStyle.DashLine))
            painter.drawLine(QPointF(cx, mt + 2), QPointF(cx, mt + ph - 2))
            painter.setPen(warn_c)
            painter.drawText(
                int(cx) - 24, mt + 2, 48, 14,
                Qt.AlignmentFlag.AlignCenter,
                f'{self._current_temp:.1f}°'
            )

        # Curve line + fill
        if self._points:
            pts = [
                QPointF(map_x(t), map_y(round(pwm / 255 * 100)))
                for t, pwm in self._points
            ]
            path = QPainterPath()
            path.moveTo(pts[0])
            for pt in pts[1:]:
                path.lineTo(pt)

            fill_path = QPainterPath(path)
            fill_path.lineTo(pts[-1].x(), mt + ph)
            fill_path.lineTo(pts[0].x(), mt + ph)
            fill_path.closeSubpath()

            fill_c = QColor(accent)
            fill_c.setAlpha(45)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(fill_c)
            painter.drawPath(fill_path)

            painter.setPen(QPen(accent, 2.2))
            painter.setBrush(Qt.BrushStyle.NoBrush)
            painter.drawPath(path)

            # Points
            painter.setPen(QPen(fg, 1.5))
            painter.setBrush(accent)
            for pt in pts:
                painter.drawEllipse(pt, 4.0, 4.0)
        else:
            painter.setPen(muted)
            painter.drawText(ml, mt, pw, ph, Qt.AlignmentFlag.AlignCenter, tr('Данные кривой недоступны', 'Fan curve data unavailable'))
