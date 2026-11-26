# -*- coding: utf-8 -*-
"""
Read PS5 Linux system information and sensors from sysfs, procfs, kscreen-doctor, and lsblk.
All file operations use context managers (Path.read_text).
"""

import glob
import json
import os
import platform
import re
import subprocess
from pathlib import Path

from .config import tr

ANSI_ESCAPE_RE = re.compile(r'\x1b\[[0-9;]*[A-Za-z]')
DRM_CARD_RE = re.compile(r'^card\d+$')


def read_text(path: str | Path, default=None):
    """Safely read a sysfs/procfs text file and strip whitespace."""
    try:
        return Path(path).read_text(encoding='utf-8', errors='replace').strip()
    except OSError:
        return default


def read_int(path: str | Path, default=None) -> int | None:
    raw = read_text(path)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError:
        return default


def find_hwmon(driver_name: str) -> Path | None:
    """Find /sys/class/hwmon/hwmonX matching the given driver name."""
    for p in sorted(Path('/sys/class/hwmon').glob('hwmon*')):
        if read_text(p / 'name') == driver_name:
            return p
    return None


def find_amdgpu_card() -> Path | None:
    """Dynamically locate the AMD GPU DRM card (/sys/class/drm/cardX)."""
    drm_root = Path('/sys/class/drm')
    cards = [
        p for p in sorted(drm_root.glob('card*'))
        if DRM_CARD_RE.match(p.name)
    ]
    for card in cards:
        vendor = read_text(card / 'device' / 'vendor', '')
        uevent = read_text(card / 'device' / 'uevent', '')
        if vendor == '0x1002' or 'DRIVER=amdgpu' in uevent:
            return card
    return cards[0] if cards else None


def detect_ps5_info() -> dict:
    """Detect PS5 model from GPU PCI ID, kernel version, and session parameters."""
    card = find_amdgpu_card()
    pci_dev = ''
    vbios = '—'
    vram_total_mb = None
    if card:
        dev_dir = card / 'device'
        vendor = read_text(dev_dir / 'vendor', '').lower()
        device = read_text(dev_dir / 'device', '').lower()
        pci_dev = f'{vendor}:{device}' if vendor and device else ''
        vbios = read_text(dev_dir / 'vbios_version', '—')
        vram_bytes = read_int(dev_dir / 'mem_info_vram_total')
        if vram_bytes:
            vram_total_mb = round(vram_bytes / (1024 * 1024))

    model_map = {
        '0x1002:0x13fc': 'PlayStation 5 (Oberon)',
        '0x1002:0x13da': 'PlayStation 5 Pro (Viola)',
    }
    model = model_map.get(pci_dev, f'PS5 APU ({pci_dev or tr("неизвестно", "unknown")})')
    return {
        'model': model,
        'pci_id': pci_dev,
        'kernel': platform.release(),
        'vbios': vbios,
        'vram_total_mb': vram_total_mb,
        'session_type': os.environ.get('XDG_SESSION_TYPE', 'unknown'),
    }


def read_cpu_freq_mhz() -> int | None:
    """Read current CPU frequency from cpufreq or average across /proc/cpuinfo."""
    khz = read_int('/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq')
    if khz is not None and khz > 0:
        return round(khz / 1000)

    raw = read_text('/proc/cpuinfo')
    if not raw:
        return None
    freqs = []
    for line in raw.splitlines():
        if line.startswith('cpu MHz'):
            try:
                freqs.append(float(line.split(':', 1)[1]))
            except ValueError:
                pass
    return round(sum(freqs) / len(freqs)) if freqs else None


