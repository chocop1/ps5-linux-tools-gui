# -*- coding: utf-8 -*-
"""
Background QThread workers for executing privileged commands and polling
systemd units, sysfs sensors, displays, M.2 storage, and kernel/service journals.
"""

import os
import subprocess
import sys
from pathlib import Path
from PyQt6.QtCore import QThread, pyqtSignal

from .config import (
    HELPER_INSTALL_PATH,
    POLICY_INSTALL_PATH,
    get_ps5_services,
    is_polkit_helper_installed,
    is_ru,
    resolve_asset,
    resolve_tool,
    tr,
)
from . import sysinfo


def build_privileged_cmd(action: str, args: list[str] | None = None) -> list[str]:
    """
    Build a root-privileged command list.
    Uses /usr/local/libexec/ps5tools-helper + polkit policy (with auth_admin_keep
    caching) when installed along with required sbin tools; otherwise falls back to
    safe direct pkexec commands with absolute paths.
    """
    args = args or []
    helper_ok = is_polkit_helper_installed()

    # For m2-* actions the helper invokes binaries in /usr/local/sbin
    if helper_ok and action.startswith('m2-') and action != 'm2-unmount':
        needed = {
            'm2-init': '/usr/local/sbin/m2_init',
            'm2-mount': '/usr/local/sbin/m2_install.sh',
            'm2-install': '/usr/local/sbin/m2_install.sh',
            'm2-exec': '/usr/local/sbin/m2_exec.sh',
        }.get(action)
        if needed and not Path(needed).is_file():
            helper_ok = False
    if helper_ok and action == 'warm-reboot' and not Path('/usr/local/sbin/ps5-linux-warm-reboot.sh').is_file():
        helper_ok = False

    if helper_ok:
        return [HELPER_INSTALL_PATH, action, *args]

    # --- Fallback without installed system helper ---
    if action == 'fan':
        return [resolve_tool('ps5_control'), '--fan', args[0]]
    if action == 'boost':
        return [resolve_tool('ps5_control'), '--boost', args[0]]

    if action == 'systemctl':
        op, unit = args[0], args[1]
        if op == 'enable-now':
            return ['systemctl', 'enable', '--now', unit]
        if op == 'disable-now':
            return ['systemctl', 'disable', '--now', unit]
        return ['systemctl', op, unit]

    if action in ('fan-pwm', 'fan-auto', 'fan-curve', 'fan-curve-reset', 'fan-curve-persist', 'm2-unmount'):
        # Invoke via helper directly if available outside AppImage FUSE
        helper_local = resolve_tool('ps5tools-root-helper')
        if Path(helper_local).is_file():
            # m2-init/install use the fallback above, while sysfs/unmount run directly through helper
            return [sys.executable, helper_local, action, *args]

    if action == 'm2-init':
        return [resolve_tool('m2_init')]
    if action == 'm2-mount':
        return [resolve_tool('m2_install.sh'), '--mount', args[0]]
    if action == 'm2-install':
        return [resolve_tool('m2_install.sh'), '--install', args[0]]
    if action == 'm2-exec':
        return [resolve_tool('m2_exec.sh')]
    if action == 'warm-reboot':
        return [resolve_tool('ps5-linux-warm-reboot.sh')]

    raise ValueError(tr(f'Не удалось сформировать команду для действия: {action}',
                        f'Failed to build command for action: {action}'))


def build_install_integration_cmd() -> list[str]:
    """
    Build a single-shot privileged command that installs system integration files:
    - /usr/local/libexec/ps5tools-helper
    - /usr/share/polkit-1/actions/org.ps5linuxtools.policy
    - missing helper binaries/scripts into /usr/local/sbin/
    """
    helper_src = resolve_tool('ps5tools-root-helper')
    policy_src = resolve_asset('org.ps5linuxtools.policy')
    if not Path(helper_src).is_file() or not policy_src:
        raise FileNotFoundError(tr('Не найдены ps5tools-root-helper или org.ps5linuxtools.policy',
                                   'ps5tools-root-helper or org.ps5linuxtools.policy not found'))

    # Copy helper/policy inline via python -c so root pkexec can read them even from AppImage FUSE
    helper_text = Path(helper_src).read_text(encoding='utf-8')
    policy_text = Path(policy_src).read_text(encoding='utf-8')

    tool_srcs = {}
    for tname in ('ps5_control', 'm2_init', 'm2_install.sh', 'm2_exec.sh', 'ps5-linux-warm-reboot.sh'):
        p = resolve_tool(tname)
        if Path(p).is_file() and not p.startswith('/usr/local/sbin/'):
            tool_srcs[tname] = p

    script = (
        'import os, shutil, pathlib\n'
        'os.makedirs("/usr/local/libexec", exist_ok=True)\n'
        'os.makedirs("/usr/share/polkit-1/actions", exist_ok=True)\n'
        'os.makedirs("/usr/local/sbin", exist_ok=True)\n'
        f'pathlib.Path({HELPER_INSTALL_PATH!r}).write_text({helper_text!r}, encoding="utf-8")\n'
        f'os.chmod({HELPER_INSTALL_PATH!r}, 0o755)\n'
        f'pathlib.Path({POLICY_INSTALL_PATH!r}).write_text({policy_text!r}, encoding="utf-8")\n'
        f'os.chmod({POLICY_INSTALL_PATH!r}, 0o644)\n'
        f'for name, src in {tool_srcs!r}.items():\n'
        '    dst = f"/usr/local/sbin/{name}"\n'
        '    if os.path.isfile(src):\n'
        '        shutil.copy2(src, dst)\n'
        '        os.chmod(dst, 0o755)\n'
        f'print({tr("Системная интеграция (helper + polkit + sbin) успешно установлена!", "System integration (helper + polkit + sbin) installed successfully!")!r})\n'
    )
    return [sys.executable, '-c', script]


