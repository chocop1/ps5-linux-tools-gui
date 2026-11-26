#include "tabs.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <cmath>

namespace ps5 {

struct FanPreset {
    const char* id;
    const char* nameRu;
    const char* nameEn;
    int hystC;
    int temps[8];
    int pwms[8];
};

static constexpr FanPreset kFanPresets[] = {
    {"quiet",    "Тихий (до 70 °C минимум шума)",   "Quiet (minimal noise below 70 °C)", 48, {55, 62, 68, 73, 78, 82, 86, 88}, {26, 51, 77, 102, 128, 166, 210, 255}},
    {"balanced", "Сбалансированный (близко к стоку)", "Balanced (close to stock)",         45, {50, 60, 65, 70, 75, 80, 85, 87}, {26, 77, 102, 128, 166, 204, 242, 255}},
    {"gaming",   "Игровой / Холодный",               "Gaming / Cool",                     40, {45, 53, 60, 66, 72, 77, 82, 85}, {51, 89, 128, 166, 204, 230, 255, 255}},
};

static const char* kMangoHudConf = R"(### MangoHud profile for PS5 Linux
fps
frametime
gpu_stats
gpu_temp
gpu_core_clock
gpu_mem_clock
vram
cpu_stats
cpu_temp
cpu_mhz
ram
fan
custom_text=PS5 Linux
toggle_hud=Shift_R+F12
toggle_hud_position=Shift_R+F11
font_size=20
background_alpha=0.55
round_corners=8
)";

// ═══════════════════════════════════════════════════════════════════════════
// ControlTab
// ═══════════════════════════════════════════════════════════════════════════

