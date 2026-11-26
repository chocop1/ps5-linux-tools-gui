#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QThread>

#include "sysinfo.h"

namespace ps5 {

struct ServiceStatusInfo {
    int active = -1; // 1 = active, 0 = inactive, -1 = unknown
    QString activeState = "unknown";
    QString subState;
    bool enabled = false;
    QString unitFileState = "not-installed";
};

using ServiceStatusMap = QMap<QString, ServiceStatusInfo>;

QStringList buildPrivilegedCmd(const QString& action, const QStringList& args = {});
QStringList buildInstallIntegrationCmd();

class CommandRunner : public QThread {
    Q_OBJECT
public:
    explicit CommandRunner(QStringList cmd, bool needsRoot = true, int timeoutMs = 120000, QObject* parent = nullptr);

signals:
    void commandFinished(const QString& out, const QString& err, int exitCode);

protected:
    void run() override;

private:
    QStringList m_cmd;
    bool m_needsRoot;
    int m_timeoutMs;
    bool m_ru = false;
};

class StatusChecker : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void statusesReady(const ps5::ServiceStatusMap& map);
protected:
    void run() override;
};

class SensorPoller : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void sensorsReady(const ps5::SensorSnapshot& data);
protected:
    void run() override;
};

class DisplayPoller : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void displayReady(const ps5::DisplayAndPeripheralsInfo& data);
protected:
    void run() override;
};

class M2Poller : public QThread {
    Q_OBJECT
public:
    using QThread::QThread;
signals:
    void m2Ready(const ps5::M2EnvironmentInfo& data);
protected:
    void run() override;
};

class JournalPoller : public QThread {
    Q_OBJECT
public:
    JournalPoller(QString mode, QString unit, QString filterRegex, int lines = 180, QObject* parent = nullptr);
signals:
    void journalReady(const QString& title, const QString& text);
protected:
    void run() override;
private:
    QString m_mode;
    QString m_unit;
    QString m_filter;
    int m_lines;
    bool m_ru = false;
};

void registerMetaTypes();

} // namespace ps5

Q_DECLARE_METATYPE(ps5::ServiceStatusMap)
Q_DECLARE_METATYPE(ps5::SensorSnapshot)
Q_DECLARE_METATYPE(ps5::DisplayAndPeripheralsInfo)
Q_DECLARE_METATYPE(ps5::M2EnvironmentInfo)
