#include "mainwindow.h"
#include "workers.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenu>
#include <QStatusBar>

namespace ps5 {

QIcon loadAppIcon() {
    QString p = resolveAsset("ps5tools.svg");
    if (!p.isEmpty()) {
        QIcon ico(p);
        if (!ico.isNull()) return ico;
    }
    return QIcon::fromTheme("preferences-system");
}

PS5ToolsWindow::PS5ToolsWindow(const KDETheme* initialTheme, QWidget* parent)
    : QMainWindow(parent),
      m_theme(initialTheme ? *initialTheme : KDETheme()),
      m_ps5Info(detectPS5Info()),
      m_settings(ORG_NAME, SETTINGS_NAME) {
    setMinimumSize(780, 580);
    resize(880, 670);
    setWindowIcon(loadAppIcon());

    m_overlay = new HybridOverlayManager(m_theme, this);
    connect(m_overlay, &HybridOverlayManager::visibilityChanged, this, &PS5ToolsWindow::onOverlayVisibilityChanged);

    m_tabs = new QTabWidget();
    setCentralWidget(m_tabs);

    auto runCb = [this](const QString& a, const QStringList& args, const QString& lbl, std::function<void()> done) {
        runAction(a, args, lbl, std::move(done));
    };

    bool minimizeDefault = m_settings.value("ui/minimize_to_tray", false).toBool();

    m_tabControl = new ControlTab(m_theme, runCb);
    m_tabMonitor = new MonitorTab(
        m_theme, m_ps5Info,
        [this](bool c) { toggleOverlay(c); },
        [this]() { m_overlay->openSettings(); },
        [this]() { detachWithOverlay(); },
        m_overlay->isDaemonMode(),
        [this](const QString& t) { log(t); });
    m_tabDisplay = new DisplayTab(m_theme, [this]() { pollDisplay(); }, [this](const QString& t) { log(t); });
    m_tabM2 = new M2Tab(m_theme, runCb, [this]() { pollM2(); }, [this](const QString& t) { log(t); });
    m_tabServices = new ServicesTab(
        m_theme, m_ps5Info, runCb,
        [this](SimpleCallback done) { installIntegration(std::move(done)); },
        [this]() { refreshStatuses(); });
    m_tabSettings = new SettingsTab(m_theme, minimizeDefault);
    connect(m_tabSettings, &SettingsTab::appearanceChanged, this, &PS5ToolsWindow::onAppearanceChanged);
    connect(m_tabSettings, &SettingsTab::minimizeToTrayToggled, this, &PS5ToolsWindow::onMinimizeSettingChanged);
    m_tabLog = new LogTab();

    m_tabs->addTab(m_tabControl, "");
    m_tabs->addTab(m_tabMonitor, "");
    m_tabs->addTab(m_tabDisplay, "");
    m_tabs->addTab(m_tabM2, "");
    m_tabs->addTab(m_tabServices, "");
    m_tabs->addTab(m_tabSettings, "");
    m_tabs->addTab(m_tabLog, "");
    connect(m_tabs, &QTabWidget::currentChanged, this, &PS5ToolsWindow::onTabChanged);

    m_btnOverlay = new QPushButton();
    m_btnOverlay->setCheckable(true);
    m_btnOverlay->setFixedHeight(22);
    connect(m_btnOverlay, &QPushButton::clicked, this, &PS5ToolsWindow::toggleOverlay);
    statusBar()->addPermanentWidget(m_btnOverlay);

    initTray(minimizeDefault);
    retranslateUi();

    m_sensorTimer = new QTimer(this);
    m_sensorTimer->setInterval(2000);
    connect(m_sensorTimer, &QTimer::timeout, this, &PS5ToolsWindow::onSensorTick);
    m_sensorTimer->start();

    m_displayTimer = new QTimer(this);
    m_displayTimer->setInterval(5000);
    connect(m_displayTimer, &QTimer::timeout, this, &PS5ToolsWindow::onDisplayTick);
    m_displayTimer->start();

    refreshStatuses();
    pollSensors();
    pollDisplay();
    pollM2();
    m_tabServices->refreshJournal();
    onOverlayVisibilityChanged(m_overlay->isVisible());
}

void PS5ToolsWindow::onAppearanceChanged(const QString& langMode, const QString& styleMode, const QString& schemeMode) {
    setActiveLanguage(langMode, true);
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()))
        applyAppAppearance(*app, m_theme, styleMode, schemeMode, true);
    m_ps5Info = detectPS5Info();
    m_tabMonitor->updateSystemInfo(m_ps5Info);
    m_tabServices->updateSystemInfo(m_ps5Info);
    m_overlay->refreshAppearance();
    retranslateUi();
    refreshTheme();
}

