#include "overlay.h"

#include <LayerShellQt/Window>

#include <QAction>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMargins>
#include <QMenu>
#include <QMouseEvent>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <signal.h>
#include <unistd.h>

namespace ps5 {

struct MetricDef {
    const char* id;
    const char* title;
    bool defaultVisible;
};

static constexpr MetricDef kMetrics[] = {
    {"soc",      "MainSoC:",  true},
    {"local1",   "Local1:",   false},
    {"local2",   "Local2:",   false},
    {"local3",   "Local3:",   false},
    {"gpu_t",    "GPU edge:", true},
    {"cpu_t",    "CPU Tctl:", true},
    {"cpu_mhz",  "CPU freq:", true},
    {"nvme",     "NVMe:",     true},
    {"gpu_busy", "GPU load:", true},
    {"gpu_mhz",  "GPU freq:", true},
    {"vram",     "VRAM:",     true},
    {"fan",      "Fan PWM:",  true},
    {"gpu_pm",   "GPU PM:",   false},
};

OverlaySettingsDialog::OverlaySettingsDialog(const QMap<QString, bool>& visibleKeys, int opacityPct, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tx("Настройка оверлея", "Overlay Settings"));
    setModal(true);
    setMinimumWidth(290);

    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto* opRow = new QHBoxLayout();
    opRow->addWidget(new QLabel(tx("Непрозрачность:", "Opacity:")));
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(40, 100);
    m_slider->setValue(opacityPct);
    m_lblOp = new QLabel(QString::number(opacityPct) + "%");
    m_lblOp->setFixedWidth(36);
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_lblOp->setText(QString::number(v) + "%");
    });
    opRow->addWidget(m_slider, 1);
    opRow->addWidget(m_lblOp);
    layout->addLayout(opRow);

    layout->addWidget(new QLabel(tx("Отображаемые датчики:", "Visible sensors:")));
    auto* grid = new QGridLayout();
    int idx = 0;
    for (const auto& m : kMetrics) {
        QString key = QString::fromLatin1(m.id);
        QString lbl = QString::fromUtf8(m.title);
        if (lbl.endsWith(':')) lbl.chop(1);
        auto* cb = new QCheckBox(lbl);
        cb->setChecked(visibleKeys.value(key, m.defaultVisible));
        m_checks[key] = cb;
        grid->addWidget(cb, idx / 2, idx % 2);
        ++idx;
    }
    layout->addLayout(grid);

    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(btns);
}

QMap<QString, bool> OverlaySettingsDialog::visibleKeys() const {
    QMap<QString, bool> res;
    for (auto it = m_checks.cbegin(); it != m_checks.cend(); ++it)
        res[it.key()] = it.value()->isChecked();
    return res;
}

int OverlaySettingsDialog::opacityPct() const {
    return m_slider->value();
}

