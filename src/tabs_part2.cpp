#include "tabs.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QProcess>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace ps5 {

// ═══════════════════════════════════════════════════════════════════════════
// DisplayRevertDialog & DisplayTab
// ═══════════════════════════════════════════════════════════════════════════

DisplayRevertDialog::DisplayRevertDialog(const QString& summary, QWidget* parent)
    : QMessageBox(parent), m_summary(summary) {
    setWindowTitle(tx("Подтвердите настройки дисплея", "Confirm Display Settings"));
    setIcon(QMessageBox::Question);
    setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    setDefaultButton(QMessageBox::No);
    button(QMessageBox::Yes)->setText(tx("Сохранить", "Keep Changes"));
    button(QMessageBox::No)->setText(tx("Вернуть как было", "Revert"));
    updateText();

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &DisplayRevertDialog::tick);
    m_timer->start();
}

void DisplayRevertDialog::updateText() {
    setText(tx(
        QString("Настройки применены:\n%1\n\nАвтоматический возврат к прежним параметрам через %2 с.").arg(m_summary).arg(m_seconds),
        QString("Display settings applied:\n%1\n\nAutomatically reverting to previous settings in %2 s.").arg(m_summary).arg(m_seconds)));
}

void DisplayRevertDialog::tick() {
    --m_seconds;
    if (m_seconds <= 0) {
        m_timer->stop();
        reject();
    } else {
        updateText();
    }
}

void DisplayRevertDialog::done(int result) {
    m_timer->stop();
    QMessageBox::done(result);
}

DisplayTab::DisplayTab(const KDETheme& theme, SimpleCallback refreshCb, LogCallback logCb, QWidget* parent)
    : QScrollArea(parent), m_theme(theme), m_refreshCb(std::move(refreshCb)), m_log(std::move(logCb)) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget();
    auto* outer = new QVBoxLayout(inner);
    outer->setSpacing(10);
    outer->setContentsMargins(12, 12, 12, 12);
    setWidget(inner);

    m_grpDrm = new QGroupBox();
    m_drmLayout = new QVBoxLayout(m_grpDrm);
    m_lblDrmEmpty = new QLabel();
    m_drmLayout->addWidget(m_lblDrmEmpty);
    outer->addWidget(m_grpDrm);

    m_grpCtrl = new QGroupBox();
    auto* gCtrl = new QGridLayout(m_grpCtrl);
    gCtrl->setColumnMinimumWidth(0, 120);

    m_cmbOutput = new QComboBox();
    connect(m_cmbOutput, qOverload<int>(&QComboBox::currentIndexChanged), this, &DisplayTab::onOutputSelected);
    m_cmbMode = new QComboBox();
    m_cmbScale = new QComboBox();
    m_cmbScale->addItems({"1.0", "1.25", "1.5", "1.75", "2.0"});

    auto* bRow = new QHBoxLayout();
    m_sldBright = new QSlider(Qt::Horizontal);
    m_sldBright->setRange(10, 100);
    m_sldBright->setValue(100);
    m_lblBright = new QLabel("100%");
    m_lblBright->setFixedWidth(40);
    connect(m_sldBright, &QSlider::valueChanged, this, [this](int v) { m_lblBright->setText(QString::number(v) + "%"); });
    bRow->addWidget(m_sldBright, 1);
    bRow->addWidget(m_lblBright);

    m_cmbVrr = new QComboBox();
    m_cmbHdr = new QComboBox();

    m_lblOutCap = new QLabel();
    m_lblModeCap = new QLabel();
    m_lblScaleCap = new QLabel();
    m_lblBrightCap = new QLabel();

    gCtrl->addWidget(m_lblOutCap, 0, 0);
    gCtrl->addWidget(m_cmbOutput, 0, 1);
    gCtrl->addWidget(m_lblModeCap, 1, 0);
    gCtrl->addWidget(m_cmbMode, 1, 1);
    gCtrl->addWidget(m_lblScaleCap, 2, 0);
    gCtrl->addWidget(m_cmbScale, 2, 1);
    gCtrl->addWidget(m_lblBrightCap, 3, 0);
    gCtrl->addLayout(bRow, 3, 1);
    gCtrl->addWidget(new QLabel("VRR / HDR:"), 4, 0);
    auto* vhRow = new QHBoxLayout();
    vhRow->addWidget(m_cmbVrr);
    vhRow->addWidget(m_cmbHdr);
    gCtrl->addLayout(vhRow, 4, 1);

    auto* applyRow = new QHBoxLayout();
    m_btnApplyDisp = new QPushButton();
    connect(m_btnApplyDisp, &QPushButton::clicked, this, &DisplayTab::onApplyDisplay);
    m_btnRefresh = new QPushButton();
    connect(m_btnRefresh, &QPushButton::clicked, this, [this]() { m_refreshCb(); });
    applyRow->addWidget(m_btnApplyDisp, 1);
    applyRow->addWidget(m_btnRefresh);
    gCtrl->addLayout(applyRow, 5, 0, 1, 2);
    outer->addWidget(m_grpCtrl);

    m_grpPcie = new QGroupBox();
    auto* gPcie = new QGridLayout(m_grpPcie);
    gPcie->setColumnMinimumWidth(0, 130);
    m_lblPcieCap = new QLabel();
    m_lblPcie = new QLabel("—");
    m_lblHdcp = new QLabel("—");
    m_lblVbios = new QLabel("—");
    m_lblReset = new QLabel("—");
    m_lblFw = new QLabel("—");
    m_lblFw->setWordWrap(true);
    m_lblFw->setStyleSheet("font-family: monospace; font-size: 10px;");
    gPcie->addWidget(m_lblPcieCap, 0, 0);
    gPcie->addWidget(m_lblPcie, 0, 1);
    gPcie->addWidget(new QLabel("HDCP SRM:"), 1, 0);
    gPcie->addWidget(m_lblHdcp, 1, 1);
    gPcie->addWidget(new QLabel("VBIOS:"), 2, 0);
    gPcie->addWidget(m_lblVbios, 2, 1);
    gPcie->addWidget(new QLabel("GPU Reset:"), 3, 0);
    gPcie->addWidget(m_lblReset, 3, 1);
    gPcie->addWidget(new QLabel("Firmware:"), 4, 0);
    gPcie->addWidget(m_lblFw, 4, 1);
    outer->addWidget(m_grpPcie);

    auto* grpBt = new QGroupBox("📶  Bluetooth");
    auto* btV = new QVBoxLayout(grpBt);
    m_lblBtInfo = new QLabel("…");
    m_lblBtInfo->setWordWrap(true);
    btV->addWidget(m_lblBtInfo);
    outer->addWidget(grpBt);

    auto* grpWifi = new QGroupBox("📡  Wi-Fi");
    auto* wifiV = new QVBoxLayout(grpWifi);
    m_lblWifiInfo = new QLabel("…");
    m_lblWifiInfo->setWordWrap(true);
    wifiV->addWidget(m_lblWifiInfo);
    outer->addWidget(grpWifi);
    outer->addStretch();

    retranslateUi();
}