void PS5ToolsWindow::onMinimizeSettingChanged(bool checked) {
    m_settings.setValue("ui/minimize_to_tray", checked);
    if (m_actTrayMinimize) {
        m_actTrayMinimize->blockSignals(true);
        m_actTrayMinimize->setChecked(checked);
        m_actTrayMinimize->blockSignals(false);
    }
}

void PS5ToolsWindow::onTrayMinimizeToggled(bool checked) {
    m_settings.setValue("ui/minimize_to_tray", checked);
    m_tabSettings->setMinimizeToTray(checked);
}

void PS5ToolsWindow::openAppSettings() {
    showNormal();
    activateWindow();
    raise();
    m_tabs->setCurrentWidget(m_tabSettings);
}

void PS5ToolsWindow::retranslateUi() {
    setWindowTitle(QString("%1 — %2").arg(APP_NAME, m_ps5Info.model));

    m_tabs->setTabText(0, tx("🌀  Охлаждение / Boost", "🌀  Cooling / Boost"));
    m_tabs->setTabText(1, tx("🌡️  Мониторинг", "🌡️  Monitoring"));
    m_tabs->setTabText(2, tx("🖥️  Дисплей / Перифер.", "🖥️  Display / Devices"));
    m_tabs->setTabText(3, "💾  M.2 SSD");
    m_tabs->setTabText(4, tx("⚙️  Службы и Ядро", "⚙️  Services & Kernel"));
    m_tabs->setTabText(5, tx("🎨  Настройки", "🎨  Settings"));
    m_tabs->setTabText(6, tx("📋  Лог", "📋  Log"));

    m_btnOverlay->setText(tx("📊  Оверлей", "📊  Overlay"));
    m_btnOverlay->setToolTip(tx("Показать/скрыть полупрозрачный оверлей мониторинга поверх всех окон",
                                "Show/hide translucent monitoring overlay above all windows"));
    updateStatusBar();

    if (m_tray) {
        m_actTrayShow->setText(tx("Показать / скрыть окно", "Show / Hide Window"));
        m_actTrayOverlay->setText(tx("Оверлей мониторинга", "Monitoring Overlay"));
        m_actTraySettings->setText(tx("Настройки темы и языка…", "Theme & Language Settings…"));
        m_trayFanMenu->setTitle(tx("🌀 Вентилятор", "🌀 Fan"));
        m_trayBoostMenu->setTitle("⚡ Boost");
        m_actTrayMinimize->setText(tx("Сворачивать в трей при закрытии", "Minimize to tray on close"));
        m_actTrayQuit->setText(tx("Выход", "Quit"));
        if (m_actTrayQuitAll) m_actTrayQuitAll->setText(tx("Выход (вместе с оверлеем)", "Quit (including overlay)"));
    }

    m_tabControl->retranslateUi();
    m_tabMonitor->retranslateUi();
    m_tabDisplay->retranslateUi();
    m_tabM2->retranslateUi();
    m_tabServices->retranslateUi();
    m_tabSettings->retranslateUi();
    m_tabLog->retranslateUi();
}

void PS5ToolsWindow::refreshTheme() {
    m_tabControl->refreshTheme();
    m_tabMonitor->refreshTheme();
    m_tabDisplay->refreshTheme();
    m_tabM2->refreshTheme();
    m_tabServices->refreshTheme();
    m_tabSettings->refreshTheme();
}

void PS5ToolsWindow::updateStatusBar() {
    if (m_busy) {
        statusBar()->showMessage(tx("⏳  Выполнение команды…", "⏳  Running command…"));
    } else {
        statusBar()->showMessage(tx("Готов  •  Ядро ", "Ready  •  Kernel ") + m_ps5Info.kernel);
    }
}