ControlTab::ControlTab(const KDETheme& theme, ActionCallback runAction, QWidget* parent)
    : QScrollArea(parent), m_theme(theme), m_runAction(std::move(runAction)) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget();
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(12);
    layout->setContentsMargins(12, 12, 12, 12);
    setWidget(inner);

    // 1. Fan control
    m_grpFan = new QGroupBox();
    auto* fanV = new QVBoxLayout(m_grpFan);
    auto* rowFan = new QHBoxLayout();
    m_btnFanOn = new QPushButton("🌀  Fan ON");
    m_btnFanOff = new QPushButton("🔇  Fan OFF");
    m_btnFanMax = new QPushButton("💨  Fan MAX");
    for (auto* b : {m_btnFanOn, m_btnFanOff, m_btnFanMax}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rowFan->addWidget(b);
        m_actionWidgets.append(b);
    }
    fanV->addLayout(rowFan);

    auto* fanSrvRow = new QHBoxLayout();
    m_lblFanStatus = new QLabel("ps5fan.service: …");
    m_lblFanLive = new QLabel("PWM: —");
    fanSrvRow->addWidget(m_lblFanStatus);
    fanSrvRow->addStretch();
    fanSrvRow->addWidget(m_lblFanLive);
    fanV->addLayout(fanSrvRow);

    m_chkFanAutostart = new QCheckBox();
    fanV->addWidget(m_chkFanAutostart);
    m_actionWidgets.append(m_chkFanAutostart);
    layout->addWidget(m_grpFan);

    // 2. Boost mode
    m_grpBoost = new QGroupBox();
    auto* boostV = new QVBoxLayout(m_grpBoost);
    auto* rowBoost = new QHBoxLayout();
    m_btnBoostOn = new QPushButton("⚡  Boost ON");
    m_btnBoostOff = new QPushButton("💤  Boost OFF");
    for (auto* b : {m_btnBoostOn, m_btnBoostOff}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        rowBoost->addWidget(b);
        m_actionWidgets.append(b);
    }
    boostV->addLayout(rowBoost);

    auto* boostSrvRow = new QHBoxLayout();
    m_lblBoostStatus = new QLabel("ps5boost.service: …");
    m_lblBoostClocks = new QLabel("CPU: — | GPU: —");
    boostSrvRow->addWidget(m_lblBoostStatus);
    boostSrvRow->addStretch();
    boostSrvRow->addWidget(m_lblBoostClocks);
    boostV->addLayout(boostSrvRow);

    m_chkBoostAutostart = new QCheckBox();
    boostV->addWidget(m_chkBoostAutostart);
    m_actionWidgets.append(m_chkBoostAutostart);
    layout->addWidget(m_grpBoost);

    // 3. Fan curve
    m_grpCurve = new QGroupBox();
    auto* curveV = new QVBoxLayout(m_grpCurve);

    auto* manRow = new QHBoxLayout();
    m_lblManualPwm = new QLabel();
    manRow->addWidget(m_lblManualPwm);
    m_sldPwm = new QSlider(Qt::Horizontal);
    m_sldPwm->setRange(0, 100);
    m_sldPwm->setValue(30);
    m_lblPwmVal = new QLabel("30%");
    m_lblPwmVal->setFixedWidth(40);
    m_btnPwmApply = new QPushButton();
    m_btnPwmAuto = new QPushButton();
    connect(m_sldPwm, &QSlider::valueChanged, this, [this](int v) { m_lblPwmVal->setText(QString::number(v) + "%"); });
    manRow->addWidget(m_sldPwm, 1);
    manRow->addWidget(m_lblPwmVal);
    manRow->addWidget(m_btnPwmApply);
    manRow->addWidget(m_btnPwmAuto);
    curveV->addLayout(manRow);
    m_actionWidgets << m_btnPwmApply << m_btnPwmAuto;

    m_chart = new FanCurveChart(m_theme);
    curveV->addWidget(m_chart);

    auto* presetRow = new QHBoxLayout();
    m_lblPreset = new QLabel();
    presetRow->addWidget(m_lblPreset);
    m_cmbPreset = new QComboBox();
    m_btnLoadPreset = new QPushButton();
    connect(m_btnLoadPreset, &QPushButton::clicked, this, &ControlTab::onLoadPreset);

    presetRow->addWidget(m_cmbPreset, 1);
    presetRow->addWidget(m_btnLoadPreset);
    presetRow->addSpacing(12);
    m_lblHyst = new QLabel();
    presetRow->addWidget(m_lblHyst);
    m_spnHyst = new QSpinBox();
    m_spnHyst->setRange(25, 80);
    m_spnHyst->setValue(45);
    presetRow->addWidget(m_spnHyst);
    curveV->addLayout(presetRow);

    auto* ptsGrid = new QGridLayout();
    ptsGrid->setSpacing(6);
    for (int i = 0; i < 8; ++i) {
        int col = (i % 4) * 3;
        int row = (i / 4) * 2;
        auto* lbl = new QLabel();
        lbl->setStyleSheet("font-weight: bold; font-size: 10px;");
        auto* spnT = new QSpinBox();
        spnT->setRange(25, 100);
        spnT->setSuffix(" °C");
        auto* spnP = new QSpinBox();
        spnP->setRange(0, 100);
        spnP->setSuffix(" %");
        connect(spnT, qOverload<int>(&QSpinBox::valueChanged), this, &ControlTab::refreshChartFromInputs);
        connect(spnP, qOverload<int>(&QSpinBox::valueChanged), this, &ControlTab::refreshChartFromInputs);
        ptsGrid->addWidget(lbl, row, col, 1, 2);
        ptsGrid->addWidget(spnT, row + 1, col);
        ptsGrid->addWidget(spnP, row + 1, col + 1);
        m_ptLabels.append(lbl);
        m_ptSpins.emplaceBack(spnT, spnP);
    }
    curveV->addLayout(ptsGrid);

    auto* curveBtns = new QHBoxLayout();
    m_btnCurveApply = new QPushButton();
    m_btnCurveReset = new QPushButton();
    m_btnCurveReload = new QPushButton();
    m_btnCurvePersist = new QPushButton();
    for (auto* b : {m_btnCurveApply, m_btnCurveReset, m_btnCurveReload, m_btnCurvePersist}) {
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        curveBtns->addWidget(b);
        m_actionWidgets.append(b);
    }
    curveV->addLayout(curveBtns);
    layout->addWidget(m_grpCurve);
    layout->addStretch();

    // Signals
    connect(m_btnFanOn, &QPushButton::clicked, this, [this]() { m_runAction("fan", {"on"}, "ps5_control --fan on", nullptr); });
    connect(m_btnFanOff, &QPushButton::clicked, this, [this]() { m_runAction("fan", {"off"}, "ps5_control --fan off", nullptr); });
    connect(m_btnFanMax, &QPushButton::clicked, this, [this]() { m_runAction("fan", {"max"}, "ps5_control --fan max", nullptr); });
    connect(m_btnBoostOn, &QPushButton::clicked, this, [this]() { m_runAction("boost", {"on"}, "ps5_control --boost on", nullptr); });
    connect(m_btnBoostOff, &QPushButton::clicked, this, [this]() { m_runAction("boost", {"off"}, "ps5_control --boost off", nullptr); });

    connect(m_chkFanAutostart, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState st) {
        confirmAutostart("ps5fan.service", static_cast<int>(st), m_chkFanAutostart);
    });
    connect(m_chkBoostAutostart, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState st) {
        confirmAutostart("ps5boost.service", static_cast<int>(st), m_chkBoostAutostart);
    });

    connect(m_btnPwmApply, &QPushButton::clicked, this, &ControlTab::onApplyManualPwm);
    connect(m_btnPwmAuto, &QPushButton::clicked, this, [this]() {
        m_runAction("fan-auto", {}, "pwm1_enable=2 (auto curve)", nullptr);
    });
    connect(m_btnCurveApply, &QPushButton::clicked, this, &ControlTab::onApplyCurve);
    connect(m_btnCurveReset, &QPushButton::clicked, this, [this]() {
        m_runAction("fan-curve-reset", {}, "curve_reset", [this]() { loadFanCurve(); });
    });
    connect(m_btnCurveReload, &QPushButton::clicked, this, &ControlTab::loadFanCurve);
    connect(m_btnCurvePersist, &QPushButton::clicked, this, &ControlTab::onPersistCurve);

    retranslateUi();
    loadFanCurve();
}

void ControlTab::updateCurveGroupTitle() {
    if (m_curveAvailable) {
        m_grpCurve->setTitle(tx(
            "📈  Кривая вентилятора и ручной ШИМ (ps5_fan — ядро 7.3.3)",
            "📈  Fan Curve & Manual PWM (ps5_fan — kernel 7.3.3+)"));
    } else {
        m_grpCurve->setTitle(tx(
            "📈  Кривая вентилятора (ps5_fan hwmon не обнаружен)",
            "📈  Fan Curve (ps5_fan hwmon not detected)"));
    }
}