void DisplayTab::retranslateUi() {
    m_grpDrm->setTitle(tx("🖥️  Дисплеи (kscreen-doctor + DRM)", "🖥️  Displays (kscreen-doctor + DRM)"));
    if (m_lastConnectors.isEmpty())
        m_lblDrmEmpty->setText(tx("Опрос коннекторов…", "Polling connectors…"));

    m_grpCtrl->setTitle(tx("🎛️  Настройка вывода (kscreen-doctor)", "🎛️  Output Configuration (kscreen-doctor)"));
    m_lblOutCap->setText(tx("Выход:", "Output:"));
    m_lblModeCap->setText(tx("Режим (Гц):", "Mode (Hz):"));
    m_lblScaleCap->setText(tx("Масштаб:", "Scale:"));
    m_lblBrightCap->setText(tx("Яркость:", "Brightness:"));

    const QString keepStr = tx("(не менять)", "(keep unchanged)");
    for (auto* cmb : {m_cmbVrr, m_cmbHdr}) {
        int cur = qMax(0, cmb->currentIndex());
        cmb->blockSignals(true);
        cmb->clear();
        if (cmb == m_cmbVrr) cmb->addItems({keepStr, "automatic", "always", "never"});
        else cmb->addItems({keepStr, "enable", "disable"});
        cmb->setCurrentIndex(cur < cmb->count() ? cur : 0);
        cmb->blockSignals(false);
    }

    m_btnApplyDisp->setText(tx("✓  Применить параметры дисплея (с откатом 15 с)",
                               "✓  Apply Display Settings (15s auto-revert)"));
    m_btnRefresh->setText(tx("↺  Обновить", "↺  Refresh"));

    m_grpPcie->setTitle(tx("⚡  GPU / PCIe / HDCP / Прошивки (7.3.3)", "⚡  GPU / PCIe / HDCP / Firmware (7.3.3+)"));
    m_lblPcieCap->setText(tx("PCIe линк:", "PCIe link:"));

    for (auto& w : m_connectorWidgets) {
        w.capStatus->setText(tx("Статус:", "Status:"));
        w.capCur->setText(tx("Активный режим:", "Active mode:"));
        w.capModes->setText(tx("Все режимы:", "All modes:"));
    }

    if (m_hasData) {
        updateDisplayData(m_lastData);
        onOutputSelected();
    } else {
        m_lblBtInfo->setText(tx("Опрос…", "Polling…"));
        m_lblWifiInfo->setText(tx("Опрос…", "Polling…"));
    }
}

void DisplayTab::refreshTheme() {
    if (m_hasData) updateDisplayData(m_lastData);
}

DisplayTab::ConnectorWidgets DisplayTab::ensureConnectorCard(const QString& drmName) {
    if (m_connectorWidgets.contains(drmName)) return m_connectorWidgets[drmName];
    m_lblDrmEmpty->hide();

    auto* grp = new QGroupBox(drmName);
    grp->setFlat(true);
    auto* g = new QGridLayout(grp);
    g->setColumnMinimumWidth(0, 120);

    ConnectorWidgets w;
    w.grp = grp;
    w.capStatus = new QLabel(tx("Статус:", "Status:"));
    w.capCur = new QLabel(tx("Активный режим:", "Active mode:"));
    w.capModes = new QLabel(tx("Все режимы:", "All modes:"));
    w.status = new QLabel("—");
    w.dpms = new QLabel("—");
    w.current = new QLabel("—");
    w.vh = new QLabel("—");
    w.modes = new QLabel("—");
    w.modes->setWordWrap(true);
    w.modes->setStyleSheet("font-size: 10px;");

    g->addWidget(w.capStatus, 0, 0);
    g->addWidget(w.status, 0, 1);
    g->addWidget(new QLabel("DPMS:"), 1, 0);
    g->addWidget(w.dpms, 1, 1);
    g->addWidget(w.capCur, 2, 0);
    g->addWidget(w.current, 2, 1);
    g->addWidget(new QLabel("VRR / HDR:"), 3, 0);
    g->addWidget(w.vh, 3, 1);
    g->addWidget(w.capModes, 4, 0);
    g->addWidget(w.modes, 4, 1);

    m_drmLayout->addWidget(grp);
    m_connectorWidgets[drmName] = w;
    return w;
}