void PS5ToolsWindow::initTray(bool minimizeDefault) {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(loadAppIcon(), this);
    m_tray->setToolTip(APP_NAME);

    auto* menu = new QMenu(this);
    m_actTrayShow = menu->addAction("");
    connect(m_actTrayShow, &QAction::triggered, this, &PS5ToolsWindow::toggleWindow);

    m_actTrayOverlay = menu->addAction("");
    m_actTrayOverlay->setCheckable(true);
    connect(m_actTrayOverlay, &QAction::triggered, this, &PS5ToolsWindow::toggleOverlay);

    m_actTraySettings = menu->addAction("");
    connect(m_actTraySettings, &QAction::triggered, this, &PS5ToolsWindow::openAppSettings);

    menu->addSeparator();
    m_trayFanMenu = menu->addMenu("🌀");
    connect(m_trayFanMenu->addAction("Fan ON"), &QAction::triggered, this, [this]() { runAction("fan", {"on"}, "ps5_control --fan on", nullptr); });
    connect(m_trayFanMenu->addAction("Fan OFF"), &QAction::triggered, this, [this]() { runAction("fan", {"off"}, "ps5_control --fan off", nullptr); });
    connect(m_trayFanMenu->addAction("Fan MAX"), &QAction::triggered, this, [this]() { runAction("fan", {"max"}, "ps5_control --fan max", nullptr); });

    m_trayBoostMenu = menu->addMenu("⚡ Boost");
    connect(m_trayBoostMenu->addAction("Boost ON"), &QAction::triggered, this, [this]() { runAction("boost", {"on"}, "ps5_control --boost on", nullptr); });
    connect(m_trayBoostMenu->addAction("Boost OFF"), &QAction::triggered, this, [this]() { runAction("boost", {"off"}, "ps5_control --boost off", nullptr); });

    menu->addSeparator();
    m_actTrayMinimize = menu->addAction("");
    m_actTrayMinimize->setCheckable(true);
    m_actTrayMinimize->setChecked(minimizeDefault);
    connect(m_actTrayMinimize, &QAction::toggled, this, &PS5ToolsWindow::onTrayMinimizeToggled);

    m_actTrayQuit = menu->addAction("");
    connect(m_actTrayQuit, &QAction::triggered, this, [this]() { quitApp(true); });
    if (m_overlay->isDaemonMode()) {
        m_actTrayQuitAll = menu->addAction("");
        connect(m_actTrayQuitAll, &QAction::triggered, this, [this]() { quitApp(false); });
    }
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) toggleWindow();
    });
    m_tray->show();
}

void PS5ToolsWindow::toggleWindow() {
    if (isVisible() && !isMinimized()) hide();
    else { showNormal(); activateWindow(); raise(); }
}

void PS5ToolsWindow::log(const QString& text) {
    m_tabLog->append(text);
}

void PS5ToolsWindow::setBusy(bool busy) {
    m_busy = busy;
    for (auto* w : m_tabControl->actionWidgets()) w->setEnabled(!busy);
    for (auto* w : m_tabM2->actionWidgets()) w->setEnabled(!busy);
    for (auto* w : m_tabServices->actionWidgets()) w->setEnabled(!busy);
    updateStatusBar();
    if (!busy) m_tabM2->reapplyRootGuard();
}

void PS5ToolsWindow::runAction(const QString& action, const QStringList& args, const QString& label, std::function<void()> onDone) {
    QStringList cmd = buildPrivilegedCmd(action, args);
    if (cmd.isEmpty()) {
        log("✗ " + label + ": " + tx("не удалось сформировать команду", "failed to build command"));
        return;
    }
    log("▶  " + label);
    setBusy(true);
    auto* r = new CommandRunner(cmd, true, 120000, this);
    connect(r, &CommandRunner::commandFinished, this, [this, label, onDone](const QString& out, const QString& err, int rc) {
        onCmdDone(out, err, rc, label, onDone);
    });
    connect(r, &QThread::finished, this, [this, r]() { m_runners.removeAll(r); r->deleteLater(); });
    m_runners.append(r);
    r->start();
}

void PS5ToolsWindow::installIntegration(SimpleCallback onDone) {
    QStringList cmd = buildInstallIntegrationCmd();
    if (cmd.isEmpty()) {
        log(tx("✗ Не найдены ps5tools-root-helper / org.ps5linuxtools.policy",
               "✗ ps5tools-root-helper / org.ps5linuxtools.policy not found"));
        return;
    }
    log(tx("▶  Установка системной интеграции (helper + polkit + sbin)",
           "▶  Installing system integration (helper + polkit + sbin)"));
    setBusy(true);
    auto* r = new CommandRunner(cmd, true, 120000, this);
    const QString lbl = tx("Системная интеграция", "System integration");
    connect(r, &CommandRunner::commandFinished, this, [this, lbl, onDone](const QString& out, const QString& err, int rc) {
        onCmdDone(out, err, rc, lbl, onDone);
    });
    connect(r, &QThread::finished, this, [this, r]() { m_runners.removeAll(r); r->deleteLater(); });
    m_runners.append(r);
    r->start();
}