MonitorOverlay::MonitorOverlay(const KDETheme& theme, QWidget* settingsParent)
    : QWidget(nullptr, Qt::FramelessWindowHint),
      m_theme(theme),
      m_settingsParent(settingsParent),
      m_settings(ORG_NAME, SETTINGS_NAME) {
    const bool isWayland = QGuiApplication::platformName().startsWith("wayland");
    if (!isWayland)
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setObjectName("overlay_root");
    setWindowTitle("PS5MonitorOverlay");
    setMinimumWidth(205);
    setMaximumWidth(260);

    winId();
    if (isWayland && windowHandle()) {
        m_layerWin = LayerShellQt::Window::get(windowHandle());
        if (m_layerWin) {
            m_layerWin->setLayer(LayerShellQt::Window::LayerOverlay);
            m_layerWin->setAnchors(LayerShellQt::Window::Anchors(
                LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft));
            m_layerWin->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            m_layerWin->setExclusiveZone(-1);
            m_layerWin->setActivateOnShow(false);
            m_layerWin->setWantsToBeOnActiveScreen(true);
            m_layerWin->setScope("ps5tools-overlay");
        } else {
            setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
        }
    }

    m_opacityPct = m_settings.value("overlay/opacity", 85).toInt();
    for (const auto& m : kMetrics) {
        QString key = QString::fromLatin1(m.id);
        m_visibleMetrics[key] = m_settings.value("overlay/metric_" + key, m.defaultVisible).toBool();
    }

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_card = new QWidget(this);
    m_card->setObjectName("overlay_bg");
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(10, 8, 10, 8);
    cardLayout->setSpacing(3);
    rootLayout->addWidget(m_card);

    auto* hdr = new QHBoxLayout();
    hdr->setSpacing(4);
    m_titleLbl = new QLabel("🎮  PS5 Monitor");
    hdr->addWidget(m_titleLbl);
    hdr->addStretch();

    m_btnCfg = new QPushButton("⚙");
    m_btnCfg->setObjectName("ov_btn");
    m_btnCfg->setFixedSize(18, 18);
    connect(m_btnCfg, &QPushButton::clicked, this, &MonitorOverlay::openSettings);
    hdr->addWidget(m_btnCfg);

    m_btnClose = new QPushButton("✕");
    m_btnClose->setObjectName("ov_close");
    m_btnClose->setFixedSize(18, 18);
    connect(m_btnClose, &QPushButton::clicked, this, &QWidget::hide);
    hdr->addWidget(m_btnClose);
    cardLayout->addLayout(hdr);

    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setObjectName("ov_sep");
    cardLayout->addWidget(sep);

    auto* grid = new QGridLayout();
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(2);
    grid->setColumnMinimumWidth(0, 82);

    int rowIdx = 0;
    for (const auto& m : kMetrics) {
        QString key = QString::fromLatin1(m.id);
        auto* kLbl = new QLabel(QString::fromUtf8(m.title));
        auto* vLbl = new QLabel("—");
        vLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(kLbl, rowIdx, 0);
        grid->addWidget(vLbl, rowIdx, 1);
        m_rows[key] = {kLbl, vLbl};
        ++rowIdx;
    }
    cardLayout->addLayout(grid);

    m_hintLbl = new QLabel();
    m_hintLbl->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(m_hintLbl);

    retranslateUi();
    applyStyles();
    applyVisibility();
    restorePosition();
}

void MonitorOverlay::retranslateUi() {
    m_btnCfg->setToolTip(tx("Настроить метрики и прозрачность оверлея", "Configure overlay metrics and opacity"));
    m_btnClose->setToolTip(tx("Скрыть оверлей", "Hide overlay"));
    m_hintLbl->setText(tx("⠿ Потяните за карточку (ПКМ — настройки)", "⠿ Drag card (right-click — settings)"));
}

void MonitorOverlay::refreshAppearance() {
    retranslateUi();
    applyStyles();
    if (m_hasLastData) updateSensors(m_lastData, true);
}

void MonitorOverlay::applyStyles() {
    QString accent = m_theme.overlayAccent();
    QColor cAcc(accent);
    int alpha = qBound(90, qRound(m_opacityPct * 2.55), 255);
    setStyleSheet(QString(
        "QWidget#overlay_root { background: transparent; }"
        "QWidget#overlay_bg { background-color: rgba(16, 26, 22, %1); "
        "border: 1px solid rgba(%2, %3, %4, 120); border-radius: 8px; }"
        "QFrame#ov_sep { background: rgba(%2, %3, %4, 70); min-height: 1px; max-height: 1px; border: none; }"
        "QPushButton#ov_btn, QPushButton#ov_close { background: transparent; color: rgba(220,230,225,175); border: none; font-size: 11px; padding: 0px; }"
        "QPushButton#ov_btn:hover { color: %5; }"
        "QPushButton#ov_close:hover { color: %6; }"
    ).arg(alpha).arg(cAcc.red()).arg(cAcc.green()).arg(cAcc.blue()).arg(accent, m_theme.negative()));

    m_titleLbl->setStyleSheet(QString("color: %1; font-weight: bold; font-size: 11px;").arg(accent));
    m_hintLbl->setStyleSheet("color: rgba(180, 200, 190, 120); font-size: 8px;");
    for (const auto& r : m_rows) {
        r.key->setStyleSheet("color: rgba(205, 222, 215, 200); font-size: 10px;");
        r.val->setStyleSheet("color: #efeee1; font-size: 10px; font-family: monospace;");
    }
}