void ControlTab::retranslateUi() {
    m_grpFan->setTitle(tx("🌀  Вентилятор (/dev/icc)", "🌀  Fan Control (/dev/icc)"));
    m_btnFanOn->setToolTip(tx(
        "<b>Fan ON</b><br>Включает вентилятор PS5 (servo pattern 1).<br><code>ps5_control --fan on</code>",
        "<b>Fan ON</b><br>Enables PS5 standard fan curve mode (servo pattern 1).<br><code>ps5_control --fan on</code>"));
    m_btnFanOff->setToolTip(tx(
        "<b>Fan OFF</b><br>Возвращает базовый режим контроллера (pattern 0).<br><code>ps5_control --fan off</code>",
        "<b>Fan OFF</b><br>Returns control to base PS5 controller (pattern 0).<br><code>ps5_control --fan off</code>"));
    m_btnFanMax->setToolTip(tx(
        "<b>Fan MAX</b><br>Максимальная скорость вращения (pattern 3).<br><code>ps5_control --fan max</code>",
        "<b>Fan MAX</b><br>Forces maximum fan speed (pattern 3).<br><code>ps5_control --fan max</code>"));
    m_chkFanAutostart->setText(tx("Автозапуск ps5fan.service при загрузке", "Autostart ps5fan.service at boot"));
    m_chkFanAutostart->setToolTip(tx(
        "Включает/отключает службу <code>ps5fan.service</code> в автозапуске systemd.",
        "Enables/disables systemd unit <code>ps5fan.service</code> at boot."));

    m_grpBoost->setTitle(tx("⚡  Boost режим (/dev/mp1)", "⚡  Boost Mode (/dev/mp1)"));
    m_btnBoostOn->setToolTip(tx(
        "<b>Boost ON</b><br>Повышенные частоты CPU/GPU.<br><code>ps5_control --boost on</code>",
        "<b>Boost ON</b><br>Enables boosted CPU/GPU clocks.<br><code>ps5_control --boost on</code>"));
    m_btnBoostOff->setToolTip(tx(
        "<b>Boost OFF</b><br>Отключает Boost-режим.<br><code>ps5_control --boost off</code>",
        "<b>Boost OFF</b><br>Disables Boost mode.<br><code>ps5_control --boost off</code>"));
    m_chkBoostAutostart->setText(tx("Автозапуск ps5boost.service при загрузке", "Autostart ps5boost.service at boot"));
    m_chkBoostAutostart->setToolTip(tx(
        "Включает/отключает службу <code>ps5boost.service</code> в автозапуске systemd.",
        "Enables/disables systemd unit <code>ps5boost.service</code> at boot."));

    updateCurveGroupTitle();
    m_lblManualPwm->setText(tx("Ручной PWM:", "Manual PWM:"));
    m_btnPwmApply->setText(tx("Задать PWM", "Set PWM"));
    m_btnPwmApply->setToolTip(tx(
        "Переключает <code>pwm1_enable=1</code> и задаёт фиксированную скорость.",
        "Switches <code>pwm1_enable=1</code> and sets a fixed fan duty cycle."));
    m_btnPwmAuto->setText(tx("Авто (кривая)", "Auto (curve)"));
    m_btnPwmAuto->setToolTip(tx(
        "Возвращает управление температурной кривой (<code>pwm1_enable=2</code>).",
        "Returns fan control to the temperature curve (<code>pwm1_enable=2</code>)."));

    m_lblPreset->setText(tx("Пресет:", "Preset:"));
    int curPreset = m_cmbPreset->currentIndex();
    m_cmbPreset->blockSignals(true);
    m_cmbPreset->clear();
    for (const auto& p : kFanPresets) m_cmbPreset->addItem(tx(p.nameRu, p.nameEn), p.id);
    if (curPreset >= 0 && curPreset < m_cmbPreset->count()) m_cmbPreset->setCurrentIndex(curPreset);
    m_cmbPreset->blockSignals(false);

    m_btnLoadPreset->setText(tx("Загрузить пресет", "Load preset"));
    m_lblHyst->setText(tx("Гистерезис (°C):", "Hysteresis (°C):"));
    for (int i = 0; i < m_ptLabels.size(); ++i)
        m_ptLabels[i]->setText(tx("Точка %1:", "Point %1:").arg(i + 1));

    m_btnCurveApply->setText(tx("✓  Применить кривую", "✓  Apply curve"));
    m_btnCurveReset->setText(tx("↺  Сбросить (curve_reset)", "↺  Reset (curve_reset)"));
    m_btnCurveReload->setText(tx("⟳  Прочитать текущую", "⟳  Reload current"));
    m_btnCurvePersist->setText(tx("📌  В автозапуск", "📌  Persist at boot"));

    refreshTheme();
    if (m_hasStatuses) {
        updateServiceStatus(m_lastStatuses);
    } else {
        m_lblFanStatus->setText(tx("ps5fan.service: ● проверка…", "ps5fan.service: ● checking…"));
        m_lblBoostStatus->setText(tx("ps5boost.service: ● проверка…", "ps5boost.service: ● checking…"));
    }
    if (m_hasSensors) updateLiveSensors(m_lastSensors);
}

void ControlTab::refreshTheme() {
    m_lblFanLive->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    m_lblBoostClocks->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    if (m_hasStatuses) updateServiceStatus(m_lastStatuses);
    m_chart->update();
}