void DisplayTab::updateDisplayData(const DisplayAndPeripheralsInfo& data) {
    m_lastData = data;
    m_hasData = true;
    m_lastConnectors.clear();
    if (data.connectors.isEmpty()) {
        m_lblDrmEmpty->setText(tx("Коннекторы DRM не обнаружены", "No DRM connectors detected"));
        m_lblDrmEmpty->show();
    }

    QString sig;
    QVector<DisplayConnectorInfo> connectedList;
    for (const auto& info : data.connectors) {
        m_lastConnectors[info.drmName] = info;
        auto w = ensureConnectorCard(info.drmName);

        QString stC = (info.status == "connected") ? m_theme.positive() : m_theme.muted();
        w.status->setText(info.status);
        w.status->setStyleSheet(QString("color: %1; font-weight: bold;").arg(stC));

        QString dpC = (info.dpms == "On") ? m_theme.positive() : m_theme.neutral();
        w.dpms->setText(info.dpms);
        w.dpms->setStyleSheet(QString("color: %1;").arg(dpC));

        w.current->setText(info.current);
        w.current->setStyleSheet(QString("color: %1; font-weight: bold;").arg(m_theme.accent()));

        QString vrrC = (info.vrr != "incapable" && info.vrr != "—" && info.vrr != "never") ? m_theme.positive() : m_theme.muted();
        QString hdrC = (info.hdr != "incapable" && info.hdr != "—" && info.hdr != "disabled") ? m_theme.positive() : m_theme.muted();
        w.vh->setText(QString("VRR: <span style=\"color:%1\">%2</span>  |  HDR: <span style=\"color:%3\">%4</span>")
                          .arg(vrrC, info.vrr, hdrC, info.hdr));

        QStringList mTexts;
        for (int i = 0; i < info.modes.size() && i < 14; ++i) {
            const auto& m = info.modes[i];
            mTexts << (m.active ? QString("<b>%1 ✓</b>").arg(m.label) : m.label);
        }
        w.modes->setText(mTexts.isEmpty() ? "—" : mTexts.join("   "));

        if (info.connected) {
            connectedList.append(info);
            sig += info.drmName + ":" + info.currentModeId + ":" + QString::number(info.modes.size()) + ";";
        }
    }

    m_grpCtrl->setEnabled(data.kscreenAvailable && !connectedList.isEmpty());
    if (sig != m_knownSignature) {
        m_knownSignature = sig;
        QString prevOut = m_cmbOutput->currentData().toString();
        m_cmbOutput->blockSignals(true);
        m_cmbOutput->clear();
        for (const auto& inf : connectedList)
            m_cmbOutput->addItem(QString("%1 (%2)").arg(inf.shortName, inf.current), inf.drmName);
        int idx = m_cmbOutput->findData(prevOut);
        if (idx >= 0) m_cmbOutput->setCurrentIndex(idx);
        m_cmbOutput->blockSignals(false);
        onOutputSelected();
    }

    m_lblPcie->setText(QString("%1 × %2 lanes").arg(data.pcieSpeed, data.pcieWidth));
    m_lblHdcp->setText(data.hdcpSupported ? tx("✓ поддерживается", "✓ supported") : tx("✗ не обнаружен", "✗ not detected"));
    m_lblHdcp->setStyleSheet(QString("color: %1;").arg(data.hdcpSupported ? m_theme.positive() : m_theme.muted()));
    m_lblVbios->setText(data.vbiosVersion);
    m_lblReset->setText(data.resetMethod);

    QStringList fwParts;
    for (auto it = data.fwVersions.cbegin(); it != data.fwVersions.cend(); ++it) {
        QString k = it.key();
        k.replace("_fw_version", "");
        fwParts << (k + ":" + it.value());
    }
    m_lblFw->setText(fwParts.isEmpty() ? "—" : fwParts.join("  "));

    if (!data.bluetooth.isEmpty()) {
        QStringList lines;
        for (const auto& b : data.bluetooth) {
            QString rf = b.hardBlocked == 1
                ? QString("<span style=\"color:%1\">hard-blocked</span>").arg(m_theme.negative())
                : b.softBlocked == 1
                ? QString("<span style=\"color:%1\">soft-blocked</span>").arg(m_theme.neutral())
                : QString("<span style=\"color:%1\">%2</span>").arg(m_theme.positive(), tx("активен", "active"));
            lines << QString("<b>%1</b> — %2 (%3)").arg(b.name, b.address, rf);
        }
        m_lblBtInfo->setStyleSheet("");
        m_lblBtInfo->setText(lines.join("<br>"));
    } else {
        m_lblBtInfo->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
        m_lblBtInfo->setText(tx("Bluetooth-адаптеры не обнаружены", "No Bluetooth adapters detected"));
    }

    if (!data.wifi.isEmpty()) {
        QStringList lines;
        for (const auto& w : data.wifi) {
            QString c = (w.state == "up") ? m_theme.positive() : m_theme.muted();
            lines << QString("<b>%1</b>: <span style=\"color:%2\">%3</span>").arg(w.iface, c, w.state);
        }
        m_lblWifiInfo->setStyleSheet("");
        m_lblWifiInfo->setText(lines.join("<br>"));
    } else {
        m_lblWifiInfo->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
        m_lblWifiInfo->setText(tx("Wi-Fi адаптеры не обнаружены", "No Wi-Fi adapters detected"));
    }
}

void DisplayTab::onOutputSelected() {
    QString key = m_cmbOutput->currentData().toString();
    if (!m_lastConnectors.contains(key)) {
        m_cmbMode->clear();
        return;
    }
    const auto& info = m_lastConnectors[key];
    m_cmbMode->clear();
    const QString curTag = tx(" [текущий]", " [current]");
    const QString prefTag = tx(" [рек.]", " [pref.]");
    for (const auto& m : info.modes) {
        QString suf = m.active ? curTag : (m.preferred ? prefTag : "");
        m_cmbMode->addItem(m.label + suf, m.id);
        if (m.active) m_cmbMode->setCurrentIndex(m_cmbMode->count() - 1);
    }

    QString sc = QString::number(info.scale, 'g', 2);
    if (!sc.contains('.')) sc += ".0";
    int sIdx = m_cmbScale->findText(sc);
    if (sIdx >= 0) m_cmbScale->setCurrentIndex(sIdx);

    m_sldBright->setValue(qBound(10, qRound(info.brightness * 100.0), 100));
    m_cmbVrr->setEnabled(info.vrr != "incapable" && info.vrr != "—");
    m_cmbHdr->setEnabled(info.hdr != "incapable" && info.hdr != "—");
}