void MonitorOverlay::clampPosition(const QSize& sz) {
    QScreen* scr = QGuiApplication::primaryScreen();
    if (!scr) return;
    QRect geo = scr->availableGeometry();
    int maxX = qMax(0, geo.width() - sz.width());
    int maxY = qMax(0, geo.height() - sz.height());
    m_posX = qBound(0, m_posX, maxX);
    m_posY = qBound(0, m_posY, maxY);
}

void MonitorOverlay::syncGeometry() {
    adjustSize();
    QSize sz = sizeHint().expandedTo(QSize(minimumWidth(), 80));
    resize(sz);
    clampPosition(sz);
    if (m_layerWin) {
        m_layerWin->setMargins(QMargins(m_posX, m_posY, 0, 0));
        m_layerWin->setDesiredSize(sz);
    } else {
        move(m_posX, m_posY);
    }
}

void MonitorOverlay::applyVisibility() {
    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it) {
        bool vis = m_visibleMetrics.value(it.key(), true);
        it.value().key->setVisible(vis);
        it.value().val->setVisible(vis);
    }
    syncGeometry();
}

void MonitorOverlay::restorePosition() {
    QScreen* scr = QGuiApplication::primaryScreen();
    int defX = 20, defY = 40;
    if (scr) {
        QRect geo = scr->availableGeometry();
        defX = qMax(0, geo.width() - width() - 20);
    }
    m_posX = m_settings.value("overlay/x", defX).toInt();
    m_posY = m_settings.value("overlay/y", defY).toInt();
    syncGeometry();
}

void MonitorOverlay::showOverlay() {
    if (m_hasLastData) updateSensors(m_lastData, true);
    syncGeometry();
    show();
    if (!m_layerWin && !QGuiApplication::platformName().startsWith("offscreen"))
        raise();
}

void MonitorOverlay::openSettings() {
    QWidget* p = (m_settingsParent && m_settingsParent->isVisible()) ? m_settingsParent : nullptr;
    OverlaySettingsDialog dlg(m_visibleMetrics, m_opacityPct, p);
    if (dlg.exec() == QDialog::Accepted) {
        m_visibleMetrics = dlg.visibleKeys();
        m_opacityPct = dlg.opacityPct();
        m_settings.setValue("overlay/opacity", m_opacityPct);
        for (auto it = m_visibleMetrics.cbegin(); it != m_visibleMetrics.cend(); ++it)
            m_settings.setValue("overlay/metric_" + it.key(), it.value());
        applyStyles();
        applyVisibility();
    }
}

void MonitorOverlay::contextMenuEvent(QContextMenuEvent* ev) {
    QMenu menu(this);
    QAction* actCfg = menu.addAction(tx("Настроить метрики и прозрачность…", "Configure metrics & opacity…"));
    connect(actCfg, &QAction::triggered, this, &MonitorOverlay::openSettings);
    menu.addSeparator();
    QAction* actHide = menu.addAction(tx("Скрыть оверлей", "Hide overlay"));
    connect(actHide, &QAction::triggered, this, &QWidget::hide);
    menu.exec(ev->globalPos());
}

void MonitorOverlay::showEvent(QShowEvent* ev) {
    QWidget::showEvent(ev);
    emit visibilityChanged(true);
}

void MonitorOverlay::hideEvent(QHideEvent* ev) {
    QWidget::hideEvent(ev);
    emit visibilityChanged(false);
}

void MonitorOverlay::mousePressEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton) {
        m_dragging = true;
        if (m_layerWin)
            m_dragOffset = ev->position().toPoint();
        else
            m_dragOffset = ev->globalPosition().toPoint() - frameGeometry().topLeft();
        ev->accept();
    }
}