void ControlTab::setSrvLabel(QLabel* lbl, const QString& name, const ServiceStatusInfo& inf) {
    QString color = inf.active == 1 ? m_theme.positive() : inf.active == 0 ? m_theme.negative() : m_theme.neutral();
    QString txt = inf.active == 1 ? tx("● активен", "● active")
                : inf.active == 0 ? tx("● неактивен", "● inactive")
                                  : tx("● неизвестно", "● unknown");
    lbl->setText(name + ": " + txt);
    lbl->setStyleSheet(QString("color: %1; font-weight: bold;").arg(color));
}

void ControlTab::updateServiceStatus(const ServiceStatusMap& statuses) {
    m_lastStatuses = statuses;
    m_hasStatuses = true;
    const auto fan = statuses.value("ps5fan.service");
    const auto boost = statuses.value("ps5boost.service");
    setSrvLabel(m_lblFanStatus, "ps5fan.service", fan);
    setSrvLabel(m_lblBoostStatus, "ps5boost.service", boost);

    m_chkFanAutostart->blockSignals(true);
    m_chkFanAutostart->setChecked(fan.enabled);
    m_chkFanAutostart->blockSignals(false);

    m_chkBoostAutostart->blockSignals(true);
    m_chkBoostAutostart->setChecked(boost.enabled);
    m_chkBoostAutostart->blockSignals(false);
}

void ControlTab::updateLiveSensors(const SensorSnapshot& data) {
    m_lastSensors = data;
    m_hasSensors = true;
    QString modeStr = data.fanPwmEnable == 0 ? "Full/Off"
                    : data.fanPwmEnable == 1 ? tx("Ручной", "Manual")
                    : data.fanPwmEnable == 2 ? tx("Авто", "Auto") : "—";
    if (data.hasFanPwm) {
        m_lblFanLive->setText(QString("PWM: %1% (%2/255, %3: %4)")
                                  .arg(data.fanPwmPct).arg(data.fanPwm).arg(tx("режим", "mode"), modeStr));
    }

    const QString mhz = tx("МГц", "MHz");
    QString cpuS = data.hasCpuFreq ? QString("%1 %2").arg(data.cpuFreqMhz).arg(mhz) : "—";
    QString gpuS = data.hasGpuFreq ? QString("%1 %2").arg(data.gpuFreqMhz).arg(mhz) : "—";
    m_lblBoostClocks->setText(QString("CPU: %1  |  GPU: %2").arg(cpuS, gpuS));

    if (data.fanTemps[0].valid)
        m_chart->setCurrentTemp(data.fanTemps[0].tempC);
}

void ControlTab::confirmAutostart(const QString& unit, int state, QCheckBox* chk) {
    bool enable = (state == Qt::Checked);
    QString verb = tx(enable ? "включить" : "отключить", enable ? "enable" : "disable");
    auto reply = QMessageBox::question(
        this,
        tx("Автозапуск ", "Autostart ") + unit,
        tx(QString("Вы действительно хотите %1 автозапуск %2 при старте системы?").arg(verb, unit),
           QString("Are you sure you want to %1 %2 autostart at boot?").arg(verb, unit)),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        chk->blockSignals(true);
        chk->setChecked(!enable);
        chk->blockSignals(false);
        return;
    }
    QString op = enable ? "enable" : "disable";
    m_runAction("systemctl", {op, unit}, QString("systemctl %1 %2").arg(op, unit), nullptr);
}

void ControlTab::loadFanCurve() {
    FanCurveInfo info = readFanCurve();
    m_curveAvailable = info.available;
    updateCurveGroupTitle();
    if (!info.available) {
        m_grpCurve->setEnabled(false);
        return;
    }
    m_grpCurve->setEnabled(true);
    m_spnHyst->blockSignals(true);
    m_spnHyst->setValue(info.hystC);
    m_spnHyst->blockSignals(false);

    for (int i = 0; i < 8 && i < info.points.size(); ++i) {
        m_ptSpins[i].first->blockSignals(true);
        m_ptSpins[i].second->blockSignals(true);
        m_ptSpins[i].first->setValue(info.points[i].first);
        m_ptSpins[i].second->setValue(qRound(info.points[i].second / 255.0 * 100.0));
        m_ptSpins[i].first->blockSignals(false);
        m_ptSpins[i].second->blockSignals(false);
    }
    m_sldPwm->setValue(qRound(info.pwmNow / 255.0 * 100.0));
    m_chart->setData(info.points);
}

void ControlTab::onLoadPreset() {
    int idx = m_cmbPreset->currentIndex();
    if (idx < 0 || idx >= 3) return;
    const auto& p = kFanPresets[idx];
    m_spnHyst->setValue(p.hystC);
    for (int i = 0; i < 8; ++i) {
        m_ptSpins[i].first->blockSignals(true);
        m_ptSpins[i].second->blockSignals(true);
        m_ptSpins[i].first->setValue(p.temps[i]);
        m_ptSpins[i].second->setValue(qRound(p.pwms[i] / 255.0 * 100.0));
        m_ptSpins[i].first->blockSignals(false);
        m_ptSpins[i].second->blockSignals(false);
    }
    refreshChartFromInputs();
}

void ControlTab::refreshChartFromInputs() {
    QVector<QPair<int, int>> pts;
    pts.reserve(8);
    for (const auto& sp : m_ptSpins)
        pts.append({sp.first->value(), qRound(sp.second->value() * 255.0 / 100.0)});
    m_chart->setData(pts);
}

