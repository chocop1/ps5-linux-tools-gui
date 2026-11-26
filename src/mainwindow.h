#pragma once

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QVector>

#include "config.h"
#include "overlay.h"
#include "sysinfo.h"
#include "tabs.h"

namespace ps5 {

QIcon loadAppIcon();

class PS5ToolsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PS5ToolsWindow(const KDETheme* initialTheme = nullptr, QWidget* parent = nullptr);

public slots:
    void quitApp(bool keepDaemon = true);
    void detachWithOverlay();
    void refreshStatuses();

protected:
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void onTabChanged(int idx);
    void onSensorTick();
    void onDisplayTick();
    void pollSensors();
    void pollDisplay();
    void pollM2();
    void onSensorsReady(const ps5::SensorSnapshot& data);
    void onStatusesReady(const ps5::ServiceStatusMap& map);
    void toggleOverlay(bool checked);
    void onOverlayVisibilityChanged(bool visible);
    void toggleWindow();
    void onAppearanceChanged(const QString& langMode, const QString& styleMode, const QString& schemeMode);
    void onMinimizeSettingChanged(bool checked);
    void onTrayMinimizeToggled(bool checked);
    void openAppSettings();

private:
    void initTray(bool minimizeDefault);
    void retranslateUi();
    void refreshTheme();
    void updateStatusBar();
    void setBusy(bool busy);
    void log(const QString& text);
    void runAction(const QString& action, const QStringList& args, const QString& label, std::function<void()> onDone);
    void installIntegration(SimpleCallback onDone);
    void onCmdDone(const QString& out, const QString& err, int rc, const QString& label, std::function<void()> onDone);

    KDETheme m_theme;
    PS5SystemInfo m_ps5Info;
    QSettings m_settings;
    QVector<QThread*> m_runners;
    bool m_forceQuit = false;
    bool m_keepDaemonOnQuit = true;
    bool m_busy = false;

    HybridOverlayManager* m_overlay = nullptr;
    QTabWidget* m_tabs = nullptr;
    ControlTab* m_tabControl = nullptr;
    MonitorTab* m_tabMonitor = nullptr;
    DisplayTab* m_tabDisplay = nullptr;
    M2Tab* m_tabM2 = nullptr;
    ServicesTab* m_tabServices = nullptr;
    SettingsTab* m_tabSettings = nullptr;
    LogTab* m_tabLog = nullptr;

    QPushButton* m_btnOverlay = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayFanMenu = nullptr;
    QMenu* m_trayBoostMenu = nullptr;
    QAction* m_actTrayShow = nullptr;
    QAction* m_actTrayOverlay = nullptr;
    QAction* m_actTraySettings = nullptr;
    QAction* m_actTrayMinimize = nullptr;
    QAction* m_actTrayQuit = nullptr;
    QAction* m_actTrayQuitAll = nullptr;

    QTimer* m_sensorTimer = nullptr;
    QTimer* m_displayTimer = nullptr;
};

} // namespace ps5