class CommandRunner(QThread):
    """Run a command in a background thread, wrapping with pkexec when needed."""
    finished = pyqtSignal(str, str, int)  # stdout, stderr, returncode

    def __init__(self, cmd_list: list[str], needs_root: bool = True, timeout: int = 120, parent=None):
        super().__init__(parent)
        self.cmd_list = cmd_list
        self.needs_root = needs_root
        self.timeout = timeout
        self._ru = is_ru()

    def run(self):
        if self.needs_root and os.geteuid() != 0:
            cmd = ['pkexec', *self.cmd_list]
        else:
            cmd = self.cmd_list
        try:
            r = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=self.timeout,
            )
            self.finished.emit(r.stdout.strip(), r.stderr.strip(), r.returncode)
        except subprocess.TimeoutExpired:
            msg = f'Таймаут выполнения ({self.timeout} с)' if self._ru else f'Execution timeout ({self.timeout} s)'
            self.finished.emit('', msg, -1)
        except FileNotFoundError as exc:
            msg = f'Команда не найдена: {exc}' if self._ru else f'Command not found: {exc}'
            self.finished.emit('', msg, -1)
        except Exception as exc:
            self.finished.emit('', str(exc), -1)


class StatusChecker(QThread):
    """Query all PS5 systemd unit states with a single `systemctl show` call."""
    result = pyqtSignal(dict)

    def run(self):
        units = [u for u, _ in get_ps5_services()]
        statuses: dict[str, dict] = {
            u: {'active': None, 'substate': '', 'enabled': None} for u in units
        }
        try:
            r = subprocess.run(
                ['systemctl', 'show', '--property=Id,ActiveState,SubState,UnitFileState', *units],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if r.returncode == 0:
                cur: dict[str, str] = {}
                for line in r.stdout.splitlines() + ['']:
                    if not line.strip():
                        uid = cur.get('Id')
                        if uid in statuses:
                            act = cur.get('ActiveState', '')
                            uf = cur.get('UnitFileState', '')
                            statuses[uid] = {
                                'active': (act == 'active') if act else None,
                                'active_state': act or 'unknown',
                                'substate': cur.get('SubState', ''),
                                'enabled': (uf in ('enabled', 'static', 'enabled-runtime')) if uf else False,
                                'unit_file_state': uf or 'not-installed',
                            }
                        cur = {}
                    elif '=' in line:
                        k, v = line.split('=', 1)
                        cur[k] = v
        except Exception:
            pass
        self.result.emit(statuses)


class SensorPoller(QThread):
    """Asynchronously collect sysfs sensor readings."""
    result = pyqtSignal(dict)

    def run(self):
        self.result.emit(sysinfo.poll_sensors())


class DisplayPoller(QThread):
    """Asynchronously collect display (kscreen-doctor -j) and peripheral info."""
    result = pyqtSignal(dict)

    def run(self):
        self.result.emit(sysinfo.poll_display_and_peripherals())


class M2Poller(QThread):
    """Asynchronously collect lsblk, M.2 mount, and EFI file info."""
    result = pyqtSignal(dict)

    def run(self):
        self.result.emit(sysinfo.poll_m2_environment())


class JournalPoller(QThread):
    """Read kernel (`journalctl -k`) or service (`journalctl -u`) journal logs."""
    result = pyqtSignal(str, str)  # header, text

    def __init__(self, mode: str = 'kernel', unit: str = '', filter_regex: str = '', lines: int = 180, parent=None):
        super().__init__(parent)
        self.mode = mode
        self.unit = unit
        self.filter_regex = filter_regex
        self.lines = lines
        self._ru = is_ru()

    def run(self):
        try:
            if self.mode == 'unit' and self.unit:
                cmd = ['journalctl', '-u', self.unit, '-n', str(self.lines), '--no-pager', '-b']
                title = (
                    f'Журнал службы {self.unit} (текущая загрузка)'
                    if self._ru else
                    f'Service log: {self.unit} (current boot)'
                )
            elif self.mode == 'kernel_err':
                cmd = ['journalctl', '-k', '-p', 'err..emerg', '-n', str(self.lines), '--no-pager', '-b']
                title = 'Ошибки ядра (текущая загрузка)' if self._ru else 'Kernel errors (current boot)'
            else:
                cmd = ['journalctl', '-k', '-n', str(self.lines), '--no-pager', '-b']
                title = 'Журнал ядра (текущая загрузка)' if self._ru else 'Kernel log (current boot)'
                if self.filter_regex:
                    cmd.extend(['-g', self.filter_regex])
                    flt_lbl = 'фильтр' if self._ru else 'filter'
                    title += f' [{flt_lbl}: {self.filter_regex}]'

            r = subprocess.run(cmd, capture_output=True, text=True, timeout=6)
            empty_msg = '(Записей не найдено)' if self._ru else '(No entries found)'
            out = r.stdout.strip() or r.stderr.strip() or empty_msg
            self.result.emit(title, out)
        except Exception as exc:
            self.result.emit('Ошибка чтения журнала' if self._ru else 'Journal read error', str(exc))
