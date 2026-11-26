# -*- coding: utf-8 -*-
"""Log tab: execution log of GUI commands with copy and clear actions."""

from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (
    QApplication, QHBoxLayout, QLabel, QPushButton,
    QTextEdit, QVBoxLayout, QWidget,
)

from ..config import tr


class LogTab(QWidget):
    """Execution output log for GUI commands and system messages."""

    def __init__(self, parent=None):
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(10, 10, 10, 10)
        layout.setSpacing(6)

        hdr = QHBoxLayout()
        self._lbl_hdr = QLabel()
        hdr.addWidget(self._lbl_hdr)
        hdr.addStretch()

        self._btn_copy = QPushButton()
        self._btn_copy.clicked.connect(self._copy)
        self._btn_clear = QPushButton()
        self._btn_clear.clicked.connect(self.clear)
        hdr.addWidget(self._btn_copy)
        hdr.addWidget(self._btn_clear)
        layout.addLayout(hdr)

        self.log_view = QTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setFont(QFont('Monospace', 9))
        self.log_view.setLineWrapMode(QTextEdit.LineWrapMode.NoWrap)
        layout.addWidget(self.log_view)

        self.retranslate_ui()

    def retranslate_ui(self):
        self._lbl_hdr.setText(tr('Журнал операций:', 'Operations log:'))
        self._btn_copy.setText(tr('📋  Копировать', '📋  Copy'))
        self._btn_copy.setToolTip(tr('Скопировать весь лог в буфер обмена', 'Copy entire log to clipboard'))
        self._btn_clear.setText(tr('🗑️  Очистить', '🗑️  Clear'))
        self._btn_clear.setToolTip(tr('Очистить журнал', 'Clear log'))

    def refresh_theme(self):
        pass

    def append(self, text: str):
        self.log_view.append(text)
        sb = self.log_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _copy(self):
        QApplication.clipboard().setText(self.log_view.toPlainText())

    def clear(self):
        self.log_view.clear()