QStringList ControlTab::collectCurveArgs() {
    QStringList args;
    args << QString::number(m_spnHyst->value() * 1000);
    int prevT = 0;
    for (int i = 0; i < m_ptSpins.size(); ++i) {
        int tC = m_ptSpins[i].first->value();
        if (tC < prevT) {
            QMessageBox::warning(this, tx("Некорректная кривая", "Invalid fan curve"),
                tx(QString("Температура в точке %1 (%2 °C) не может быть меньше предыдущей (%3 °C)."),
                   QString("Point %1 temperature (%2 °C) cannot be lower than previous point (%3 °C)."))
                    .arg(i + 1).arg(tC).arg(prevT));
            return {};
        }
        prevT = tC;
        int pwm = qRound(m_ptSpins[i].second->value() * 255.0 / 100.0);
        args << QString("%1:%2").arg(tC * 1000).arg(pwm);
    }
    return args;
}

void ControlTab::onApplyManualPwm() {
    int pct = m_sldPwm->value();
    int pwm = qRound(pct * 255.0 / 100.0);
    m_runAction("fan-pwm", {QString::number(pwm)}, QString("Manual PWM = %1% (%2/255)").arg(pct).arg(pwm), nullptr);
}

void ControlTab::onApplyCurve() {
    QStringList args = collectCurveArgs();
    if (args.isEmpty()) return;
    m_runAction("fan-curve", args, tx("Применение кривой ps5_fan", "Apply ps5_fan curve"), [this]() { loadFanCurve(); });
}

void ControlTab::onPersistCurve() {
    if (!isPolkitHelperInstalled()) {
        QMessageBox::information(this, tx("Требуется системный helper", "System helper required"),
            tx("Для создания автозапуска кривой (ps5-fan-curve.service) сначала "
               "установите системную интеграцию на вкладке «⚙️ Службы и Ядро».",
               "To persist the fan curve (ps5-fan-curve.service), first install "
               "system integration in the \"⚙️ Services & Kernel\" tab."));
        return;
    }
    QStringList args = collectCurveArgs();
    if (args.isEmpty()) return;
    m_runAction("fan-curve-persist", QStringList{"enable"} + args,
                "Enable ps5-fan-curve.service", [this]() { loadFanCurve(); });
}

// ═══════════════════════════════════════════════════════════════════════════
// MonitorTab
// ═══════════════════════════════════════════════════════════════════════════