void MonitorOverlay::mouseMoveEvent(QMouseEvent* ev) {
    if (m_dragging && (ev->buttons() & Qt::LeftButton)) {
        if (m_layerWin) {
            QPoint delta = ev->position().toPoint() - m_dragOffset;
            m_posX += delta.x();
            m_posY += delta.y();
            clampPosition(size());
            m_layerWin->setMargins(QMargins(m_posX, m_posY, 0, 0));
        } else {
            QPoint pt = ev->globalPosition().toPoint() - m_dragOffset;
            m_posX = pt.x();
            m_posY = pt.y();
            move(m_posX, m_posY);
        }
        ev->accept();
    }
}

void MonitorOverlay::mouseReleaseEvent(QMouseEvent* ev) {
    if (ev->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        m_settings.setValue("overlay/x", m_posX);
        m_settings.setValue("overlay/y", m_posY);
    }
}

void MonitorOverlay::setVal(const QString& key, const QString& text, const QString& color) {
    if (m_rows.contains(key)) {
        QLabel* v = m_rows[key].val;
        v->setText(text);
        v->setStyleSheet(QString("color: %1; font-size: 10px; font-family: monospace;").arg(color));
    }
}

QString MonitorOverlay::valTempColor(double val, double warn, double hot) const {
    if (val >= hot) return m_theme.negative();
    if (val >= warn) return m_theme.neutral();
    return "#efeee1";
}

void MonitorOverlay::updateSensors(const SensorSnapshot& data, bool force) {
    m_lastData = data;
    m_hasLastData = true;
    if (!force && !isVisible()) return;

    const QString naStr = tx("н/д", "N/A");
    const QString mhzUnit = tx("МГц", "MHz");
    const QString mbUnit = tx("МБ", "MB");

    auto updT = [&](const QString& key, const TempEntry& t, double warn, double hot) {
        if (t.valid) setVal(key, QString::number(t.tempC, 'f', 1) + " °C", valTempColor(t.tempC, warn, hot));
        else setVal(key, naStr);
    };

    updT("soc", data.fanTemps[0], 75, 90);
    updT("local1", data.fanTemps[1], 70, 85);
    updT("local2", data.fanTemps[2], 50, 65);
    updT("local3", data.fanTemps[3], 70, 85);

    if (data.hasGpuTemp) setVal("gpu_t", QString::number(data.gpuTemp, 'f', 1) + " °C", valTempColor(data.gpuTemp, 75, 90));
    else setVal("gpu_t", naStr);

    if (data.hasCpuTemp) setVal("cpu_t", QString::number(data.cpuTemp, 'f', 1) + " °C", valTempColor(data.cpuTemp, 75, 90));
    else setVal("cpu_t", naStr);

    updT("nvme", data.nvmeTemps[0], 60, 70);

    setVal("cpu_mhz", data.hasCpuFreq ? QString("%1 %2").arg(data.cpuFreqMhz).arg(mhzUnit) : "—");

    if (data.hasGpuBusy) {
        QString c = data.gpuBusy > 95 ? m_theme.negative() : data.gpuBusy > 70 ? m_theme.neutral() : "#efeee1";
        setVal("gpu_busy", QString::number(data.gpuBusy) + "%", c);
    } else {
        setVal("gpu_busy", naStr);
    }

    setVal("gpu_mhz", data.hasGpuFreq ? QString("%1 %2").arg(data.gpuFreqMhz).arg(mhzUnit) : "—");

    if (data.hasVram) {
        double ratio = double(data.vramUsedMb) / data.vramTotalMb;
        QString c = ratio > 0.9 ? m_theme.negative() : ratio > 0.75 ? m_theme.neutral() : "#efeee1";
        setVal("vram", QString("%1/%2 %3").arg(data.vramUsedMb).arg(data.vramTotalMb).arg(mbUnit), c);
    } else {
        setVal("vram", "—");
    }

    setVal("fan", data.hasFanPwm ? QString::number(data.fanPwmPct) + "%" : "—");

    QString pmC = (data.gpuPm == "active") ? m_theme.positive() : m_theme.neutral();
    setVal("gpu_pm", data.gpuPm, pmC);
}