void DisplayTab::onApplyDisplay() {
    QString key = m_cmbOutput->currentData().toString();
    if (!m_lastConnectors.contains(key)) return;
    const auto& info = m_lastConnectors[key];
    QString outName = info.shortName;
    QString oldModeId = info.currentModeId;
    double oldScale = info.scale;
    int oldBright = qRound(info.brightness * 100.0);

    QString newModeId = m_cmbMode->currentData().toString();
    QString newModeLbl = m_cmbMode->currentText();
    QString newScale = m_cmbScale->currentText();
    int newBright = m_sldBright->value();

    QStringList args = {
        QString("output.%1.mode.%2").arg(outName, newModeId),
        QString("output.%1.scale.%2").arg(outName, newScale),
        QString("output.%1.brightness.%2").arg(outName).arg(newBright),
    };
    if (m_cmbVrr->isEnabled() && m_cmbVrr->currentIndex() > 0)
        args << QString("output.%1.vrrpolicy.%2").arg(outName, m_cmbVrr->currentText());
    if (m_cmbHdr->isEnabled() && m_cmbHdr->currentIndex() > 0)
        args << QString("output.%1.hdr.%2").arg(outName, m_cmbHdr->currentText());

    m_log("▶ kscreen-doctor " + args.join(' '));
    QProcess p;
    p.start("kscreen-doctor", args);
    if (!p.waitForFinished(6000) || p.exitCode() != 0) {
        QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        QMessageBox::warning(this, tx("Ошибка kscreen-doctor", "kscreen-doctor Error"),
                             err.isEmpty() ? tx("Не удалось применить настройки.", "Failed to apply display settings.") : err);
        return;
    }

    DisplayRevertDialog dlg(
        tx(QString("Выход: %1\nРежим: %2\nМасштаб: %3x, Яркость: %4%"),
           QString("Output: %1\nMode: %2\nScale: %3x, Brightness: %4%"))
            .arg(outName, newModeLbl, newScale).arg(newBright),
        this);
    if (dlg.exec() != QMessageBox::Yes) {
        QStringList rev;
        if (!oldModeId.isEmpty()) rev << QString("output.%1.mode.%2").arg(outName, oldModeId);
        rev << QString("output.%1.scale.%2").arg(outName).arg(oldScale);
        rev << QString("output.%1.brightness.%2").arg(outName).arg(oldBright);
        QProcess::execute("kscreen-doctor", rev);
        m_log(tx("↺ Параметры дисплея возвращены к предыдущим", "↺ Reverted display settings to previous values"));
    } else {
        m_log(tx("✓ Новые параметры дисплея сохранены", "✓ Display settings saved"));
    }
    m_refreshCb();
}

// ═══════════════════════════════════════════════════════════════════════════
// M2Tab
// ═══════════════════════════════════════════════════════════════════════════

M2Tab::M2Tab(const KDETheme& theme, ActionCallback runAction, SimpleCallback refreshM2, LogCallback logCb, QWidget* parent)
    : QScrollArea(parent), m_theme(theme), m_runAction(std::move(runAction)),
      m_refreshM2(std::move(refreshM2)), m_log(std::move(logCb)) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget();
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(10);
    layout->setContentsMargins(12, 12, 12, 12);
    setWidget(inner);

    m_grpInfo = new QGroupBox();
    auto* infoV = new QVBoxLayout(m_grpInfo);

    m_tblDisks = new QTableWidget(0, 5);
    m_tblDisks->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_tblDisks->horizontalHeader()->setStretchLastSection(true);
    m_tblDisks->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tblDisks->setSelectionMode(QAbstractItemView::NoSelection);
    m_tblDisks->verticalHeader()->setVisible(false);
    m_tblDisks->setMinimumHeight(125);
    m_tblDisks->setMaximumHeight(175);
    infoV->addWidget(m_tblDisks);

    auto* efiRow = new QHBoxLayout();
    m_lblEfiStatus = new QLabel("EFI: …");
    m_lblEfiStatus->setWordWrap(true);
    m_btnRefM2 = new QPushButton();
    connect(m_btnRefM2, &QPushButton::clicked, this, [this]() { m_refreshM2(); });
    efiRow->addWidget(m_lblEfiStatus, 1);
    efiRow->addWidget(m_btnRefM2);
    infoV->addLayout(efiRow);

    m_lblRootGuard = new QLabel();
    m_lblRootGuard->setWordWrap(true);
    m_lblRootGuard->hide();
    infoV->addWidget(m_lblRootGuard);
    layout->addWidget(m_grpInfo);

    m_grpInit = new QGroupBox();
    auto* initV = new QVBoxLayout(m_grpInit);
    m_warnInit = new QLabel();
    m_warnInit->setWordWrap(true);
    initV->addWidget(m_warnInit);
    m_btnM2Init = new QPushButton();
    m_btnM2Init->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    initV->addWidget(m_btnM2Init);
    m_actionWidgets.append(m_btnM2Init);
    layout->addWidget(m_grpInit);

    m_grpImg = new QGroupBox();
    auto* imgV = new QVBoxLayout(m_grpImg);
    auto* fileRow = new QHBoxLayout();
    m_btnChooseImg = new QPushButton();
    m_lblImgPath = new QLabel();
    m_lblImgPath->setWordWrap(true);
    fileRow->addWidget(m_btnChooseImg);
    fileRow->addWidget(m_lblImgPath, 1);
    imgV->addLayout(fileRow);

    m_lblMountState = new QLabel();
    imgV->addWidget(m_lblMountState);

    auto* actRow = new QHBoxLayout();
    m_btnMount = new QPushButton();
    m_btnUnmount = new QPushButton();
    m_btnOpenMnt = new QPushButton();
    m_btnInstall = new QPushButton();
    for (auto* b : {m_btnMount, m_btnUnmount, m_btnOpenMnt, m_btnInstall}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        actRow->addWidget(b);
    }
    imgV->addLayout(actRow);
    m_actionWidgets << m_btnChooseImg << m_btnMount << m_btnUnmount << m_btnInstall;
    layout->addWidget(m_grpImg);

    m_grpBoot = new QGroupBox();
    auto* bootV = new QVBoxLayout(m_grpBoot);
    m_warnBoot = new QLabel();
    m_warnBoot->setWordWrap(true);
    bootV->addWidget(m_warnBoot);

    auto* bootRow = new QHBoxLayout();
    m_btnKexec = new QPushButton();
    m_btnWarmReboot = new QPushButton("🔄  Warm Reboot");
    for (auto* b : {m_btnKexec, m_btnWarmReboot}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        bootRow->addWidget(b);
        m_actionWidgets.append(b);
    }
    bootV->addLayout(bootRow);
    layout->addWidget(m_grpBoot);
    layout->addStretch();

    connect(m_btnM2Init, &QPushButton::clicked, this, &M2Tab::onM2Init);
    connect(m_btnChooseImg, &QPushButton::clicked, this, &M2Tab::onChooseImg);
    connect(m_btnMount, &QPushButton::clicked, this, &M2Tab::onMount);
    connect(m_btnUnmount, &QPushButton::clicked, this, [this]() {
        m_runAction("m2-unmount", {}, "umount /mnt/ps5_image_src", [this]() { m_refreshM2(); });
    });
    connect(m_btnOpenMnt, &QPushButton::clicked, this, &M2Tab::onOpenMnt);
    connect(m_btnInstall, &QPushButton::clicked, this, &M2Tab::onInstall);
    connect(m_btnKexec, &QPushButton::clicked, this, &M2Tab::onKexec);
    connect(m_btnWarmReboot, &QPushButton::clicked, this, &M2Tab::onWarmReboot);

    retranslateUi();
    refreshTheme();
}