MonitorTab::MonitorTab(const KDETheme& theme, const PS5SystemInfo& ps5Info,
                       std::function<void(bool)> toggleOverlay, SimpleCallback openOverlayCfg,
                       SimpleCallback detachOverlay, bool isDaemonMode,
                       LogCallback logCb, QWidget* parent)
    : QScrollArea(parent), m_theme(theme), m_ps5Info(ps5Info),
      m_isDaemonMode(isDaemonMode), m_log(std::move(logCb)) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget();
    auto* outer = new QVBoxLayout(inner);
    outer->setSpacing(10);
    outer->setContentsMargins(12, 12, 12, 12);
    setWidget(inner);

    m_topBox = new QGroupBox();
    auto* topV = new QVBoxLayout(m_topBox);
    m_sysLbl = new QLabel();
    m_sysLbl->setWordWrap(true);
    topV->addWidget(m_sysLbl);

    auto* ovRow = new QHBoxLayout();
    m_btnOverlay = new QPushButton();
    m_btnOverlay->setCheckable(true);
    connect(m_btnOverlay, &QPushButton::clicked, this, std::move(toggleOverlay));

    m_btnOvCfg = new QPushButton();
    connect(m_btnOvCfg, &QPushButton::clicked, this, std::move(openOverlayCfg));

    m_btnMango = new QPushButton();
    connect(m_btnMango, &QPushButton::clicked, this, &MonitorTab::onMangohudConfig);

    ovRow->addWidget(m_btnOverlay);
    ovRow->addWidget(m_btnOvCfg);
    if (isDaemonMode) {
        m_btnDetach = new QPushButton();
        connect(m_btnDetach, &QPushButton::clicked, this, std::move(detachOverlay));
        ovRow->addWidget(m_btnDetach);
    }
    ovRow->addWidget(m_btnMango);
    ovRow->addStretch();
    topV->addLayout(ovRow);

    if (isDaemonMode) {
        m_modeLbl = new QLabel();
        m_modeLbl->setWordWrap(true);
        topV->addWidget(m_modeLbl);
    }
    outer->addWidget(m_topBox);

    m_grpPs5 = new QGroupBox();
    auto* gridPs5 = new QGridLayout(m_grpPs5);
    gridPs5->setColumnMinimumWidth(0, 115);
    gridPs5->setColumnStretch(1, 1);
    for (int i = 0; i < 4; ++i) {
        m_ps5Rows[i].name = new QLabel("—");
        m_ps5Rows[i].bar = new TempBar(100);
        m_ps5Rows[i].val = new QLabel("—");
        m_ps5Rows[i].val->setFixedWidth(72);
        m_ps5Rows[i].val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gridPs5->addWidget(m_ps5Rows[i].name, i, 0);
        gridPs5->addWidget(m_ps5Rows[i].bar, i, 1);
        gridPs5->addWidget(m_ps5Rows[i].val, i, 2);
    }
    outer->addWidget(m_grpPs5);

    m_grpFan = new QGroupBox();
    auto* fanV = new QVBoxLayout(m_grpFan);
    auto* fanRow = new QHBoxLayout();
    m_lblFanPct = new QLabel("—");
    m_lblFanPct->setStyleSheet("font-size: 18px; font-weight: bold;");
    m_lblFanPct->setFixedWidth(60);
    m_barFan = new TempBar(100);
    m_lblFanMeta = new QLabel("pwm1: —");
    fanRow->addWidget(m_lblFanPct);
    fanRow->addWidget(m_barFan, 1);
    fanRow->addWidget(m_lblFanMeta);
    fanV->addLayout(fanRow);
    outer->addWidget(m_grpFan);

    m_grpGpu = new QGroupBox("🎮  GPU (amdgpu)");
    auto* gGpu = new QGridLayout(m_grpGpu);
    gGpu->setColumnMinimumWidth(0, 115);
    gGpu->setColumnStretch(1, 1);
    m_barGpuTemp = new TempBar(105);
    m_lblGpuTemp = new QLabel("—");
    m_lblGpuTemp->setFixedWidth(72);
    m_lblGpuTemp->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gGpu->addWidget(new QLabel("Edge:"), 0, 0);
    gGpu->addWidget(m_barGpuTemp, 0, 1);
    gGpu->addWidget(m_lblGpuTemp, 0, 2);

    m_lblGpuLoadCap = new QLabel();
    m_barGpuBusy = new TempBar(100, true);
    gGpu->addWidget(m_lblGpuLoadCap, 1, 0);
    gGpu->addWidget(m_barGpuBusy, 1, 1, 1, 2);

    m_barVram = new TempBar(100, true);
    gGpu->addWidget(new QLabel("VRAM:"), 2, 0);
    gGpu->addWidget(m_barVram, 2, 1, 1, 2);

    m_lblGpuMeta = new QLabel("sclk: —  |  GTT: —  |  PM: —");
    m_lblGpuMeta->setWordWrap(true);
    gGpu->addWidget(m_lblGpuMeta, 3, 0, 1, 3);
    outer->addWidget(m_grpGpu);

    m_grpSys = new QGroupBox();
    auto* gSys = new QGridLayout(m_grpSys);
    gSys->setColumnMinimumWidth(0, 115);
    gSys->setColumnStretch(1, 1);
    m_barCpu = new TempBar(100);
    m_lblCpu = new QLabel("—");
    m_lblCpu->setFixedWidth(130);
    m_lblCpu->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gSys->addWidget(new QLabel("CPU Tctl / MHz:"), 0, 0);
    gSys->addWidget(m_barCpu, 0, 1);
    gSys->addWidget(m_lblCpu, 0, 2);

    for (int i = 0; i < 2; ++i) {
        m_nvmeRows[i].name = new QLabel("—");
        m_nvmeRows[i].bar = new TempBar(80);
        m_nvmeRows[i].val = new QLabel("—");
        m_nvmeRows[i].val->setFixedWidth(130);
        m_nvmeRows[i].val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        gSys->addWidget(m_nvmeRows[i].name, i + 1, 0);
        gSys->addWidget(m_nvmeRows[i].bar, i + 1, 1);
        gSys->addWidget(m_nvmeRows[i].val, i + 1, 2);
    }
    outer->addWidget(m_grpSys);
    outer->addStretch();

    retranslateUi();
    refreshTheme();
}

void MonitorTab::updateSystemInfo(const PS5SystemInfo& info) {
    m_ps5Info = info;
    retranslateUi();
}

void MonitorTab::retranslateUi() {
    m_topBox->setTitle(tx("ℹ️  Система и оверлеи", "ℹ️  System & Overlays"));
    m_sysLbl->setText(
        QString("<b>%1</b>  •  %2: <code>%3</code>  •  %4: <code>%5</code>  •  VBIOS: <code>%6</code>")
            .arg(m_ps5Info.model, tx("Ядро", "Kernel"), m_ps5Info.kernel,
                 tx("Сессия", "Session"), m_ps5Info.sessionType, m_ps5Info.vbios));

    setOverlayChecked(m_overlayVisible);
    m_btnOverlay->setToolTip(m_isDaemonMode
        ? tx("Запускает/останавливает независимый микро-оверлей <code>ps5overlayd</code> (Wayland LayerOverlay, ~4 МБ RAM).",
             "Starts/stops standalone micro-overlay <code>ps5overlayd</code> (Wayland LayerOverlay, ~4 MB RAM).")
        : tx("Показывает нативный LayerOverlay поверх всех окон (включая полноэкранные).",
             "Shows native LayerOverlay above all windows (including fullscreen apps)."));
    m_btnOvCfg->setText(tx("⚙  Настроить оверлей", "⚙  Overlay Settings"));
    m_btnMango->setText(tx("🎮  Профиль MangoHud", "🎮  MangoHud Profile"));
    if (m_btnDetach) {
        m_btnDetach->setText(tx("🚀  Закрыть GUI (оставить оверлей)", "🚀  Exit GUI (Keep Overlay)"));
        m_btnDetach->setToolTip(tx("Запускает оверлей <code>ps5overlayd</code> и выгружает основное окно Qt6 из памяти.",
                                   "Starts <code>ps5overlayd</code> and exits the Qt6 GUI to free RAM."));
    }
    if (m_modeLbl) {
        m_modeLbl->setText(tx(
            QString("<span style=\"color:%1\">● Гибридный режим активен:</span> "
                    "оверлей работает в отдельном C-процессе <code>ps5overlayd</code> (Wayland + PangoCairo).").arg(m_theme.positive()),
            QString("<span style=\"color:%1\">● Hybrid mode active:</span> "
                    "overlay runs in a standalone C process <code>ps5overlayd</code> (Wayland + PangoCairo).").arg(m_theme.positive())));
    }

    m_grpPs5->setTitle(tx("🌡️  Датчики платы PS5 (ps5_fan)", "🌡️  PS5 Board Thermal Sensors (ps5_fan)"));
    m_grpFan->setTitle(tx("💨  Охлаждение", "💨  Cooling"));
    m_lblGpuLoadCap->setText(tx("Загрузка GPU:", "GPU load:"));
    m_grpSys->setTitle(tx("🖥️  CPU и NVMe SSD", "🖥️  CPU & NVMe SSD"));

    if (m_hasSensors) updateSensors(m_lastSensors);
}