def poll_sensors() -> dict:
    """Collect all sensor metrics: ps5_fan, amdgpu, VRAM/GTT, nvme, k10temp, and CPU MHz."""
    data: dict = {}

    # 1. ps5_fan (4 thermal sensors + PWM)
    h_fan = find_hwmon('ps5_fan')
    if h_fan:
        data['ps5_fan_path'] = str(h_fan)
        for i in range(1, 5):
            raw = read_int(h_fan / f'temp{i}_input')
            lbl = read_text(h_fan / f'temp{i}_label', f'Temp{i}')
            fault = read_text(h_fan / f'temp{i}_fault', '0')
            if raw is not None:
                data[f'ps5fan_temp{i}'] = raw / 1000.0
                data[f'ps5fan_temp{i}_label'] = lbl
                data[f'ps5fan_temp{i}_fault'] = (fault != '0')
        pwm = read_int(h_fan / 'pwm1')
        if pwm is not None:
            data['fan_pwm'] = pwm
            data['fan_pwm_pct'] = round(pwm / 255 * 100)
        pwm_en = read_int(h_fan / 'pwm1_enable')
        if pwm_en is not None:
            data['fan_pwm_enable'] = pwm_en

    # 2. amdgpu hwmon
    h_gpu = find_hwmon('amdgpu')
    if h_gpu:
        raw_t = read_int(h_gpu / 'temp1_input')
        if raw_t is not None:
            data['gpu_temp'] = raw_t / 1000.0
        freq = read_int(h_gpu / 'freq1_input')
        if freq is not None:
            data['gpu_freq_mhz'] = freq // 1_000_000

    # 3. DRM device: busy%, runtime PM, power_state, VRAM/GTT
    card = find_amdgpu_card()
    if card:
        dev = card / 'device'
        busy = read_int(dev / 'gpu_busy_percent')
        if busy is not None:
            data['gpu_busy'] = busy
        data['gpu_pm'] = read_text(dev / 'power' / 'runtime_status', 'unknown')
        data['gpu_power_state'] = read_text(dev / 'power_state', '—')

        vram_used = read_int(dev / 'mem_info_vram_used')
        vram_total = read_int(dev / 'mem_info_vram_total')
        if vram_used is not None and vram_total:
            data['vram_used_mb'] = round(vram_used / (1024 * 1024))
            data['vram_total_mb'] = round(vram_total / (1024 * 1024))

        gtt_used = read_int(dev / 'mem_info_gtt_used')
        gtt_total = read_int(dev / 'mem_info_gtt_total')
        if gtt_used is not None and gtt_total:
            data['gtt_used_mb'] = round(gtt_used / (1024 * 1024))
            data['gtt_total_mb'] = round(gtt_total / (1024 * 1024))

    # 4. NVMe
    h_nvme = find_hwmon('nvme')
    if h_nvme:
        for i in range(1, 3):
            raw = read_int(h_nvme / f'temp{i}_input')
            lbl = read_text(h_nvme / f'temp{i}_label', f'NVMe {i}')
            if raw is not None:
                data[f'nvme_temp{i}'] = raw / 1000.0
                data[f'nvme_temp{i}_label'] = lbl

    # 5. CPU (k10temp + MHz)
    h_cpu = find_hwmon('k10temp')
    if h_cpu:
        raw = read_int(h_cpu / 'temp1_input')
        if raw is not None:
            data['cpu_temp'] = raw / 1000.0

    cpu_mhz = read_cpu_freq_mhz()
    if cpu_mhz is not None:
        data['cpu_freq_mhz'] = cpu_mhz

    return data


def read_fan_curve() -> dict:
    """
    Read the 8-point fan curve from hwmon ps5_fan.
    Returns {'available': bool, 'hwmon': str, 'pwm_enable': int, 'pwm': int,
             'hyst_c': int, 'points': list[tuple[int, int]]}
    where points are [(temp_c, pwm_0_255), ...].
    """
    h_fan = find_hwmon('ps5_fan')
    if not h_fan:
        return {'available': False, 'points': []}

    points: list[tuple[int, int]] = []
    for i in range(1, 9):
        t_mc = read_int(h_fan / f'pwm1_auto_point{i}_temp')
        p_val = read_int(h_fan / f'pwm1_auto_point{i}_pwm')
        if t_mc is not None and p_val is not None:
            points.append((round(t_mc / 1000), p_val))

    hyst_mc = read_int(h_fan / 'pwm1_auto_point1_temp_hyst', 45000)
    return {
        'available': len(points) == 8,
        'hwmon': str(h_fan),
        'pwm_enable': read_int(h_fan / 'pwm1_enable', 2),
        'pwm': read_int(h_fan / 'pwm1', 0),
        'hyst_c': round((hyst_mc or 45000) / 1000),
        'points': points,
    }