// ═══════════════════════════════════════════════════════════════════════════
// HybridOverlayManager
// ═══════════════════════════════════════════════════════════════════════════

HybridOverlayManager::HybridOverlayManager(const KDETheme& theme, QWidget* settingsParent)
    : QObject(settingsParent),
      m_theme(theme),
      m_settingsParent(settingsParent),
      m_confPath(QDir::homePath() + "/.config/ps5linuxtools/overlay.conf") {
    QString bin = resolveTool("ps5overlayd");
    if (!bin.isEmpty() && QFileInfo::exists(bin) && QFileInfo(bin).isExecutable())
        m_daemonBin = bin;

    m_useDaemon = !m_daemonBin.isEmpty() && QGuiApplication::platformName().startsWith("wayland");
    if (!m_useDaemon) {
        m_widgetOverlay = new MonitorOverlay(theme, settingsParent);
        connect(m_widgetOverlay, &MonitorOverlay::visibilityChanged, this, &HybridOverlayManager::emitVisibility);
    } else {
        if (!QFileInfo::exists(m_confPath)) {
            QMap<QString, bool> vis;
            for (const auto& m : kMetrics) vis[QString::fromLatin1(m.id)] = m.defaultVisible;
            writeConf(vis, 85, m_theme.overlayAccent());
        }
        m_watchTimer = new QTimer(this);
        m_watchTimer->setInterval(1500);
        connect(m_watchTimer, &QTimer::timeout, this, &HybridOverlayManager::checkDaemonState);
        m_watchTimer->start();
        m_lastVisible = isVisible();
    }
}

QString HybridOverlayManager::pidFilePath() {
    QByteArray xrd = qgetenv("XDG_RUNTIME_DIR");
    if (!xrd.isEmpty())
        return QString::fromLocal8Bit(xrd) + "/ps5overlayd.pid";
    return QString("/tmp/ps5overlayd-%1.pid").arg(getuid());
}

int HybridOverlayManager::daemonPid() const {
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    bool ok = false;
    int pid = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
    if (!ok || pid <= 0) return 0;
    if (kill(static_cast<pid_t>(pid), 0) != 0)
        return 0;
    return pid;
}

bool HybridOverlayManager::isVisible() const {
    if (m_useDaemon) return daemonPid() > 0;
    return m_widgetOverlay && m_widgetOverlay->isVisible();
}

void HybridOverlayManager::emitVisibility(bool vis) {
    if (vis != m_lastVisible) {
        m_lastVisible = vis;
        emit visibilityChanged(vis);
    }
}

void HybridOverlayManager::checkDaemonState() {
    emitVisibility(daemonPid() > 0);
}

