#include "workers.h"
#include "config.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QProcess>
#include <QStandardPaths>
#include <unistd.h>

namespace ps5 {

void registerMetaTypes() {
    qRegisterMetaType<ps5::ServiceStatusMap>("ps5::ServiceStatusMap");
    qRegisterMetaType<ps5::SensorSnapshot>("ps5::SensorSnapshot");
    qRegisterMetaType<ps5::DisplayAndPeripheralsInfo>("ps5::DisplayAndPeripheralsInfo");
    qRegisterMetaType<ps5::M2EnvironmentInfo>("ps5::M2EnvironmentInfo");
}

QStringList buildPrivilegedCmd(const QString& action, const QStringList& args) {
    bool helperOk = isPolkitHelperInstalled();
    if (helperOk && action.startsWith("m2-") && action != "m2-unmount") {
        static const QMap<QString, QString> needed = {
            {"m2-init", "/usr/local/sbin/m2_init"},
            {"m2-mount", "/usr/local/sbin/m2_install.sh"},
            {"m2-install", "/usr/local/sbin/m2_install.sh"},
            {"m2-exec", "/usr/local/sbin/m2_exec.sh"},
        };
        QString req = needed.value(action);
        if (!req.isEmpty() && !QFileInfo::exists(req)) helperOk = false;
    }
    if (helperOk && action == "warm-reboot" && !QFileInfo::exists("/usr/local/sbin/ps5-linux-warm-reboot.sh"))
        helperOk = false;

    if (helperOk)
        return QStringList{HELPER_INSTALL_PATH, action} + args;

    if (action == "fan") return {resolveTool("ps5_control"), "--fan", args.value(0)};
    if (action == "boost") return {resolveTool("ps5_control"), "--boost", args.value(0)};

    if (action == "systemctl") {
        QString op = args.value(0), unit = args.value(1);
        if (op == "enable-now") return {"systemctl", "enable", "--now", unit};
        if (op == "disable-now") return {"systemctl", "disable", "--now", unit};
        return {"systemctl", op, unit};
    }

    if (action == "fan-pwm" || action == "fan-auto" || action == "fan-curve" ||
        action == "fan-curve-reset" || action == "fan-curve-persist" || action == "m2-unmount") {
        QString helperLocal = resolveTool("ps5tools-root-helper");
        if (QFileInfo::exists(helperLocal)) {
            QString py = QStandardPaths::findExecutable("python3");
            if (py.isEmpty()) py = "/usr/bin/python3";
            return QStringList{py, helperLocal, action} + args;
        }
    }

    if (action == "m2-init") return {resolveTool("m2_init")};
    if (action == "m2-mount") return {resolveTool("m2_install.sh"), "--mount", args.value(0)};
    if (action == "m2-install") return {resolveTool("m2_install.sh"), "--install", args.value(0)};
    if (action == "m2-exec") return {resolveTool("m2_exec.sh")};
    if (action == "warm-reboot") return {resolveTool("ps5-linux-warm-reboot.sh")};

    return {};
}

QStringList buildInstallIntegrationCmd() {
    QString helperSrc = resolveTool("ps5tools-root-helper");
    QString policySrc = resolveAsset("org.ps5linuxtools.policy");
    if (!QFileInfo::exists(helperSrc) || policySrc.isEmpty())
        return {};

    QFile hf(helperSrc), pf(policySrc);
    if (!hf.open(QIODevice::ReadOnly) || !pf.open(QIODevice::ReadOnly))
        return {};

    QByteArray helperB64 = hf.readAll().toBase64();
    QByteArray policyB64 = pf.readAll().toBase64();

    QJsonObject sbinMap;
    for (const char* t : {"ps5_control", "m2_init", "m2_install.sh", "m2_exec.sh", "ps5-linux-warm-reboot.sh"}) {
        QString p = resolveTool(t);
        if (QFileInfo::exists(p) && !p.startsWith("/usr/local/sbin/"))
            sbinMap[t] = p;
    }
    QByteArray mapJson = QJsonDocument(sbinMap).toJson(QJsonDocument::Compact);

    QString script = QString(
        "import os, shutil, base64, json, pathlib\n"
        "os.makedirs('/usr/local/libexec', exist_ok=True)\n"
        "os.makedirs('/usr/share/polkit-1/actions', exist_ok=True)\n"
        "os.makedirs('/usr/local/sbin', exist_ok=True)\n"
        "pathlib.Path('%1').write_bytes(base64.b64decode('%2'))\n"
        "os.chmod('%1', 0o755)\n"
        "pathlib.Path('%3').write_bytes(base64.b64decode('%4'))\n"
        "os.chmod('%3', 0o644)\n"
        "for name, src in json.loads('''%5''').items():\n"
        "    dst = f'/usr/local/sbin/{name}'\n"
        "    if os.path.isfile(src):\n"
        "        shutil.copy2(src, dst)\n"
        "        os.chmod(dst, 0o755)\n"
        "print(%6)\n"
    ).arg(HELPER_INSTALL_PATH, QString::fromLatin1(helperB64),
          POLICY_INSTALL_PATH, QString::fromLatin1(policyB64),
          QString::fromUtf8(mapJson),
          isRu() ? "'Системная интеграция (helper + polkit + sbin) успешно установлена!'"
                 : "'System integration (helper + polkit + sbin) installed successfully!'");

    QString py = QStandardPaths::findExecutable("python3");
    if (py.isEmpty()) py = "/usr/bin/python3";
    return {py, "-c", script};
}

CommandRunner::CommandRunner(QStringList cmd, bool needsRoot, int timeoutMs, QObject* parent)
    : QThread(parent), m_cmd(std::move(cmd)), m_needsRoot(needsRoot), m_timeoutMs(timeoutMs), m_ru(isRu()) {}

void CommandRunner::run() {
    if (m_cmd.isEmpty()) {
        emit commandFinished("", m_ru ? "Неизвестная или недоступная команда" : "Unknown or unavailable command", -1);
        return;
    }
    QString exe = m_cmd.first();
    QStringList args = m_cmd.mid(1);
    if (m_needsRoot && geteuid() != 0) {
        args.prepend(exe);
        exe = "pkexec";
    }
    QProcess p;
    p.start(exe, args);
    if (!p.waitForStarted(5000)) {
        emit commandFinished("", (m_ru ? "Не удалось запустить: " : "Failed to start: ") + exe, -1);
        return;
    }
    if (!p.waitForFinished(m_timeoutMs)) {
        p.kill();
        emit commandFinished("", m_ru ? "Таймаут выполнения команды" : "Command execution timed out", -1);
        return;
    }
    emit commandFinished(
        QString::fromUtf8(p.readAllStandardOutput()).trimmed(),
        QString::fromUtf8(p.readAllStandardError()).trimmed(),
        p.exitCode()
    );
}

void StatusChecker::run() {
    ServiceStatusMap map;
    QStringList units = allowedUnits();
    for (const QString& u : units) map[u] = ServiceStatusInfo{};

    QProcess p;
    p.start("systemctl", QStringList{"show", "--property=Id,ActiveState,SubState,UnitFileState"} + units);
    if (p.waitForFinished(5000) && p.exitCode() == 0) {
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        QMap<QString, QString> cur;
        auto flush = [&]() {
            QString uid = cur.value("Id");
            if (map.contains(uid)) {
                ServiceStatusInfo inf;
                QString act = cur.value("ActiveState");
                QString uf = cur.value("UnitFileState");
                inf.active = act.isEmpty() ? -1 : (act == "active" ? 1 : 0);
                inf.activeState = act.isEmpty() ? "unknown" : act;
                inf.subState = cur.value("SubState");
                inf.enabled = (uf == "enabled" || uf == "static" || uf == "enabled-runtime");
                inf.unitFileState = uf.isEmpty() ? "not-installed" : uf;
                map[uid] = inf;
            }
            cur.clear();
        };
        for (const QString& line : out.split('\n')) {
            if (line.trimmed().isEmpty()) {
                flush();
            } else {
                int eq = line.indexOf('=');
                if (eq > 0) cur[line.left(eq)] = line.mid(eq + 1);
            }
        }
        flush();
    }
    emit statusesReady(map);
}

void SensorPoller::run() {
    emit sensorsReady(pollSensors());
}

void DisplayPoller::run() {
    emit displayReady(pollDisplayAndPeripherals());
}

void M2Poller::run() {
    emit m2Ready(pollM2Environment());
}

JournalPoller::JournalPoller(QString mode, QString unit, QString filterRegex, int lines, QObject* parent)
    : QThread(parent), m_mode(std::move(mode)), m_unit(std::move(unit)),
      m_filter(std::move(filterRegex)), m_lines(lines), m_ru(isRu()) {}

void JournalPoller::run() {
    QStringList args;
    QString title;
    if (m_mode == "unit" && !m_unit.isEmpty()) {
        args = {"-u", m_unit, "-n", QString::number(m_lines), "--no-pager", "-b"};
        title = QString(m_ru ? "Журнал службы %1 (текущая загрузка)" : "Service log: %1 (current boot)").arg(m_unit);
    } else if (m_mode == "kernel_err") {
        args = {"-k", "-p", "err..emerg", "-n", QString::number(m_lines), "--no-pager", "-b"};
        title = m_ru ? "Ошибки ядра (текущая загрузка)" : "Kernel errors (current boot)";
    } else {
        args = {"-k", "-n", QString::number(m_lines), "--no-pager", "-b"};
        title = m_ru ? "Журнал ядра (текущая загрузка)" : "Kernel log (current boot)";
        if (!m_filter.isEmpty()) {
            args << "-g" << m_filter;
            title += QString(m_ru ? " [фильтр: %1]" : " [filter: %1]").arg(m_filter);
        }
    }

    QProcess p;
    p.start("journalctl", args);
    if (!p.waitForFinished(6000)) {
        emit journalReady(m_ru ? "Ошибка" : "Error", m_ru ? "Таймаут journalctl" : "journalctl timeout");
        return;
    }
    QString out = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    if (out.isEmpty()) out = QString::fromUtf8(p.readAllStandardError()).trimmed();
    if (out.isEmpty()) out = m_ru ? "(Записей не найдено)" : "(No entries found)";
    emit journalReady(title, out);
}

} // namespace ps5