void PS5ToolsWindow::onCmdDone(const QString& out, const QString& err, int rc, const QString& label, std::function<void()> onDone) {
    if (!out.isEmpty()) log(out);
    if (!err.isEmpty()) log("[stderr] " + err);
    log(rc == 0
        ? tx("✓  %1 — успешно (код 0)", "✓  %1 — succeeded (exit 0)").arg(label)
        : tx("✗  %1 — ошибка (код %2)", "✗  %1 — failed (exit %2)").arg(label).arg(rc));
    setBusy(false);
    refreshStatuses();
    if (onDone) onDone();
}

void PS5ToolsWindow::refreshStatuses() {
    auto* c = new StatusChecker(this);
    connect(c, &StatusChecker::statusesReady, this, &PS5ToolsWindow::onStatusesReady);
    connect(c, &QThread::finished, this, [this, c]() { m_runners.removeAll(c); c->deleteLater(); });
    m_runners.append(c);
    c->start();
}

void PS5ToolsWindow::onStatusesReady(const ServiceStatusMap& map) {
    m_tabControl->updateServiceStatus(map);
    m_tabServices->updateStatuses(map);
}

void PS5ToolsWindow::onTabChanged(int idx) {
    QWidget* w = m_tabs->widget(idx);
    if (w == m_tabDisplay) pollDisplay();
    else if (w == m_tabM2) pollM2();
    else if (w == m_tabServices) refreshStatuses();
}

void PS5ToolsWindow::onSensorTick() {
    QWidget* cur = m_tabs->currentWidget();
    bool needWidgetOverlay = (!m_overlay->isDaemonMode()) && m_overlay->isVisible();
    if (needWidgetOverlay || (isVisible() && (cur == m_tabMonitor || cur == m_tabControl)))
        pollSensors();
}

void PS5ToolsWindow::pollSensors() {
    auto* p = new SensorPoller(this);
    connect(p, &SensorPoller::sensorsReady, this, &PS5ToolsWindow::onSensorsReady);
    connect(p, &QThread::finished, this, [this, p]() { m_runners.removeAll(p); p->deleteLater(); });
    m_runners.append(p);
    p->start();
}

void PS5ToolsWindow::onSensorsReady(const SensorSnapshot& data) {
    m_tabMonitor->updateSensors(data);
    m_tabControl->updateLiveSensors(data);
    if (m_overlay->isVisible()) m_overlay->updateSensors(data);
}

void PS5ToolsWindow::onDisplayTick() {
    if (isVisible() && m_tabs->currentWidget() == m_tabDisplay) pollDisplay();
}

void PS5ToolsWindow::pollDisplay() {
    auto* p = new DisplayPoller(this);
    connect(p, &DisplayPoller::displayReady, m_tabDisplay, &DisplayTab::updateDisplayData);
    connect(p, &QThread::finished, this, [this, p]() { m_runners.removeAll(p); p->deleteLater(); });
    m_runners.append(p);
    p->start();
}

void PS5ToolsWindow::pollM2() {
    auto* p = new M2Poller(this);
    connect(p, &M2Poller::m2Ready, m_tabM2, &M2Tab::updateM2Data);
    connect(p, &QThread::finished, this, [this, p]() { m_runners.removeAll(p); p->deleteLater(); });
    m_runners.append(p);
    p->start();
}

void PS5ToolsWindow::toggleOverlay(bool checked) {
    if (checked) {
        m_overlay->showOverlay();
        pollSensors();
    } else {
        m_overlay->hide();
    }
}

void PS5ToolsWindow::onOverlayVisibilityChanged(bool visible) {
    m_btnOverlay->setChecked(visible);
    m_tabMonitor->setOverlayChecked(visible);
    if (m_actTrayOverlay) m_actTrayOverlay->setChecked(visible);
}

void PS5ToolsWindow::detachWithOverlay() {
    m_overlay->showOverlay();
    quitApp(true);
}

void PS5ToolsWindow::quitApp(bool keepDaemon) {
    m_forceQuit = true;
    m_keepDaemonOnQuit = keepDaemon;
    close();
    QApplication::quit();
}

void PS5ToolsWindow::closeEvent(QCloseEvent* ev) {
    if (!m_forceQuit && m_actTrayMinimize && m_actTrayMinimize->isChecked() && m_tray && m_tray->isVisible()) {
        hide();
        ev->ignore();
        return;
    }

    m_sensorTimer->stop();
    m_displayTimer->stop();
    m_overlay->shutdown(m_keepDaemonOnQuit);
    if (m_tray) m_tray->hide();

    m_tabServices->waitPollers();
    for (QThread* t : m_runners) {
        if (t && t->isRunning()) t->wait(1500);
    }
    QMainWindow::closeEvent(ev);
}

} // namespace ps5