void M2Tab::retranslateUi() {
    m_grpInfo->setTitle(tx("💽  Состояние дисков и /boot/efi", "💽  Disks & /boot/efi Status"));
    m_tblDisks->setHorizontalHeaderLabels({
        tx("Устройство", "Device"),
        tx("Размер", "Size"),
        tx("Метка", "Label"),
        tx("ФС", "FS"),
        tx("Точка монтирования", "Mountpoint"),
    });
    m_btnRefM2->setText(tx("↺  Обновить диски", "↺  Refresh Disks"));

    m_grpInit->setTitle(tx("🔧  Инициализация M.2 (/dev/nvme0n1)", "🔧  M.2 Initialization (/dev/nvme0n1)"));
    m_warnInit->setText(tx(
        "⚠  Создаёт гибридную MBR + GPT разметку для PS5 OS. Все данные на /dev/nvme0n1 будут стёрты!",
        "⚠  Creates a hybrid MBR + GPT layout for PS5 OS. All data on /dev/nvme0n1 will be erased!"));
    m_btnM2Init->setText(tx("🔧  M2 Init (инициализация GPT/MBR)", "🔧  M2 Init (GPT/MBR initialization)"));

    m_grpImg->setTitle(tx("💿  Образ Linux (.img)", "💿  Linux Image (.img)"));
    m_btnChooseImg->setText(tx("📂  Выбрать образ…", "📂  Choose image…"));
    if (m_imgPath.isEmpty()) m_lblImgPath->setText(tx("(файл не выбран)", "(no file selected)"));

    m_btnMount->setText(tx("💿  Смонтировать", "💿  Mount"));
    m_btnUnmount->setText(tx("⏏  Размонтировать", "⏏  Unmount"));
    m_btnOpenMnt->setText(tx("📁  Открыть в Dolphin", "📁  Open in File Manager"));
    m_btnInstall->setText(tx("📥  Установить на M.2", "📥  Install to M.2"));

    m_grpBoot->setTitle(tx("🚀  Загрузка (kexec / Warm Reboot)", "🚀  Boot (kexec / Warm Reboot)"));
    m_warnBoot->setText(tx(
        "⚠  Немедленно переключает или перезагружает систему! Сохраните открытые файлы.",
        "⚠  Immediately switches or reboots the system! Save open files first."));
    m_btnKexec->setText(tx("🚀  kexec Linux с M.2", "🚀  kexec Linux from M.2"));

    if (m_hasState) {
        updateM2Data(m_m2State);
    } else {
        m_lblEfiStatus->setText(tx("EFI: опрос…", "EFI: polling…"));
        m_lblMountState->setText(tx("Точка монтирования /mnt/ps5_image_src: не смонтирована",
                                    "Mountpoint /mnt/ps5_image_src: not mounted"));
    }
    refreshTheme();
}

void M2Tab::refreshTheme() {
    m_warnInit->setStyleSheet(QString("color: %1;").arg(m_theme.negative()));
    m_warnBoot->setStyleSheet(QString("color: %1;").arg(m_theme.negative()));
    if (m_imgPath.isEmpty())
        m_lblImgPath->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    if (m_hasState)
        updateM2Data(m_m2State);
    else
        m_lblMountState->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
}

