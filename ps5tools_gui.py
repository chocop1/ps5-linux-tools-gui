#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PS5 Linux Tools — unified GUI for controlling and monitoring PS5 Linux.
Application entry point.
"""

import os
import sys

# Ensure the local ps5tools package is found even when launched via symlink/wrapper
APP_DIR = os.path.dirname(os.path.abspath(__file__))
if APP_DIR not in sys.path:
    sys.path.insert(0, APP_DIR)

from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import QApplication, QToolTip

from ps5tools.config import (
    APP_NAME, KDETheme, ORG_NAME, apply_app_appearance,
    capture_initial_theme, get_language_mode, set_active_language,
)
from ps5tools.main_window import PS5ToolsWindow, load_app_icon


def main():
    app = QApplication(sys.argv)
    app.setApplicationName(APP_NAME)
    app.setOrganizationName(ORG_NAME)
    app.setDesktopFileName('ps5tools_gui')
    app.setWindowIcon(load_app_icon())

    # Capture KDE platform style/palette before applying user overrides
    capture_initial_theme(app)
    set_active_language(get_language_mode(), persist=False)

    theme = KDETheme()
    apply_app_appearance(app, theme, persist=False)

    QToolTip.setFont(QFont('Noto Sans', 10))

    window = PS5ToolsWindow(theme=theme)
    window.show()
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