void MonitorTab::refreshTheme() {
    m_lblFanMeta->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    for (int i = 0; i < 4; ++i) m_ps5Rows[i].bar->resetTheme();
    for (auto* b : {m_barFan, m_barGpuTemp, m_barGpuBusy, m_barVram, m_barCpu}) b->resetTheme();
    for (int i = 0; i < 2; ++i) m_nvmeRows[i].bar->resetTheme();
    if (m_modeLbl) {
        m_modeLbl->setText(tx(
            QString("<span style=\"color:%1\">● Гибридный режим активен:</span> "
                    "оверлей работает в отдельном C-процессе <code>ps5overlayd</code> (Wayland + PangoCairo).").arg(m_theme.positive()),
            QString("<span style=\"color:%1\">● Hybrid mode active:</span> "
                    "overlay runs in a standalone C process <code>ps5overlayd</code> (Wayland + PangoCairo).").arg(m_theme.positive())));
    }
    if (m_hasSensors) updateSensors(m_lastSensors);
}

void MonitorTab::setOverlayChecked(bool visible) {
    m_overlayVisible = visible;
    m_btnOverlay->setChecked(visible);
    m_btnOverlay->setText(visible
        ? tx("📊  Скрыть оверлей", "📊  Hide Overlay")
        : tx("📊  Показать оверлей", "📊  Show Overlay"));
}

void MonitorTab::updateSensors(const SensorSnapshot& data) {
    m_lastSensors = data;
    m_hasSensors = true;
    const QString naStr = tx("н/д", "N/A");
    const QString mhz = tx("МГц", "MHz");
    const QString mb = tx("МБ", "MB");

    for (int i = 0; i < 4; ++i) {
        const auto& t = data.fanTemps[i];
        m_ps5Rows[i].name->setText(t.label + ":");
        if (t.valid) {
            QString c = tempColor(t.tempC, m_theme);
            m_ps5Rows[i].val->setText(QString::number(t.tempC, 'f', 1) + " °C" + (t.fault ? " ⚠" : ""));
            m_ps5Rows[i].val->setStyleSheet(QString("color: %1; font-weight: bold;").arg(c));
            m_ps5Rows[i].bar->setColoredValue(qRound(t.tempC), c, 100);
        } else {
            m_ps5Rows[i].val->setText(naStr);
            m_ps5Rows[i].val->setStyleSheet("");
        }
    }

    QString modeStr = data.fanPwmEnable == 0 ? "Full/Off"
                    : data.fanPwmEnable == 1 ? tx("Ручной", "Manual")
                    : data.fanPwmEnable == 2 ? tx("Авто", "Auto") : "—";
    if (data.hasFanPwm) {
        m_lblFanPct->setText(QString::number(data.fanPwmPct) + "%");
        m_barFan->setColoredValue(data.fanPwmPct, levelColor(data.fanPwmPct / 100.0, m_theme), 100);
        m_lblFanMeta->setText(QString("pwm1: %1/255 (%2)").arg(data.fanPwm).arg(modeStr));
    } else {
        m_lblFanPct->setText(naStr);
        m_lblFanMeta->setText("pwm1: —");
    }

    if (data.hasGpuTemp) {
        QString c = tempColor(data.gpuTemp, m_theme, 70, 88);
        m_lblGpuTemp->setText(QString::number(data.gpuTemp, 'f', 1) + " °C");
        m_lblGpuTemp->setStyleSheet(QString("color: %1; font-weight: bold;").arg(c));
        m_barGpuTemp->setColoredValue(qRound(data.gpuTemp), c, 105);
    } else {
        m_lblGpuTemp->setText(naStr);
        m_lblGpuTemp->setStyleSheet("");
    }

    if (data.hasGpuBusy) {
        m_barGpuBusy->setFormat(QString::number(data.gpuBusy) + "%");
        m_barGpuBusy->setColoredValue(data.gpuBusy, levelColor(data.gpuBusy / 100.0, m_theme), 100);
    }

    if (data.hasVram && data.vramTotalMb > 0) {
        double ratio = double(data.vramUsedMb) / data.vramTotalMb;
        m_barVram->setFormat(QString("%1 / %2 %3 (%4%)").arg(data.vramUsedMb).arg(data.vramTotalMb).arg(mb).arg(qRound(ratio * 100)));
        m_barVram->setColoredValue(data.vramUsedMb, levelColor(ratio, m_theme), data.vramTotalMb);
    }

    QString freqS = data.hasGpuFreq ? QString("%1 %2").arg(data.gpuFreqMhz).arg(mhz) : naStr;
    QString gttS = data.hasGtt ? QString("%1/%2 %3").arg(data.gttUsedMb).arg(data.gttTotalMb).arg(mb) : "—";
    QString pmC = (data.gpuPm == "active") ? m_theme.positive() : m_theme.neutral();
    m_lblGpuMeta->setText(QString("%1: <b>%2</b>  |  GTT: <b>%3</b>  |  PM: <span style=\"color:%4\"><b>%5</b></span> (%6)")
                              .arg(tx("Частота sclk", "sclk clock"), freqS, gttS, pmC, data.gpuPm, data.gpuPowerState));

    QString mhzS = data.hasCpuFreq ? QString(" (%1 %2)").arg(data.cpuFreqMhz).arg(mhz) : "";
    if (data.hasCpuTemp) {
        QString c = tempColor(data.cpuTemp, m_theme, 70, 88);
        m_lblCpu->setText(QString::number(data.cpuTemp, 'f', 1) + " °C" + mhzS);
        m_lblCpu->setStyleSheet(QString("color: %1; font-weight: bold;").arg(c));
        m_barCpu->setColoredValue(qRound(data.cpuTemp), c, 100);
    } else {
        m_lblCpu->setText(naStr + mhzS);
        m_lblCpu->setStyleSheet("");
    }

    for (int i = 0; i < 2; ++i) {
        const auto& t = data.nvmeTemps[i];
        m_nvmeRows[i].name->setText(t.label + ":");
        if (t.valid) {
            QString c = tempColor(t.tempC, m_theme, 58, 70);
            m_nvmeRows[i].val->setText(QString::number(t.tempC, 'f', 1) + " °C");
            m_nvmeRows[i].val->setStyleSheet(QString("color: %1; font-weight: bold;").arg(c));
            m_nvmeRows[i].bar->setColoredValue(qRound(t.tempC), c, 80);
        } else {
            m_nvmeRows[i].val->setText(naStr);
            m_nvmeRows[i].val->setStyleSheet("");
        }
    }
}

