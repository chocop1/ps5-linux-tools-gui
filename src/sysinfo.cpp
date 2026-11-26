#include "sysinfo.h"
#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>
#include <algorithm>
#include <cmath>

namespace ps5 {

static QString readFileText(const QString& path, const QString& defVal = QString()) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return defVal;
    return QString::fromUtf8(f.readAll()).trimmed();
}

static bool readFileLL(const QString& path, qint64& out) {
    QString t = readFileText(path);
    if (t.isEmpty()) return false;
    bool ok = false;
    qint64 v = t.toLongLong(&ok);
    if (ok) out = v;
    return ok;
}

static QString findHwmon(const QString& driverName) {
    QDir root("/sys/class/hwmon");
    for (const QString& entry : root.entryList({"hwmon*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString full = root.filePath(entry);
        if (readFileText(full + "/name") == driverName)
            return full;
    }
    return QString();
}

static QString findAmdgpuCard() {
    QDir root("/sys/class/drm");
    static const QRegularExpression cardRe("^card\\d+$");
    QString firstCard;
    for (const QString& entry : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!cardRe.match(entry).hasMatch()) continue;
        if (firstCard.isEmpty()) firstCard = root.filePath(entry);
        QString dev = root.filePath(entry) + "/device";
        if (readFileText(dev + "/vendor") == "0x1002" ||
            readFileText(dev + "/uevent").contains("DRIVER=amdgpu")) {
            return root.filePath(entry);
        }
    }
    return firstCard;
}

static int readCpuFreqMhz() {
    qint64 khz = 0;
    if (readFileLL("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", khz) && khz > 0)
        return qRound(khz / 1000.0);

    QString raw = readFileText("/proc/cpuinfo");
    if (raw.isEmpty()) return 0;
    double sum = 0.0;
    int cnt = 0;
    for (const QString& line : raw.split('\n')) {
        if (line.startsWith("cpu MHz")) {
            int idx = line.indexOf(':');
            if (idx > 0) {
                bool ok = false;
                double f = line.mid(idx + 1).trimmed().toDouble(&ok);
                if (ok) { sum += f; cnt++; }
            }
        }
    }
    return cnt > 0 ? qRound(sum / cnt) : 0;
}

PS5SystemInfo detectPS5Info() {
    PS5SystemInfo info;
    info.kernel = QSysInfo::kernelVersion();
    info.sessionType = QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE"));
    if (info.sessionType.isEmpty()) info.sessionType = "unknown";

    QString card = findAmdgpuCard();
    if (!card.isEmpty()) {
        QString dev = card + "/device";
        QString v = readFileText(dev + "/vendor").toLower();
        QString d = readFileText(dev + "/device").toLower();
        if (!v.isEmpty() && !d.isEmpty()) info.pciId = v + ":" + d;
        info.vbios = readFileText(dev + "/vbios_version", "—");
        qint64 vramBytes = 0;
        if (readFileLL(dev + "/mem_info_vram_total", vramBytes) && vramBytes > 0)
            info.vramTotalMb = qRound(vramBytes / (1024.0 * 1024.0));
    }

    if (info.pciId == "0x1002:0x13fc") info.model = "PlayStation 5 (Oberon)";
    else if (info.pciId == "0x1002:0x13da") info.model = "PlayStation 5 Pro (Viola)";
    else info.model = QString("PS5 APU (%1)").arg(info.pciId.isEmpty() ? tx("неизвестно", "unknown") : info.pciId);
    return info;
}

SensorSnapshot pollSensors() {
    SensorSnapshot s;
    QString hFan = findHwmon("ps5_fan");
    if (!hFan.isEmpty()) {
        for (int i = 0; i < 4; ++i) {
            qint64 raw = 0;
            QString lbl = readFileText(QString("%1/temp%2_label").arg(hFan).arg(i + 1), QString("Temp%1").arg(i + 1));
            bool fault = readFileText(QString("%1/temp%2_fault").arg(hFan).arg(i + 1), "0") != "0";
            s.fanTemps[i].label = lbl;
            s.fanTemps[i].fault = fault;
            if (readFileLL(QString("%1/temp%2_input").arg(hFan).arg(i + 1), raw)) {
                s.fanTemps[i].valid = true;
                s.fanTemps[i].tempC = raw / 1000.0;
            }
        }
        qint64 pwm = 0, en = 0;
        if (readFileLL(hFan + "/pwm1", pwm)) {
            s.hasFanPwm = true;
            s.fanPwm = static_cast<int>(pwm);
            s.fanPwmPct = qRound(pwm / 255.0 * 100.0);
        }
        if (readFileLL(hFan + "/pwm1_enable", en))
            s.fanPwmEnable = static_cast<int>(en);
    }

    QString hGpu = findHwmon("amdgpu");
    if (!hGpu.isEmpty()) {
        qint64 raw = 0, freq = 0;
        if (readFileLL(hGpu + "/temp1_input", raw)) {
            s.hasGpuTemp = true;
            s.gpuTemp = raw / 1000.0;
        }
        if (readFileLL(hGpu + "/freq1_input", freq)) {
            s.hasGpuFreq = true;
            s.gpuFreqMhz = static_cast<int>(freq / 1000000LL);
        }
    }

    QString card = findAmdgpuCard();
    if (!card.isEmpty()) {
        QString dev = card + "/device";
        qint64 busy = 0;
        if (readFileLL(dev + "/gpu_busy_percent", busy)) {
            s.hasGpuBusy = true;
            s.gpuBusy = static_cast<int>(busy);
        }
        s.gpuPm = readFileText(dev + "/power/runtime_status", "unknown");
        s.gpuPowerState = readFileText(dev + "/power_state", "—");

        qint64 vu = 0, vt = 0, gu = 0, gt = 0;
        if (readFileLL(dev + "/mem_info_vram_used", vu) && readFileLL(dev + "/mem_info_vram_total", vt) && vt > 0) {
            s.hasVram = true;
            s.vramUsedMb = qRound(vu / (1024.0 * 1024.0));
            s.vramTotalMb = qRound(vt / (1024.0 * 1024.0));
        }
        if (readFileLL(dev + "/mem_info_gtt_used", gu) && readFileLL(dev + "/mem_info_gtt_total", gt) && gt > 0) {
            s.hasGtt = true;
            s.gttUsedMb = qRound(gu / (1024.0 * 1024.0));
            s.gttTotalMb = qRound(gt / (1024.0 * 1024.0));
        }
    }

    QString hNvme = findHwmon("nvme");
    if (!hNvme.isEmpty()) {
        for (int i = 0; i < 2; ++i) {
            qint64 raw = 0;
            s.nvmeTemps[i].label = readFileText(QString("%1/temp%2_label").arg(hNvme).arg(i + 1), QString("NVMe %1").arg(i + 1));
            if (readFileLL(QString("%1/temp%2_input").arg(hNvme).arg(i + 1), raw)) {
                s.nvmeTemps[i].valid = true;
                s.nvmeTemps[i].tempC = raw / 1000.0;
            }
        }
    }

    QString hCpu = findHwmon("k10temp");
    if (!hCpu.isEmpty()) {
        qint64 raw = 0;
        if (readFileLL(hCpu + "/temp1_input", raw)) {
            s.hasCpuTemp = true;
            s.cpuTemp = raw / 1000.0;
        }
    }

    int cmhz = readCpuFreqMhz();
    if (cmhz > 0) {
        s.hasCpuFreq = true;
        s.cpuFreqMhz = cmhz;
    }

    return s;
}

FanCurveInfo readFanCurve() {
    FanCurveInfo fc;
    QString hFan = findHwmon("ps5_fan");
    if (hFan.isEmpty()) return fc;
    fc.hwmonPath = hFan;
    qint64 tmp = 0;
    if (readFileLL(hFan + "/pwm1_enable", tmp)) fc.pwmEnable = static_cast<int>(tmp);
    if (readFileLL(hFan + "/pwm1", tmp)) fc.pwmNow = static_cast<int>(tmp);
    if (readFileLL(hFan + "/pwm1_auto_point1_temp_hyst", tmp)) fc.hystC = qRound(tmp / 1000.0);

    for (int i = 1; i <= 8; ++i) {
        qint64 tMc = 0, pVal = 0;
        if (readFileLL(QString("%1/pwm1_auto_point%2_temp").arg(hFan).arg(i), tMc) &&
            readFileLL(QString("%1/pwm1_auto_point%2_pwm").arg(hFan).arg(i), pVal)) {
            fc.points.append({qRound(tMc / 1000.0), static_cast<int>(pVal)});
        }
    }
    fc.available = (fc.points.size() == 8);
    return fc;
}

static QMap<QString, QPair<QString, QString>> parseKscreenExtras() {
    QMap<QString, QPair<QString, QString>> res;
    QProcess proc;
    proc.start("kscreen-doctor", {"-o"});
    if (!proc.waitForFinished(4000) || proc.exitCode() != 0)
        return res;
    static const QRegularExpression ansiRe("\\x1b\\[[0-9;]*[A-Za-z]");
    QString cur;
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    for (const QString& raw : out.split('\n')) {
        QString line = QString(raw).remove(ansiRe).trimmed();
        if (line.startsWith("Output:")) {
            const QStringList p = line.split(' ', Qt::SkipEmptyParts);
            if (p.size() >= 3) {
                cur = p[2];
                res[cur] = {"—", "—"};
            }
        } else if (!cur.isEmpty() && line.startsWith("Vrr:")) {
            res[cur].first = line.mid(4).trimmed();
        } else if (!cur.isEmpty() && line.startsWith("HDR:")) {
            res[cur].second = line.mid(4).trimmed();
        }
    }
    return res;
}

static QMap<QString, DisplayConnectorInfo> getKscreenDisplays() {
    QMap<QString, DisplayConnectorInfo> map;
    QProcess proc;
    proc.start("kscreen-doctor", {"-j"});
    if (!proc.waitForFinished(5000) || proc.exitCode() != 0)
        return map;

    QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isObject()) return map;

    auto extras = parseKscreenExtras();
    const QJsonArray outputs = doc.object().value("outputs").toArray();
    for (const QJsonValue& val : outputs) {
        QJsonObject obj = val.toObject();
        QString name = obj.value("name").toString();
        if (name.isEmpty()) continue;

        DisplayConnectorInfo info;
        info.shortName = name;
        info.connected = obj.value("connected").toBool(false);
        info.enabled = obj.value("enabled").toBool(false);
        info.currentModeId = obj.value("currentModeId").toString();
        info.scale = obj.value("scale").toDouble(1.0);
        info.brightness = obj.value("brightness").toDouble(1.0);

        QSet<QString> prefIds;
        for (const QJsonValue& pv : obj.value("preferredModes").toArray())
            prefIds.insert(pv.toString());

        struct Key { int w, h, hz; bool operator<(const Key& o) const {
            if (w != o.w) return w < o.w;
            if (h != o.h) return h < o.h;
            return hz < o.hz;
        }};
        QMap<Key, DisplayModeEntry> dedup;

        for (const QJsonValue& mv : obj.value("modes").toArray()) {
            QJsonObject m = mv.toObject();
            QString mid = m.value("id").toString();
            QJsonObject sz = m.value("size").toObject();
            int w = sz.value("width").toInt();
            int h = sz.value("height").toInt();
            int hz = qRound(m.value("refreshRate").toDouble());
            if (w <= 0 || h <= 0 || hz <= 0) continue;

            DisplayModeEntry e;
            e.id = mid;
            e.width = w;
            e.height = h;
            e.hz = hz;
            e.label = QString("%1x%2@%3").arg(w).arg(h).arg(hz);
            e.active = (mid == info.currentModeId);
            e.preferred = prefIds.contains(mid);

            Key k{w, h, hz};
            if (!dedup.contains(k) || e.active || (e.preferred && !dedup[k].active))
                dedup[k] = e;
        }

        info.modes = dedup.values().toVector();
        std::sort(info.modes.begin(), info.modes.end(), [](const DisplayModeEntry& a, const DisplayModeEntry& b) {
            qint64 pa = qint64(a.width) * a.height;
            qint64 pb = qint64(b.width) * b.height;
            if (pa != pb) return pa > pb;
            return a.hz > b.hz;
        });

        for (const auto& m : info.modes) {
            if (m.active) { info.current = m.label; break; }
        }

        auto ext = extras.value(name, {"—", "—"});
        if (obj.contains("vrrPolicy")) {
            int vp = obj.value("vrrPolicy").toInt();
            info.vrr = (vp == 0 ? "never" : vp == 1 ? "always" : vp == 2 ? "automatic" : QString::number(vp));
        } else {
            info.vrr = ext.first;
        }
        if (obj.contains("hdr") && obj.value("hdr").isBool())
            info.hdr = obj.value("hdr").toBool() ? "enabled" : "disabled";
        else
            info.hdr = ext.second;

        map[name] = info;
    }
    return map;
}