void M2Tab::updateM2Data(const M2EnvironmentInfo& data) {
    m_m2State = data;
    m_hasState = true;

    m_tblDisks->setRowCount(data.devices.size());
    for (int r = 0; r < data.devices.size(); ++r) {
        const auto& d = data.devices[r];
        const QString vals[5] = {d.name, d.size, d.label, d.fstype, d.mountpoint};
        for (int c = 0; c < 5; ++c) {
            auto* it = new QTableWidgetItem(vals[c]);
            if (d.mountpoint == "/") {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
            m_tblDisks->setItem(r, c, it);
        }
    }

    QStringList efiParts;
    for (auto it = data.efiFiles.cbegin(); it != data.efiFiles.cend(); ++it) {
        QString col = it.value() ? m_theme.positive() : m_theme.negative();
        efiParts << QString("<span style=\"color:%1\">%2 %3</span>")
                        .arg(col, it.value() ? "✓" : "✗", it.key());
    }
    QString vramInfo = data.efiVram.isEmpty() ? "" : QString(" | VRAM cfg: <code>%1</code>").arg(data.efiVram);
    m_lblEfiStatus->setText("<b>/boot/efi:</b> " + efiParts.join(" &nbsp; ") + vramInfo);

    if (data.imgMounted) {
        m_lblMountState->setText(tx(
            QString("Точка монтирования /mnt/ps5_image_src: ✓ смонтирован (%1)").arg(data.imgMountSrc),
            QString("Mountpoint /mnt/ps5_image_src: ✓ mounted (%1)").arg(data.imgMountSrc)));
        m_lblMountState->setStyleSheet(QString("color: %1; font-weight: bold;").arg(m_theme.positive()));
    } else {
        m_lblMountState->setText(tx(
            "Точка монтирования /mnt/ps5_image_src: не смонтирована",
            "Mountpoint /mnt/ps5_image_src: not mounted"));
        m_lblMountState->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    }
    m_btnUnmount->setEnabled(data.imgMounted);
    m_btnOpenMnt->setEnabled(data.imgMounted);
    reapplyRootGuard();
}

void M2Tab::reapplyRootGuard() {
    if (!m_hasState) return;
    if (m_m2State.nvmeIsRoot) {
        m_lblRootGuard->setText(tx(
            "🛡️ <b>Защита активна:</b> текущая ОС работает с <code>/dev/nvme0n1p1</code> (<code>/</code>). "
            "Операции <b>M2 Init</b> и <b>Установить на M.2</b> заблокированы.",
            "🛡️ <b>Root guard active:</b> current OS runs from <code>/dev/nvme0n1p1</code> (<code>/</code>). "
            "<b>M2 Init</b> and <b>Install to M.2</b> are locked."));
        m_lblRootGuard->setStyleSheet(QString("color: %1;").arg(m_theme.neutral()));
        m_lblRootGuard->show();
        m_btnM2Init->setEnabled(false);
        m_btnInstall->setEnabled(false);
    } else {
        m_lblRootGuard->hide();
    }
}

void M2Tab::onM2Init() {
    if (m_m2State.nvmeIsRoot) return;
    if (QMessageBox::warning(this, tx("Подтверждение — M2 Init", "Confirmation — M2 Init"),
            tx("⚠ Перезапишет таблицу разделов на /dev/nvme0n1!\nВсе данные будут уничтожены. Продолжить?",
               "⚠ This will overwrite the partition table on /dev/nvme0n1!\nAll data will be destroyed. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        m_runAction("m2-init", {}, "m2_init", [this]() { m_refreshM2(); });
    }
}

void M2Tab::onChooseImg() {
    QString path = QFileDialog::getOpenFileName(this, tx("Выбрать образ диска", "Select Disk Image"), QDir::homePath(),
        tx("Образы дисков (*.img *.iso *.raw);;Все файлы (*)", "Disk images (*.img *.iso *.raw);;All files (*)"));
    if (!path.isEmpty()) {
        m_imgPath = path;
        m_lblImgPath->setText(path);
        m_lblImgPath->setStyleSheet("");
    }
}

void M2Tab::onMount() {
    if (m_imgPath.isEmpty()) {
        QMessageBox::information(this, tx("Файл не выбран", "No File Selected"),
                                 tx("Сначала выберите образ диска.", "Select a disk image first."));
        return;
    }
    m_runAction("m2-mount", {m_imgPath}, "m2_install.sh --mount " + m_imgPath, [this]() { m_refreshM2(); });
}

void M2Tab::onOpenMnt() {
    QString opener = QStandardPaths::findExecutable("dolphin");
    if (!opener.isEmpty())
        QProcess::startDetached(opener, {m_m2State.imgMountDir});
    else
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_m2State.imgMountDir));
}

void M2Tab::onInstall() {
    if (m_m2State.nvmeIsRoot) return;
    if (m_imgPath.isEmpty()) {
        QMessageBox::information(this, tx("Файл не выбран", "No File Selected"),
                                 tx("Сначала выберите образ диска.", "Select a disk image first."));
        return;
    }
    if (QMessageBox::warning(this, tx("Подтверждение — Установка на M.2", "Confirmation — Install to M.2"),
            tx(QString("⚠ Образ:\n%1\n\nРаздел /dev/nvme0n1p1 будет отформатирован в ext4! Продолжить?").arg(m_imgPath),
               QString("⚠ Image:\n%1\n\n/dev/nvme0n1p1 will be formatted as ext4! Continue?").arg(m_imgPath)),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        m_runAction("m2-install", {m_imgPath}, "m2_install.sh --install " + m_imgPath, [this]() { m_refreshM2(); });
    }
}

void M2Tab::onKexec() {
    if (QMessageBox::warning(this, tx("Подтверждение — kexec", "Confirmation — kexec"),
            tx("⚠ kexec немедленно загрузит ядро Linux с /dev/nvme0n1p1. Продолжить?",
               "⚠ kexec will immediately boot Linux from /dev/nvme0n1p1. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        m_runAction("m2-exec", {}, "m2_exec.sh (kexec)", nullptr);
    }
}

void M2Tab::onWarmReboot() {
    if (QMessageBox::warning(this, tx("Подтверждение — Warm Reboot", "Confirmation — Warm Reboot"),
            tx("⚠ Система выполнит тёплую перезагрузку через /boot/efi/bzImage. Продолжить?",
               "⚠ System will warm-reboot via /boot/efi/bzImage. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes) {
        m_runAction("warm-reboot", {}, "ps5-linux-warm-reboot.sh", nullptr);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// ServicesTab
// ═══════════════════════════════════════════════════════════════════════════

struct KernelFilterDef { const char* titleRu; const char* titleEn; const char* regex; };
static constexpr KernelFilterDef kKernelFilters[] = {
    {"PS5 / GPU / HDMI / BT / Wi-Fi",      "PS5 / GPU / HDMI / BT / Wi-Fi",      "amdgpu|drm|hdmi|verde|flava|hdcp|ps5|icc|mp1|bluetooth|iw620|mt7921|wlan"},
    {"Только ошибки ядра (err+)",          "Kernel errors only (err+)",          "__ERR__"},
    {"GPU и HDMI (amdgpu / drm / reset)",  "GPU & HDMI (amdgpu / drm / reset)",  "amdgpu|drm|hdmi|verde|flava|hdcp|reset"},
    {"Bluetooth и Wi-Fi",                  "Bluetooth & Wi-Fi",                  "bluetooth|hci|wlan|moal|iw620|mt7921"},
    {"PS5 Fan / ICC / MP1",                "PS5 Fan / ICC / MP1",                "ps5|icc|mp1|fan"},
    {"Все сообщения ядра (последние 200)", "All kernel messages (last 200)",     ""},
};

ServicesTab::ServicesTab(const KDETheme& theme, const PS5SystemInfo& ps5Info,
                         ActionCallback runAction, std::function<void(SimpleCallback)> installIntegration,
                         SimpleCallback refreshStatuses, QWidget* parent)
    : QWidget(parent), m_theme(theme), m_ps5Info(ps5Info),
      m_runAction(std::move(runAction)), m_installIntegration(std::move(installIntegration)),
      m_refreshStatuses(std::move(refreshStatuses)) {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(12, 12, 12, 12);

    auto* splitter = new QSplitter(Qt::Vertical);
    layout->addWidget(splitter);

    auto* topW = new QWidget();
    auto* topV = new QVBoxLayout(topW);
    topV->setContentsMargins(0, 0, 0, 0);
    topV->setSpacing(8);

    m_grpPk = new QGroupBox();
    auto* pkH = new QHBoxLayout(m_grpPk);
    m_lblPolkit = new QLabel("…");
    m_lblPolkit->setWordWrap(true);
    m_btnInstallPolkit = new QPushButton();
    connect(m_btnInstallPolkit, &QPushButton::clicked, this, &ServicesTab::onInstallIntegration);
    m_actionWidgets.append(m_btnInstallPolkit);
    pkH->addWidget(m_lblPolkit, 1);
    pkH->addWidget(m_btnInstallPolkit);
    topV->addWidget(m_grpPk);

    m_grpSrv = new QGroupBox();
    auto* srvV = new QVBoxLayout(m_grpSrv);
    const auto srvs = ps5Services();
    m_tblServices = new QTableWidget(srvs.size(), 4);
    m_tblServices->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tblServices->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tblServices->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tblServices->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_tblServices->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tblServices->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tblServices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tblServices->verticalHeader()->setVisible(false);
    for (int i = 0; i < srvs.size(); ++i) {
        m_tblServices->setItem(i, 0, new QTableWidgetItem(srvs[i].unit));
        m_tblServices->setItem(i, 1, new QTableWidgetItem("…"));
        m_tblServices->setItem(i, 2, new QTableWidgetItem("…"));
        m_tblServices->setItem(i, 3, new QTableWidgetItem(srvs[i].description));
    }
    m_tblServices->selectRow(0);
    srvV->addWidget(m_tblServices);

    auto* srvBtns = new QHBoxLayout();
    m_btnRestart = new QPushButton();
    m_btnStop = new QPushButton();
    m_btnEnable = new QPushButton();
    m_btnDisable = new QPushButton();
    m_btnLog = new QPushButton();
    m_btnRef = new QPushButton("↺");
    m_btnRef->setFixedWidth(34);
    for (auto* b : {m_btnRestart, m_btnStop, m_btnEnable, m_btnDisable}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        srvBtns->addWidget(b);
        m_actionWidgets.append(b);
    }
    srvBtns->addWidget(m_btnLog);
    srvBtns->addWidget(m_btnRef);
    srvV->addLayout(srvBtns);
    topV->addWidget(m_grpSrv);
    splitter->addWidget(topW);

    m_grpDiag = new QGroupBox();
    auto* diagV = new QVBoxLayout(m_grpDiag);
    auto* fltRow = new QHBoxLayout();
    m_lblFlt = new QLabel();
    fltRow->addWidget(m_lblFlt);
    m_cmbFilter = new QComboBox();
    connect(m_cmbFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, &ServicesTab::refreshJournal);
    m_btnDiagRef = new QPushButton();
    connect(m_btnDiagRef, &QPushButton::clicked, this, &ServicesTab::refreshJournal);
    m_btnCopyRep = new QPushButton();
    connect(m_btnCopyRep, &QPushButton::clicked, this, &ServicesTab::onCopyReport);
    fltRow->addWidget(m_cmbFilter, 1);
    fltRow->addWidget(m_btnDiagRef);
    fltRow->addWidget(m_btnCopyRep);
    diagV->addLayout(fltRow);

    m_txtJournal = new QTextEdit();
    m_txtJournal->setReadOnly(true);
    m_txtJournal->setFont(QFont("Monospace", 9));
    m_txtJournal->setLineWrapMode(QTextEdit::NoWrap);
    diagV->addWidget(m_txtJournal);
    splitter->addWidget(m_grpDiag);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    connect(m_btnRestart, &QPushButton::clicked, this, [this]() { actSelected("restart"); });
    connect(m_btnStop, &QPushButton::clicked, this, [this]() { actSelected("stop"); });
    connect(m_btnEnable, &QPushButton::clicked, this, [this]() { actSelected("enable"); });
    connect(m_btnDisable, &QPushButton::clicked, this, [this]() { actSelected("disable"); });
    connect(m_btnLog, &QPushButton::clicked, this, &ServicesTab::onViewUnitLog);
    connect(m_btnRef, &QPushButton::clicked, this, [this]() { m_refreshStatuses(); });

    retranslateUi();
    refreshIntegrationStatus();
}

void ServicesTab::updateSystemInfo(const PS5SystemInfo& info) {
    m_ps5Info = info;
}

void ServicesTab::retranslateUi() {
    m_grpPk->setTitle(tx("🔐  Системная интеграция и Polkit (auth_admin_keep)", "🔐  System Integration & Polkit (auth_admin_keep)"));
    m_btnInstallPolkit->setToolTip(tx(
        "Копирует <code>ps5tools-helper</code> в <code>/usr/local/libexec</code>, политику Polkit и недостающие утилиты.",
        "Installs <code>ps5tools-helper</code> to <code>/usr/local/libexec</code>, Polkit policy, and missing CLI binaries."));

    m_grpSrv->setTitle(tx("⚙️  Системные службы PS5", "⚙️  PS5 Systemd Services"));
    m_tblServices->setHorizontalHeaderLabels({
        tx("Служба", "Service"),
        tx("Состояние", "State"),
        tx("Автозапуск", "Autostart"),
        tx("Назначение", "Description"),
    });
    const auto srvs = ps5Services();
    for (int i = 0; i < srvs.size(); ++i) {
        m_tblServices->setItem(i, 3, new QTableWidgetItem(srvs[i].description));
    }

    m_btnRestart->setText(tx("▶  Запуск / Перезапуск", "▶  Start / Restart"));
    m_btnStop->setText(tx("■  Стоп", "■  Stop"));
    m_btnEnable->setText(tx("✓  Вкл. автозапуск", "✓  Enable Autostart"));
    m_btnDisable->setText(tx("✗  Откл. автозапуск", "✗  Disable Autostart"));
    m_btnLog->setText(tx("📄  Лог службы", "📄  Unit Log"));
    m_btnRef->setToolTip(tx("Обновить статусы служб", "Refresh service states"));

    m_grpDiag->setTitle(tx("🩺  Диагностика ядра Linux / служб (journalctl)", "🩺  Linux Kernel / Unit Diagnostics (journalctl)"));
    m_lblFlt->setText(tx("Фильтр:", "Filter:"));

    int cur = m_cmbFilter->currentIndex();
    m_cmbFilter->blockSignals(true);
    m_cmbFilter->clear();
    for (const auto& f : kKernelFilters)
        m_cmbFilter->addItem(tx(f.titleRu, f.titleEn), QString::fromUtf8(f.regex));
    m_cmbFilter->setCurrentIndex(cur >= 0 ? cur : 0);
    m_cmbFilter->blockSignals(false);

    m_btnDiagRef->setText(tx("⟳  Прочитать журнал", "⟳  Read Journal"));
    m_btnCopyRep->setText(tx("📋  Копировать отчёт", "📋  Copy Report"));

    refreshTheme();
}

void ServicesTab::refreshTheme() {
    refreshIntegrationStatus();
    if (m_hasStatuses) updateStatuses(m_lastStatuses);
}

void ServicesTab::refreshIntegrationStatus() {
    auto st = getSystemToolsStatus();
    bool pkOk = isPolkitHelperInstalled();
    QStringList missing;
    for (const char* k : {"ps5_control", "m2_init", "m2_install.sh", "m2_exec.sh", "ps5-linux-warm-reboot.sh"}) {
        if (!st.value(k)) missing << k;
    }
    if (pkOk && missing.isEmpty()) {
        m_lblPolkit->setText(tx(
            QString("<span style=\"color:%1\"><b>✓ Установлена</b></span> "
                    "(helper + polkit <code>auth_admin_keep</code> + все утилиты в <code>/usr/local/sbin</code>)").arg(m_theme.positive()),
            QString("<span style=\"color:%1\"><b>✓ Installed</b></span> "
                    "(helper + polkit <code>auth_admin_keep</code> + all tools in <code>/usr/local/sbin</code>)").arg(m_theme.positive())));
        m_btnInstallPolkit->setText(tx("🔐  Переустановить / Обновить интеграцию", "🔐  Reinstall / Update Integration"));
    } else {
        QStringList det;
        if (!pkOk) det << tx("нет helper/polkit", "missing helper/polkit");
        if (!missing.isEmpty()) det << (tx("в /usr/local/sbin отсутствуют: ", "missing in /usr/local/sbin: ") + missing.join(", "));
        m_lblPolkit->setText(tx(
            QString("<span style=\"color:%1\"><b>○ Не установлена полностью</b></span> (%2)").arg(m_theme.neutral(), det.join("; ")),
            QString("<span style=\"color:%1\"><b>○ Not fully installed</b></span> (%2)").arg(m_theme.neutral(), det.join("; "))));
        m_btnInstallPolkit->setText(tx("🔐  Установить / Обновить интеграцию", "🔐  Install / Update Integration"));
    }
}

void ServicesTab::updateStatuses(const ServiceStatusMap& statuses) {
    m_lastStatuses = statuses;
    m_hasStatuses = true;
    const auto srvs = ps5Services();
    for (int i = 0; i < srvs.size(); ++i) {
        const auto inf = statuses.value(srvs[i].unit);
        QString actTxt = inf.active == 1 ? ("● " + inf.activeState) : inf.active == 0 ? ("○ " + inf.activeState) : "? unknown";
        QString actC = inf.active == 1 ? m_theme.positive() : inf.active == 0 ? m_theme.muted() : m_theme.neutral();
        auto* itAct = new QTableWidgetItem(actTxt);
        itAct->setForeground(QColor(actC));
        m_tblServices->setItem(i, 1, itAct);

        auto* itEn = new QTableWidgetItem(inf.unitFileState);
        itEn->setForeground(QColor(inf.enabled ? m_theme.positive() : m_theme.muted()));
        m_tblServices->setItem(i, 2, itEn);
    }
}

QString ServicesTab::selectedUnit() const {
    int row = m_tblServices->currentRow();
    if (row < 0) return {};
    auto* it = m_tblServices->item(row, 0);
    return it ? it->text() : QString();
}

void ServicesTab::actSelected(const QString& op) {
    QString u = selectedUnit();
    if (!u.isEmpty())
        m_runAction("systemctl", {op, u}, QString("systemctl %1 %2").arg(op, u), nullptr);
}

void ServicesTab::onViewUnitLog() {
    QString u = selectedUnit();
    if (!u.isEmpty()) startJournalPoller("unit", u);
}

void ServicesTab::refreshJournal() {
    QString rx = m_cmbFilter->currentData().toString();
    if (rx == "__ERR__") startJournalPoller("kernel_err");
    else startJournalPoller("kernel", "", rx);
}

void ServicesTab::startJournalPoller(const QString& mode, const QString& unit, const QString& filter) {
    m_txtJournal->setPlainText(tx("Загрузка журнала…", "Loading journal…"));
    auto* p = new JournalPoller(mode, unit, filter, 180, this);
    connect(p, &JournalPoller::journalReady, this, &ServicesTab::onJournalReady);
    connect(p, &QThread::finished, this, [this, p]() {
        m_pollers.removeAll(p);
        p->deleteLater();
    });
    m_pollers.append(p);
    p->start();
}

void ServicesTab::onJournalReady(const QString& title, const QString& text) {
    m_txtJournal->setPlainText(QString("=== %1 ===\n\n%2").arg(title, text));
}

void ServicesTab::onCopyReport() {
    QString hdr = QString("Model: %1\nKernel: %2\nSession: %3\nVBIOS: %4\n--------------------------------------------------\n")
                      .arg(m_ps5Info.model, m_ps5Info.kernel, m_ps5Info.sessionType, m_ps5Info.vbios);
    QApplication::clipboard()->setText(hdr + m_txtJournal->toPlainText());
    QMessageBox::information(this, tx("Скопировано", "Copied"),
                             tx("Диагностический отчёт скопирован в буфер обмена.", "Diagnostic report copied to clipboard."));
}

void ServicesTab::onInstallIntegration() {
    if (QMessageBox::question(this, tx("Установка системной интеграции", "Install System Integration"),
            tx("Будут установлены в систему (через один запрос pkexec):\n"
               "• /usr/local/libexec/ps5tools-helper\n"
               "• /usr/share/polkit-1/actions/org.ps5linuxtools.policy (auth_admin_keep)\n"
               "• Недостающие утилиты ps5-linux-tools в /usr/local/sbin/\n\nПродолжить?",
               "The following files will be installed system-wide (via one pkexec prompt):\n"
               "• /usr/local/libexec/ps5tools-helper\n"
               "• /usr/share/polkit-1/actions/org.ps5linuxtools.policy (auth_admin_keep)\n"
               "• Missing ps5-linux-tools binaries in /usr/local/sbin/\n\nContinue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
        m_installIntegration([this]() { refreshIntegrationStatus(); });
    }
}

void ServicesTab::waitPollers() {
    for (auto* p : m_pollers) {
        if (p && p->isRunning()) p->wait(1000);
    }
}

} // namespace ps5