void MonitorTab::onMangohudConfig() {
    QString cfgDir = QDir::homePath() + "/.config/MangoHud";
    QString cfgFile = cfgDir + "/MangoHud.conf";

    QDialog dlg(this);
    dlg.setWindowTitle(tx("Генерация профиля MangoHud для PS5", "Generate MangoHud Profile for PS5"));
    dlg.resize(460, 380);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(tx(QString("Будет записан файл: <code>%1</code>").arg(cfgFile),
                               QString("File to be written: <code>%1</code>").arg(cfgFile))));
    auto* edit = new QTextEdit();
    edit->setPlainText(QString::fromUtf8(kMangoHudConf));
    v->addWidget(edit);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(btns);
    if (dlg.exec() != QDialog::Accepted) return;

    QDir().mkpath(cfgDir);
    if (QFile::exists(cfgFile)) {
        QFile::remove(cfgDir + "/MangoHud.conf.bak");
        QFile::copy(cfgFile, cfgDir + "/MangoHud.conf.bak");
    }
    QFile f(cfgFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tx("Ошибка", "Error"), f.errorString());
        return;
    }
    f.write(edit->toPlainText().toUtf8());
    m_log(tx("✓ Профиль MangoHud сохранён: ", "✓ MangoHud profile saved: ") + cfgFile);
    QMessageBox::information(this, tx("MangoHud готов", "MangoHud Ready"),
        tx(QString("Конфигурация сохранена в %1.\nДля игр: mangohud %%command%%.").arg(cfgFile),
           QString("Configuration saved to %1.\nFor games: mangohud %%command%%.").arg(cfgFile)));
}

// ═══════════════════════════════════════════════════════════════════════════
// LogTab
// ═══════════════════════════════════════════════════════════════════════════

LogTab::LogTab(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(6);

    auto* hdr = new QHBoxLayout();
    m_lblHdr = new QLabel();
    hdr->addWidget(m_lblHdr);
    hdr->addStretch();
    m_btnCopy = new QPushButton();
    connect(m_btnCopy, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_logView->toPlainText());
    });
    m_btnClear = new QPushButton();
    connect(m_btnClear, &QPushButton::clicked, this, &LogTab::clear);
    hdr->addWidget(m_btnCopy);
    hdr->addWidget(m_btnClear);
    layout->addLayout(hdr);

    m_logView = new QTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setFont(QFont("Monospace", 9));
    m_logView->setLineWrapMode(QTextEdit::NoWrap);
    layout->addWidget(m_logView);

    retranslateUi();
}

void LogTab::retranslateUi() {
    m_lblHdr->setText(tx("Журнал операций:", "Operations log:"));
    m_btnCopy->setText(tx("📋  Копировать", "📋  Copy"));
    m_btnClear->setText(tx("🗑️  Очистить", "🗑️  Clear"));
}

void LogTab::append(const QString& text) {
    m_logView->append(text);
}

void LogTab::clear() {
    m_logView->clear();
}

} // namespace ps5