def _parse_kscreen_extras() -> dict[str, dict]:
    """
    Extract Vrr/HDR fields from `kscreen-doctor -o` after stripping ANSI escape
    codes, since `kscreen-doctor -j` may omit them on some Plasma versions.
    """
    extras: dict[str, dict] = {}
    try:
        r = subprocess.run(
            ['kscreen-doctor', '-o'],
            capture_output=True, text=True, timeout=4
        )
        if r.returncode != 0:
            return extras
        cur = None
        for raw_line in r.stdout.splitlines():
            line = ANSI_ESCAPE_RE.sub('', raw_line).strip()
            if line.startswith('Output:'):
                parts = line.split()
                if len(parts) >= 3:
                    cur = parts[2]
                    extras[cur] = {'vrr': '—', 'hdr': '—'}
            elif cur and line.startswith('Vrr:'):
                extras[cur]['vrr'] = line.split(':', 1)[1].strip()
            elif cur and line.startswith('HDR:'):
                extras[cur]['hdr'] = line.split(':', 1)[1].strip()
    except Exception:
        pass
    return extras


def _get_kscreen_displays() -> dict[str, dict]:
    """
    Read monitor configuration via `kscreen-doctor -j` (JSON),
    deduplicating modes by (width, height, round(Hz)) while keeping mode_id.
    """
    outputs: dict[str, dict] = {}
    try:
        r = subprocess.run(
            ['kscreen-doctor', '-j'],
            capture_output=True, text=True, timeout=5
        )
        if r.returncode != 0:
            return {}
        payload = json.loads(r.stdout)
    except Exception:
        return {}

    extras = _parse_kscreen_extras()

    for out in payload.get('outputs', []):
        name = out.get('name', '')
        if not name:
            continue
        cur_id = str(out.get('currentModeId', ''))
        pref_ids = {str(x) for x in out.get('preferredModes', [])}

        # Deduplicate modes, prioritizing the currently active mode id
        dedup: dict[tuple[int, int, int], dict] = {}
        for m in out.get('modes', []):
            mid = str(m.get('id', ''))
            sz = m.get('size', {})
            w = int(sz.get('width', 0))
            h = int(sz.get('height', 0))
            hz_exact = float(m.get('refreshRate', 0.0))
            hz_int = round(hz_exact)
            if not (w and h and hz_int):
                continue
            key = (w, h, hz_int)
            is_active = (mid == cur_id)
            is_pref = (mid in pref_ids)
            entry = {
                'id': mid,
                'label': f'{w}x{h}@{hz_int}',
                'width': w,
                'height': h,
                'hz': hz_int,
                'hz_exact': hz_exact,
                'active': is_active,
                'preferred': is_pref,
            }
            prev = dedup.get(key)
            if prev is None:
                dedup[key] = entry
            elif is_active or (is_pref and not prev['active']):
                dedup[key] = entry

        # Sort higher resolution and refresh rate first
        modes_sorted = sorted(
            dedup.values(),
            key=lambda x: (x['width'] * x['height'], x['hz']),
            reverse=True
        )

        active_label = next((m['label'] for m in modes_sorted if m['active']), '—')
        ext = extras.get(name, {})

        # Newer Plasma versions expose explicit VRR/HDR fields in JSON
        vrr_val = out.get('vrrPolicy')
        if vrr_val is not None:
            vrr_str = {0: 'never', 1: 'always', 2: 'automatic'}.get(vrr_val, str(vrr_val))
        else:
            vrr_str = ext.get('vrr', '—')

        hdr_val = out.get('hdr')
        if isinstance(hdr_val, bool):
            hdr_str = 'enabled' if hdr_val else 'disabled'
        else:
            hdr_str = ext.get('hdr', '—')

        outputs[name] = {
            'name': name,
            'id': out.get('id'),
            'connected': bool(out.get('connected', False)),
            'enabled': bool(out.get('enabled', False)),
            'current': active_label,
            'current_mode_id': cur_id,
            'modes': modes_sorted,
            'scale': float(out.get('scale', 1.0)),
            'brightness': float(out.get('brightness', 1.0)),
            'vrr': vrr_str,
            'hdr': hdr_str,
        }

    return outputs