DisplayAndPeripheralsInfo pollDisplayAndPeripherals() {
    DisplayAndPeripheralsInfo data;
    auto kscreen = getKscreenDisplays();
    data.kscreenAvailable = !kscreen.isEmpty();

    QString card = findAmdgpuCard();
    QString cardName = card.isEmpty() ? "card0" : QFileInfo(card).fileName();
    QString prefix = cardName + "-";

    QDir drm("/sys/class/drm");
    for (const QString& entry : drm.entryList({prefix + "*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString cPath = drm.filePath(entry);
        QString shortName = entry.mid(prefix.size());
        QString status = readFileText(cPath + "/status", "unknown");
        QString dpms = readFileText(cPath + "/dpms", "—");
        QString sysEnabled = readFileText(cPath + "/enabled", "—");

        DisplayConnectorInfo ci;
        if (kscreen.contains(shortName)) {
            ci = kscreen[shortName];
        } else {
            ci.shortName = shortName;
            ci.connected = (status == "connected");
            ci.enabled = (sysEnabled == "enabled");
            const QStringList rawModes = readFileText(cPath + "/modes").split('\n', Qt::SkipEmptyParts);
            QSet<QString> seen;
            for (const QString& m : rawModes) {
                if (!seen.contains(m)) {
                    seen.insert(m);
                    DisplayModeEntry e;
                    e.id = m; e.label = m;
                    ci.modes.append(e);
                }
            }
        }
        ci.drmName = entry;
        ci.status = status;
        ci.dpms = dpms;
        data.connectors.append(ci);
    }

    if (!card.isEmpty()) {
        QString dev = card + "/device";
        data.pcieSpeed = readFileText(dev + "/current_link_speed", "—");
        data.pcieWidth = readFileText(dev + "/current_link_width", "—");
        data.hdcpSupported = QFileInfo::exists(dev + "/hdcp_srm");
        data.vbiosVersion = readFileText(dev + "/vbios_version", "—");
        data.resetMethod = readFileText(dev + "/reset_method", "—");

        QDir fwDir(dev + "/fw_version");
        if (fwDir.exists()) {
            for (const QString& f : fwDir.entryList(QDir::Files, QDir::Name)) {
                QString v = readFileText(fwDir.filePath(f));
                if (!v.isEmpty()) data.fwVersions[f] = v;
            }
        }
    }

    QDir btDir("/sys/class/bluetooth");
    for (const QString& entry : btDir.entryList({"hci*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString p = btDir.filePath(entry);
        BluetoothAdapterInfo bi;
        bi.name = entry;
        bi.address = readFileText(p + "/address", "—");
        for (const QString& rf : QDir(p).entryList({"rfkill*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            qint64 s = 0, h = 0;
            if (readFileLL(p + "/" + rf + "/soft", s)) bi.softBlocked = static_cast<int>(s);
            if (readFileLL(p + "/" + rf + "/hard", h)) bi.hardBlocked = static_cast<int>(h);
            break;
        }
        data.bluetooth.append(bi);
    }

    QDir netDir("/sys/class/net");
    for (const QString& entry : netDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QString p = netDir.filePath(entry);
        if (QFileInfo::exists(p + "/wireless")) {
            WifiAdapterInfo wi;
            wi.iface = entry;
            wi.state = readFileText(p + "/operstate", "—");
            data.wifi.append(wi);
        }
    }

    return data;
}

static void walkLsblk(const QJsonObject& node, const QString& indent, M2EnvironmentInfo& info) {
    QString name = node.value("name").toString();
    QString mp = node.value("mountpoint").toString();
    if (name.startsWith("nvme0n1") && mp == "/")
        info.nvmeIsRoot = true;

    BlockDeviceRow row;
    row.name = indent + name;
    row.size = node.value("size").toString("—");
    row.label = node.value("label").toString();
    if (row.label.isEmpty()) row.label = "—";
    row.fstype = node.value("fstype").toString();
    if (row.fstype.isEmpty()) row.fstype = "—";
    row.mountpoint = mp.isEmpty() ? "—" : mp;
    info.devices.append(row);

    for (const QJsonValue& ch : node.value("children").toArray())
        walkLsblk(ch.toObject(), indent + "  └─ ", info);
}

M2EnvironmentInfo pollM2Environment() {
    M2EnvironmentInfo info;
    info.nvmeDiskExists = QFileInfo::exists("/dev/nvme0n1");
    info.nvmePartExists = QFileInfo::exists("/dev/nvme0n1p1");

    QProcess p;
    p.start("lsblk", {"-J", "-o", "NAME,SIZE,LABEL,FSTYPE,MOUNTPOINT,TYPE"});
    if (p.waitForFinished(4000) && p.exitCode() == 0) {
        QJsonDocument doc = QJsonDocument::fromJson(p.readAllStandardOutput());
        for (const QJsonValue& dv : doc.object().value("blockdevices").toArray())
            walkLsblk(dv.toObject(), "", info);
    }

    QProcess fm;
    fm.start("findmnt", {"-n", "-o", "SOURCE", info.imgMountDir});
    if (fm.waitForFinished(3000) && fm.exitCode() == 0) {
        info.imgMountSrc = QString::fromUtf8(fm.readAllStandardOutput()).trimmed();
        info.imgMounted = !info.imgMountSrc.isEmpty();
    }

    info.efiFiles["bzImage"] = QFileInfo::exists("/boot/efi/bzImage");
    info.efiFiles["initrd.img"] = QFileInfo::exists("/boot/efi/initrd.img");
    info.efiFiles["cmdline.txt"] = QFileInfo::exists("/boot/efi/cmdline.txt");
    info.efiFiles["vram.txt"] = QFileInfo::exists("/boot/efi/vram.txt");
    info.efiVram = readFileText("/boot/efi/vram.txt");
    return info;
}

} // namespace ps5