void HybridOverlayManager::readConf(QMap<QString, bool>& vis, int& opacity, QMap<QString, QString>& extra) const {
    for (const auto& m : kMetrics)
        vis[QString::fromLatin1(m.id)] = m.defaultVisible;
    opacity = 85;
    extra.clear();

    QFile f(m_confPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString text = QString::fromUtf8(f.readAll());
    for (const QString& line : text.split('\n')) {
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        QString k = line.left(eq).trimmed();
        QString v = line.mid(eq + 1).trimmed();
        if (k == "opacity") {
            bool ok = false;
            int op = v.toInt(&ok);
            if (ok) opacity = qBound(40, op, 100);
        } else if (k.startsWith("metric_")) {
            QString mkey = k.mid(7);
            if (vis.contains(mkey)) vis[mkey] = (v != "0");
        } else {
            extra[k] = v;
        }
    }
}

void HybridOverlayManager::writeConf(const QMap<QString, bool>& vis, int opacity, const QString& accent) const {
    QMap<QString, bool> dummyVis;
    int dummyOp = 85;
    QMap<QString, QString> extra;
    readConf(dummyVis, dummyOp, extra);

    if (!accent.isEmpty()) extra["accent"] = accent;
    else if (!extra.contains("accent")) extra["accent"] = m_theme.overlayAccent();

    QColor win = QGuiApplication::palette().color(QPalette::Window);
    QString bgHex = (win.lightness() < 128) ? win.darker(145).name() : QString("#161d24");

    QDir().mkpath(QFileInfo(m_confPath).absolutePath());
    QFile f(m_confPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QStringList lines;
    if (extra.contains("x")) lines << ("x=" + extra["x"]);
    if (extra.contains("y")) lines << ("y=" + extra["y"]);
    lines << ("opacity=" + QString::number(opacity));
    lines << (QString("lang=") + (isRu() ? "ru" : "en"));
    lines << ("accent=" + extra["accent"]);
    lines << ("bg=" + bgHex);
    lines << ("fg=#efeee1");
    for (const auto& m : kMetrics) {
        QString k = QString::fromLatin1(m.id);
        lines << QString("metric_%1=%2").arg(k).arg(vis.value(k, m.defaultVisible) ? 1 : 0);
    }
    f.write((lines.join('\n') + '\n').toUtf8());
}

void HybridOverlayManager::refreshAppearance() {
    if (m_useDaemon) {
        QMap<QString, bool> vis;
        int op = 85;
        QMap<QString, QString> extra;
        readConf(vis, op, extra);
        writeConf(vis, op, m_theme.overlayAccent());
        int pid = daemonPid();
        if (pid > 0) kill(static_cast<pid_t>(pid), SIGUSR1);
    } else if (m_widgetOverlay) {
        m_widgetOverlay->refreshAppearance();
    }
}

void HybridOverlayManager::showOverlay() {
    if (m_useDaemon) {
        if (daemonPid() <= 0 && !m_daemonBin.isEmpty()) {
            QMap<QString, bool> vis;
            int op = 85;
            QMap<QString, QString> extra;
            readConf(vis, op, extra);
            writeConf(vis, op, m_theme.overlayAccent());
            QProcess::startDetached(m_daemonBin, {});
        }
        QTimer::singleShot(150, this, &HybridOverlayManager::checkDaemonState);
    } else if (m_widgetOverlay) {
        m_widgetOverlay->showOverlay();
    }
}

void HybridOverlayManager::hide() {
    if (m_useDaemon) {
        int pid = daemonPid();
        if (pid > 0) kill(static_cast<pid_t>(pid), SIGTERM);
        QTimer::singleShot(150, this, &HybridOverlayManager::checkDaemonState);
    } else if (m_widgetOverlay) {
        m_widgetOverlay->hide();
    }
}

void HybridOverlayManager::openSettings() {
    if (!m_useDaemon && m_widgetOverlay) {
        m_widgetOverlay->openSettings();
        return;
    }
    QMap<QString, bool> vis;
    int op = 85;
    QMap<QString, QString> extra;
    readConf(vis, op, extra);
    QWidget* p = (m_settingsParent && m_settingsParent->isVisible()) ? m_settingsParent : nullptr;
    OverlaySettingsDialog dlg(vis, op, p);
    if (dlg.exec() == QDialog::Accepted) {
        writeConf(dlg.visibleKeys(), dlg.opacityPct(), m_theme.overlayAccent());
        int pid = daemonPid();
        if (pid > 0) kill(static_cast<pid_t>(pid), SIGUSR1);
    }
}

void HybridOverlayManager::updateSensors(const SensorSnapshot& data) {
    if (m_widgetOverlay) m_widgetOverlay->updateSensors(data);
}

void HybridOverlayManager::shutdown(bool keepDaemon) {
    if (m_useDaemon) {
        if (m_watchTimer) m_watchTimer->stop();
        if (!keepDaemon) hide();
    } else if (m_widgetOverlay) {
        m_widgetOverlay->close();
    }
}

} // namespace ps5