def _get_xrandr_displays() -> dict[str, dict]:
    """Fallback xrandr parser for sessions without kscreen-doctor."""
    outputs: dict[str, dict] = {}
    try:
        r = subprocess.run(['xrandr', '--query'], capture_output=True, text=True, timeout=5)
        if r.returncode != 0:
            return {}
    except Exception:
        return {}

    cur = None
    for line in r.stdout.splitlines():
        if not line.startswith(' ') and 'connected' in line:
            parts = line.split()
            name = parts[0]
            connected = (parts[1] == 'connected')
            cur = name
            outputs[name] = {
                'name': name,
                'id': None,
                'connected': connected,
                'enabled': '+' in line,
                'current': '—',
                'current_mode_id': '',
                'modes': [],
                'scale': 1.0,
                'brightness': 1.0,
                'vrr': '—',
                'hdr': '—',
                '_seen': set(),
            }
        elif cur and line.startswith('   ') and outputs[cur]['connected']:
            parts = line.split()
            if len(parts) < 2 or 'x' not in parts[0]:
                continue
            try:
                w_str, h_str = parts[0].split('x', 1)
                w, h = int(w_str), int(h_str)
            except ValueError:
                continue
            for hz_tok in parts[1:]:
                is_active = '*' in hz_tok
                is_pref = '+' in hz_tok
                try:
                    hz_int = round(float(hz_tok.rstrip('*+')))
                except ValueError:
                    continue
                key = (w, h, hz_int)
                if key in outputs[cur]['_seen']:
                    continue
                outputs[cur]['_seen'].add(key)
                label = f'{w}x{h}@{hz_int}'
                outputs[cur]['modes'].append({
                    'id': label,
                    'label': label,
                    'width': w,
                    'height': h,
                    'hz': hz_int,
                    'active': is_active,
                    'preferred': is_pref,
                })
                if is_active:
                    outputs[cur]['current'] = label

    for v in outputs.values():
        v.pop('_seen', None)
    return outputs


def poll_display_and_peripherals() -> dict:
    """Read display state, GPU PCIe/HDCP/firmware, Bluetooth, and Wi-Fi status."""
    data: dict = {}

    # 1. Displays from kscreen-doctor -j (or xrandr fallback)
    kscreen = _get_kscreen_displays()
    use_kscreen = bool(kscreen)
    if not kscreen:
        kscreen = _get_xrandr_displays()
    data['kscreen_available'] = use_kscreen

    # 2. Poll all connectors under /sys/class/drm/<card>-*
    card = find_amdgpu_card()
    card_prefix = f'{card.name}-' if card else 'card0-'
    connectors: dict[str, dict] = {}
    for c in sorted(Path('/sys/class/drm').glob(f'{card_prefix}*')):
        short_name = c.name[len(card_prefix):]
        status = read_text(c / 'status', 'unknown')
        dpms = read_text(c / 'dpms', '—')
        sys_enabled = read_text(c / 'enabled', '—')
        kd = kscreen.get(short_name)
        if kd:
            connectors[c.name] = {
                **kd,
                'drm_name': c.name,
                'short_name': short_name,
                'status': status,
                'dpms': dpms,
                'sys_enabled': sys_enabled,
            }
        else:
            raw_modes = read_text(c / 'modes', '') or ''
            seen_m = []
            for m in raw_modes.splitlines():
                if m and m not in seen_m:
                    seen_m.append(m)
            connectors[c.name] = {
                'name': short_name,
                'drm_name': c.name,
                'short_name': short_name,
                'status': status,
                'dpms': dpms,
                'sys_enabled': sys_enabled,
                'connected': (status == 'connected'),
                'enabled': (sys_enabled == 'enabled'),
                'current': '—',
                'current_mode_id': '',
                'modes': [{'id': m, 'label': m, 'active': False, 'preferred': False} for m in seen_m[:8]],
                'scale': 1.0,
                'brightness': 1.0,
                'vrr': '—',
                'hdr': '—',
            }
    data['connectors'] = connectors

    # 3. GPU / PCIe / HDCP / Firmware versions
    if card:
        dev = card / 'device'
        data['pcie_speed'] = read_text(dev / 'current_link_speed', '—')
        data['pcie_width'] = read_text(dev / 'current_link_width', '—')
        data['hdcp_supported'] = (dev / 'hdcp_srm').exists()
        data['vbios_version'] = read_text(dev / 'vbios_version', '—')
        data['reset_method'] = read_text(dev / 'reset_method', '—')
        fw_versions = {}
        fw_dir = dev / 'fw_version'
        if fw_dir.is_dir():
            for f in sorted(fw_dir.iterdir()):
                val = read_text(f)
                if val:
                    fw_versions[f.name] = val
        data['fw_versions'] = fw_versions
    else:
        data['pcie_speed'] = '—'
        data['pcie_width'] = '—'
        data['hdcp_supported'] = False
        data['vbios_version'] = '—'
        data['reset_method'] = '—'
        data['fw_versions'] = {}

    # 4. Bluetooth directly from sysfs + rfkill state (no subprocess call)
    bt_devs = []
    for bt in sorted(Path('/sys/class/bluetooth').glob('hci*')):
        addr = read_text(bt / 'address', '—')
        soft = hard = None
        for rf in bt.glob('rfkill*'):
            soft = read_int(rf / 'soft')
            hard = read_int(rf / 'hard')
            break
        bt_devs.append({
            'name': bt.name,
            'address': addr,
            'soft_blocked': (soft == 1) if soft is not None else None,
            'hard_blocked': (hard == 1) if hard is not None else None,
        })
    data['bluetooth'] = bt_devs

    # 5. Wi-Fi from sysfs
    wifi_devs = []
    for net in sorted(Path('/sys/class/net').iterdir()):
        if (net / 'wireless').is_dir():
            wifi_devs.append({
                'iface': net.name,
                'state': read_text(net / 'operstate', '—'),
                'carrier': read_int(net / 'carrier'),
            })
    data['wifi'] = wifi_devs

    return data


