# -*- coding: utf-8 -*-
"""
M.2 SSD tab:
- block device overview (lsblk) and EFI boot status (/boot/efi)
- safety guard preventing nvme0n1 overwrite when current OS is booted from it (/)
- M2 Init, .img selection, Mount, Unmount, Open in Dolphin, Install
- kexec Linux from M.2 and Warm Reboot
"""

import os
import shutil
import subprocess
from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QFileDialog, QGroupBox, QHBoxLayout, QHeaderView, QLabel,
    QMessageBox, QPushButton, QScrollArea, QSizePolicy,
    QTableWidget, QTableWidgetItem, QVBoxLayout, QWidget,
)

from ..config import KDETheme, tr


class M2Tab(QScrollArea):
    """Tab for M.2 NVMe SSD management and kernel boot."""

    def __init__(self, theme: KDETheme, run_action_cb, refresh_m2_cb, log_cb, parent=None):
        super().__init__(parent)
        self._theme = theme
        self._run_action = run_action_cb
        self._refresh_m2 = refresh_m2_cb
        self._log = log_cb
        self._img_path = ''
        self._m2_state: dict = {}
        self._action_buttons: list[QWidget] = []

        self.setWidgetResizable(True)
        self.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        inner = QWidget()
        layout = QVBoxLayout(inner)
        layout.setSpacing(10)
        layout.setContentsMargins(12, 12, 12, 12)
        self.setWidget(inner)

        # ── 1. Storage and EFI overview ─────────────────────────────────────
        self._grp_info = QGroupBox()
        info_v = QVBoxLayout(self._grp_info)

        self.tbl_disks = QTableWidget(0, 5)
        self.tbl_disks.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeMode.ResizeToContents)
        self.tbl_disks.horizontalHeader().setStretchLastSection(True)
        self.tbl_disks.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.tbl_disks.setSelectionMode(QTableWidget.SelectionMode.NoSelection)
        self.tbl_disks.verticalHeader().setVisible(False)
        self.tbl_disks.setMinimumHeight(125)
        self.tbl_disks.setMaximumHeight(175)
        info_v.addWidget(self.tbl_disks)

        efi_row = QHBoxLayout()
        self.lbl_efi_status = QLabel('EFI: …')
        self.lbl_efi_status.setWordWrap(True)
        self._btn_ref_m2 = QPushButton()
        self._btn_ref_m2.clicked.connect(self._refresh_m2)
        efi_row.addWidget(self.lbl_efi_status, stretch=1)
        efi_row.addWidget(self._btn_ref_m2)
        info_v.addLayout(efi_row)

        self.lbl_root_guard = QLabel('')
        self.lbl_root_guard.setWordWrap(True)
        self.lbl_root_guard.hide()
        info_v.addWidget(self.lbl_root_guard)
        layout.addWidget(self._grp_info)

        # ── 2. M.2 initialization ───────────────────────────────────────────
        self._grp_init = QGroupBox()
        init_v = QVBoxLayout(self._grp_init)

        self._warn_init = QLabel()
        self._warn_init.setWordWrap(True)
        init_v.addWidget(self._warn_init)

        self.btn_m2_init = QPushButton()
        self.btn_m2_init.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        init_v.addWidget(self.btn_m2_init)
        self._action_buttons.append(self.btn_m2_init)
        layout.addWidget(self._grp_init)

        # ── 3. Image operations ─────────────────────────────────────────────
        self._grp_img = QGroupBox()
        img_v = QVBoxLayout(self._grp_img)

        file_row = QHBoxLayout()
        self.btn_choose_img = QPushButton()
        self.lbl_img_path = QLabel()
        self.lbl_img_path.setWordWrap(True)
        file_row.addWidget(self.btn_choose_img)
        file_row.addWidget(self.lbl_img_path, stretch=1)
        img_v.addLayout(file_row)

        self.lbl_mount_state = QLabel()
        img_v.addWidget(self.lbl_mount_state)

        act_row = QHBoxLayout()
        self.btn_mount = QPushButton()
        self.btn_unmount = QPushButton()
        self.btn_open_mnt = QPushButton()
        self.btn_install = QPushButton()

        for b in (self.btn_mount, self.btn_unmount, self.btn_open_mnt, self.btn_install):
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            act_row.addWidget(b)
        img_v.addLayout(act_row)
        self._action_buttons.extend([self.btn_choose_img, self.btn_mount, self.btn_unmount, self.btn_install])
        layout.addWidget(self._grp_img)

        # ── 4. Boot / kexec ─────────────────────────────────────────────────
        self._grp_boot = QGroupBox()
        boot_v = QVBoxLayout(self._grp_boot)

        self._warn_boot = QLabel()
        self._warn_boot.setWordWrap(True)
        boot_v.addWidget(self._warn_boot)

        boot_row = QHBoxLayout()
        self.btn_kexec = QPushButton()
        self.btn_warm_reboot = QPushButton()
        for b in (self.btn_kexec, self.btn_warm_reboot):
            b.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
            boot_row.addWidget(b)
            self._action_buttons.append(b)
        boot_v.addLayout(boot_row)
        layout.addWidget(self._grp_boot)

        layout.addStretch()

        # Signals
        self.btn_m2_init.clicked.connect(self._on_m2_init)
        self.btn_choose_img.clicked.connect(self._on_choose_img)
        self.btn_mount.clicked.connect(self._on_mount)
        self.btn_unmount.clicked.connect(
            lambda: self._run_action('m2-unmount', [], 'umount /mnt/ps5_image_src', on_done=self._refresh_m2)
        )
        self.btn_open_mnt.clicked.connect(self._on_open_mnt)
        self.btn_install.clicked.connect(self._on_install)
        self.btn_kexec.clicked.connect(self._on_kexec)
        self.btn_warm_reboot.clicked.connect(self._on_warm_reboot)

        self.retranslate_ui()
        self.refresh_theme()

    def retranslate_ui(self):
        self._grp_info.setTitle(tr('💽  Состояние дисков и /boot/efi', '💽  Disks & /boot/efi Status'))
        self.tbl_disks.setHorizontalHeaderLabels([
            tr('Устройство', 'Device'),
            tr('Размер', 'Size'),
            tr('Метка', 'Label'),
            tr('ФС', 'FS'),
            tr('Точка монтирования', 'Mountpoint'),
        ])
        self._btn_ref_m2.setText(tr('↺  Обновить диски', '↺  Refresh Disks'))
        self._btn_ref_m2.setToolTip(tr(
            'Повторно опросить разделы lsblk, монтирование образа и /boot/efi',
            'Re-scan lsblk partitions, image mount state, and /boot/efi',
        ))

        self._grp_init.setTitle(tr('🔧  Инициализация M.2 (/dev/nvme0n1)', '🔧  M.2 Initialization (/dev/nvme0n1)'))
        self._warn_init.setText(tr(
            '⚠  Создаёт гибридную MBR + GPT разметку для PS5 OS. Все данные на /dev/nvme0n1 будут стёрты!',
            '⚠  Creates a hybrid MBR + GPT layout for PS5 OS. All data on /dev/nvme0n1 will be erased!',
        ))
        self.btn_m2_init.setText(tr('🔧  M2 Init (инициализация GPT/MBR)', '🔧  M2 Init (GPT/MBR initialization)'))
        self.btn_m2_init.setToolTip(tr(
            '<b>M2 Init ⚠</b><br>Создаёт фиктивный MBR + GPT с выравненным разделом на <code>/dev/nvme0n1</code>.<br>'
            'Выполняет: <code>m2_init</code>',
            '<b>M2 Init ⚠</b><br>Creates a hybrid MBR + aligned GPT partition on <code>/dev/nvme0n1</code>.<br>'
            'Runs: <code>m2_init</code>',
        ))

        self._grp_img.setTitle(tr('💿  Образ Linux (.img)', '💿  Linux Image (.img)'))
        self.btn_choose_img.setText(tr('📂  Выбрать образ…', '📂  Choose image…'))
        self.btn_choose_img.setToolTip(tr('Выбрать .img / .raw образ системы для монтирования или установки', 'Select .img / .raw system image to mount or install'))
        if not self._img_path:
            self.lbl_img_path.setText(tr('(файл не выбран)', '(no file selected)'))

        self.btn_mount.setText(tr('💿  Смонтировать', '💿  Mount'))
        self.btn_unmount.setText(tr('⏏  Размонтировать', '⏏  Unmount'))
        self.btn_open_mnt.setText(tr('📁  Открыть в Dolphin', '📁  Open in File Manager'))
        self.btn_install.setText(tr('📥  Установить на M.2', '📥  Install to M.2'))

        self.btn_mount.setToolTip(tr(
            '<b>Смонтировать образ</b><br>Подключает раздел образа через <code>losetup</code> в <code>/mnt/ps5_image_src</code>.<br>'
            'Выполняет: <code>m2_install.sh --mount &lt;img&gt;</code>',
            '<b>Mount Image</b><br>Attaches image partition via <code>losetup</code> at <code>/mnt/ps5_image_src</code>.<br>'
            'Runs: <code>m2_install.sh --mount &lt;img&gt;</code>',
        ))
        self.btn_unmount.setToolTip(tr(
            '<b>Размонтировать образ</b><br>Выполняет <code>umount -R /mnt/ps5_image_src</code> и освобождает loop-устройство.',
            '<b>Unmount Image</b><br>Runs <code>umount -R /mnt/ps5_image_src</code> and detaches the loop device.',
        ))
        self.btn_open_mnt.setToolTip(tr('Открыть <code>/mnt/ps5_image_src</code> в файловом менеджере', 'Open <code>/mnt/ps5_image_src</code> in file manager'))
        self.btn_install.setToolTip(tr(
            '<b>Установить на M.2 ⚠</b><br>Форматирует <code>/dev/nvme0n1p1</code> в ext4, копирует образ и создаёт <code>/etc/fstab</code>.<br>'
            'Выполняет: <code>m2_install.sh --install &lt;img&gt;</code>',
            '<b>Install to M.2 ⚠</b><br>Formats <code>/dev/nvme0n1p1</code> as ext4, copies image contents, and creates <code>/etc/fstab</code>.<br>'
            'Runs: <code>m2_install.sh --install &lt;img&gt;</code>',
        ))

        self._grp_boot.setTitle(tr('🚀  Загрузка (kexec / Warm Reboot)', '🚀  Boot (kexec / Warm Reboot)'))
        self._warn_boot.setText(tr(
            '⚠  Немедленно переключает или перезагружает систему! Сохраните открытые файлы.',
            '⚠  Immediately switches or reboots the system! Save open files first.',
        ))
        self.btn_kexec.setText(tr('🚀  kexec Linux с M.2', '🚀  kexec Linux from M.2'))
        self.btn_warm_reboot.setText('🔄  Warm Reboot')
        self.btn_kexec.setToolTip(tr(
            '<b>kexec с M.2</b><br>Загружает ядро и initrd с <code>/dev/nvme0n1p1</code> без перезагрузки железа.<br>'
            'Выполняет: <code>m2_exec.sh</code>',
            '<b>kexec from M.2</b><br>Boots kernel and initrd from <code>/dev/nvme0n1p1</code> without full hardware reset.<br>'
            'Runs: <code>m2_exec.sh</code>',
        ))
        self.btn_warm_reboot.setToolTip(tr(
            '<b>Warm Reboot</b><br>Загружает ядро из <code>/boot/efi/bzImage</code> + <code>cmdline.txt</code>.<br>'
            'Выполняет: <code>ps5-linux-warm-reboot.sh</code>',
            '<b>Warm Reboot</b><br>Boots kernel from <code>/boot/efi/bzImage</code> + <code>cmdline.txt</code>.<br>'
            'Runs: <code>ps5-linux-warm-reboot.sh</code>',
        ))

        if self._m2_state:
            self.update_m2_data(self._m2_state)
        else:
            self.lbl_efi_status.setText(tr('EFI: опрос…', 'EFI: polling…'))
            self.lbl_mount_state.setText(tr('Точка монтирования /mnt/ps5_image_src: не смонтирована',
                                            'Mountpoint /mnt/ps5_image_src: not mounted'))
        self.refresh_theme()

    def refresh_theme(self):
        self._warn_init.setStyleSheet(f'color: {self._theme.negative()};')
        self._warn_boot.setStyleSheet(f'color: {self._theme.negative()};')
        if not self._img_path:
            self.lbl_img_path.setStyleSheet(f'color: {self._theme.muted()};')
        if self._m2_state:
            self.update_m2_data(self._m2_state)
        else:
            self.lbl_mount_state.setStyleSheet(f'color: {self._theme.muted()};')

    def action_widgets(self) -> list[QWidget]:
        return self._action_buttons

    def update_m2_data(self, data: dict):
        self._m2_state = data

        # Block devices table
        devs = data.get('devices', [])
        self.tbl_disks.setRowCount(len(devs))
        for r, d in enumerate(devs):
            vals = [d['name'], d['size'], d['label'], d['fstype'], d['mountpoint']]
            for c, txt in enumerate(vals):
                item = QTableWidgetItem(txt)
                if d['mountpoint'] == '/':
                    font = item.font()
                    font.setBold(True)
                    item.setFont(font)
                self.tbl_disks.setItem(r, c, item)

        # EFI files status
        efi = data.get('efi_files', {})
        parts = []
        for fname, ok in efi.items():
            col = self._theme.positive() if ok else self._theme.negative()
            mark = '✓' if ok else '✗'
            parts.append(f'<span style="color:{col}">{mark} {fname}</span>')
        vram_txt = data.get('efi_vram', '')
        vram_info = f' | VRAM cfg: <code>{vram_txt}</code>' if vram_txt else ''
        self.lbl_efi_status.setText('<b>/boot/efi:</b> ' + ' &nbsp; '.join(parts) + vram_info)

        # --mount image mount status
        mounted = data.get('img_mounted', False)
        src = data.get('img_mount_src', '')
        if mounted:
            self.lbl_mount_state.setText(tr(
                f'Точка монтирования /mnt/ps5_image_src: ✓ смонтирован ({src})',
                f'Mountpoint /mnt/ps5_image_src: ✓ mounted ({src})',
            ))
            self.lbl_mount_state.setStyleSheet(f'color: {self._theme.positive()}; font-weight: bold;')
        else:
            self.lbl_mount_state.setText(tr(
                'Точка монтирования /mnt/ps5_image_src: не смонтирована',
                'Mountpoint /mnt/ps5_image_src: not mounted',
            ))
            self.lbl_mount_state.setStyleSheet(f'color: {self._theme.muted()};')
        self.btn_unmount.setEnabled(mounted)
        self.btn_open_mnt.setEnabled(mounted)

        # Safety guard: lock destructive actions if current OS is booted from nvme0n1*
        is_root = data.get('nvme_is_root', False)
        if is_root:
            self.lbl_root_guard.setText(tr(
                '🛡️ <b>Защита активна:</b> текущая ОС работает с <code>/dev/nvme0n1p1</code> (<code>/</code>). '
                'Операции <b>M2 Init</b> и <b>Установить на M.2</b> заблокированы, чтобы не уничтожить работающую систему.',
                '🛡️ <b>Root guard active:</b> current OS runs from <code>/dev/nvme0n1p1</code> (<code>/</code>). '
                '<b>M2 Init</b> and <b>Install to M.2</b> are locked to prevent destroying the running system.',
            ))
            self.lbl_root_guard.setStyleSheet(f'color: {self._theme.neutral()};')
            self.lbl_root_guard.show()
            self.btn_m2_init.setEnabled(False)
            self.btn_install.setEnabled(False)
        else:
            self.lbl_root_guard.hide()

    def _on_m2_init(self):
        if self._m2_state.get('nvme_is_root'):
            QMessageBox.critical(
                self,
                tr('Операция заблокирована', 'Operation Blocked'),
                tr('Нельзя переразметить диск с текущей запущенной ОС (/).', 'Cannot repartition the disk running the active OS (/).'),
            )
            return
        reply = QMessageBox.warning(
            self,
            tr('Подтверждение — M2 Init', 'Confirmation — M2 Init'),
            tr(
                '⚠ Эта операция перезапишет таблицу разделов на /dev/nvme0n1!\n\n'
                'Все данные на M.2 SSD будут уничтожены.\nПродолжить?',
                '⚠ This will overwrite the partition table on /dev/nvme0n1!\n\n'
                'All data on the M.2 SSD will be destroyed.\nContinue?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._run_action('m2-init', [], 'm2_init', on_done=self._refresh_m2)

    def _on_choose_img(self):
        path, _ = QFileDialog.getOpenFileName(
            self,
            tr('Выбрать образ диска', 'Select Disk Image'),
            os.path.expanduser('~'),
            tr('Образы дисков (*.img *.iso *.raw);;Все файлы (*)', 'Disk images (*.img *.iso *.raw);;All files (*)'),
        )
        if path:
            self._img_path = path
            self.lbl_img_path.setText(path)
            self.lbl_img_path.setStyleSheet('')

    def _on_mount(self):
        if not self._img_path:
            QMessageBox.information(self, tr('Файл не выбран', 'No File Selected'), tr('Сначала выберите файл образа (.img).', 'Select an image file (.img) first.'))
            return
        self._run_action(
            'm2-mount', [self._img_path],
            f'm2_install.sh --mount {self._img_path}',
            on_done=self._refresh_m2,
        )

    def _on_open_mnt(self):
        mnt = self._m2_state.get('img_mount_dir', '/mnt/ps5_image_src')
        opener = shutil.which('dolphin') or shutil.which('xdg-open')
        if opener:
            subprocess.Popen([opener, mnt])
        else:
            self._log(tr(f'Точка монтирования: {mnt} (файловый менеджер не найден)', f'Mountpoint: {mnt} (file manager not found)'))

    def _on_install(self):
        if self._m2_state.get('nvme_is_root'):
            QMessageBox.critical(
                self,
                tr('Операция заблокирована', 'Operation Blocked'),
                tr('Нельзя форматировать /dev/nvme0n1p1, пока он смонтирован как /.', 'Cannot format /dev/nvme0n1p1 while mounted as /.'),
            )
            return
        if not self._img_path:
            QMessageBox.information(self, tr('Файл не выбран', 'No File Selected'), tr('Сначала выберите файл образа (.img).', 'Select an image file (.img) first.'))
            return
        reply = QMessageBox.warning(
            self,
            tr('Подтверждение — Установка на M.2', 'Confirmation — Install to M.2'),
            tr(
                f'⚠ Образ будет развёрнут на /dev/nvme0n1p1:\n{self._img_path}\n\n'
                'Раздел будет отформатирован в ext4! Все данные на нём будут уничтожены!\nПродолжить?',
                f'⚠ Image will be flashed onto /dev/nvme0n1p1:\n{self._img_path}\n\n'
                'Partition will be formatted as ext4! All data on it will be destroyed!\nContinue?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._run_action(
                'm2-install', [self._img_path],
                f'm2_install.sh --install {self._img_path}',
                on_done=self._refresh_m2,
            )

    def _on_kexec(self):
        if not self._m2_state.get('nvme_part_exists', True):
            QMessageBox.warning(self, tr('Раздел не найден', 'Partition Not Found'), tr('/dev/nvme0n1p1 не обнаружен в системе.', '/dev/nvme0n1p1 was not detected in the system.'))
            return
        reply = QMessageBox.warning(
            self,
            tr('Подтверждение — kexec', 'Confirmation — kexec'),
            tr(
                '⚠ kexec немедленно загрузит ядро Linux с /dev/nvme0n1p1.\n'
                'Несохранённые данные будут потеряны. Продолжить?',
                '⚠ kexec will immediately boot Linux from /dev/nvme0n1p1.\n'
                'Unsaved work will be lost. Continue?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._run_action('m2-exec', [], 'm2_exec.sh (kexec)')

    def _on_warm_reboot(self):
        efi = self._m2_state.get('efi_files', {})
        if efi and (not efi.get('bzImage') or not efi.get('cmdline.txt')):
            QMessageBox.warning(
                self,
                tr('Файлы EFI отсутствуют', 'Missing EFI Files'),
                tr(
                    'В /boot/efi не найдены bzImage или cmdline.txt — warm-reboot может не сработать.',
                    'bzImage or cmdline.txt missing in /boot/efi — warm-reboot may fail.',
                ),
            )
            return
        reply = QMessageBox.warning(
            self,
            tr('Подтверждение — Warm Reboot', 'Confirmation — Warm Reboot'),
            tr(
                '⚠ Система немедленно выполнит тёплую перезагрузку через /boot/efi/bzImage.\n'
                'Несохранённые данные будут потеряны. Продолжить?',
                '⚠ System will immediately warm-reboot via /boot/efi/bzImage.\n'
                'Unsaved work will be lost. Continue?',
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            QMessageBox.StandardButton.No,
        )
        if reply == QMessageBox.StandardButton.Yes:
            self._run_action('warm-reboot', [], 'ps5-linux-warm-reboot.sh')