def poll_m2_environment() -> dict:
    """Check block devices (lsblk), M.2 mount point, /mnt/ps5_image_src, and EFI files."""
    devices = []
    nvme_is_root = False
    nvme_part_exists = Path('/dev/nvme0n1p1').exists()
    nvme_disk_exists = Path('/dev/nvme0n1').exists()

    try:
        r = subprocess.run(
            ['lsblk', '-J', '-o', 'NAME,SIZE,LABEL,FSTYPE,MOUNTPOINT,TYPE'],
            capture_output=True, text=True, timeout=4
        )
        if r.returncode == 0:
            tree = json.loads(r.stdout)

            def walk(node, indent=''):
                nonlocal nvme_is_root
                name = node.get('name', '')
                mp = node.get('mountpoint') or ''
                if name.startswith('nvme0n1') and mp == '/':
                    nvme_is_root = True
                devices.append({
                    'name': indent + name,
                    'raw_name': name,
                    'size': node.get('size') or '—',
                    'label': node.get('label') or '—',
                    'fstype': node.get('fstype') or '—',
                    'mountpoint': mp or '—',
                    'type': node.get('type') or '—',
                })
                for ch in node.get('children', []):
                    walk(ch, indent + '  └─ ')

            for dev in tree.get('blockdevices', []):
                walk(dev)
    except Exception:
        pass

    # Check image mount state (--mount)
    img_mount_dir = '/mnt/ps5_image_src'
    img_mount_src = ''
    try:
        r = subprocess.run(
            ['findmnt', '-n', '-o', 'SOURCE', img_mount_dir],
            capture_output=True, text=True, timeout=3
        )
        if r.returncode == 0:
            img_mount_src = r.stdout.strip()
    except Exception:
        pass

    efi_files = {
        'bzImage': Path('/boot/efi/bzImage').is_file(),
        'initrd.img': Path('/boot/efi/initrd.img').is_file(),
        'cmdline.txt': Path('/boot/efi/cmdline.txt').is_file(),
        'vram.txt': Path('/boot/efi/vram.txt').is_file(),
    }

    return {
        'devices': devices,
        'nvme_disk_exists': nvme_disk_exists,
        'nvme_part_exists': nvme_part_exists,
        'nvme_is_root': nvme_is_root,
        'img_mounted': bool(img_mount_src),
        'img_mount_dir': img_mount_dir,
        'img_mount_src': img_mount_src,
        'efi_files': efi_files,
        'efi_cmdline': read_text('/boot/efi/cmdline.txt', ''),
        'efi_vram': read_text('/boot/efi/vram.txt', ''),
    }
