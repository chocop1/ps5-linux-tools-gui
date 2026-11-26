#include "tabs.h"

#include <QApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QSizePolicy>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

namespace ps5 {

SettingsTab::SettingsTab(const KDETheme& theme, bool minimizeToTray, QWidget* parent)
    : QScrollArea(parent), m_theme(theme) {
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* inner = new QWidget();
    auto* layout = new QVBoxLayout(inner);
    layout->setSpacing(12);
    layout->setContentsMargins(12, 12, 12, 12);
    setWidget(inner);

    // 1. Language
    m_grpLang = new QGroupBox();
    auto* langV = new QVBoxLayout(m_grpLang);
    auto* langRow = new QHBoxLayout();
    m_lblLangCap = new QLabel();
    m_lblLangCap->setMinimumWidth(145);
    m_cmbLang = new QComboBox();
    connect(m_cmbLang, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsTab::onControlsChanged);
    langRow->addWidget(m_lblLangCap);
    langRow->addWidget(m_cmbLang, 1);
    langV->addLayout(langRow);

    m_lblLangInfo = new QLabel();
    m_lblLangInfo->setWordWrap(true);
    langV->addWidget(m_lblLangInfo);
    layout->addWidget(m_grpLang);

    // 2. Theme & Colors
    m_grpTheme = new QGroupBox();
    auto* themeV = new QVBoxLayout(m_grpTheme);
    auto* gTheme = new QGridLayout();
    gTheme->setColumnMinimumWidth(0, 145);

    m_lblStyleCap = new QLabel();
    m_cmbStyle = new QComboBox();
    connect(m_cmbStyle, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsTab::onControlsChanged);

    m_lblSchemeCap = new QLabel();
    m_cmbScheme = new QComboBox();
    connect(m_cmbScheme, qOverload<int>(&QComboBox::currentIndexChanged), this, &SettingsTab::onControlsChanged);

    gTheme->addWidget(m_lblStyleCap, 0, 0);
    gTheme->addWidget(m_cmbStyle, 0, 1);
    gTheme->addWidget(m_lblSchemeCap, 1, 0);
    gTheme->addWidget(m_cmbScheme, 1, 1);
    themeV->addLayout(gTheme);

    m_lblPresetsCap = new QLabel();
    themeV->addWidget(m_lblPresetsCap);

    auto* presetsRow = new QHBoxLayout();
    struct Spec { const char* ru; const char* en; const char* st; const char* sc; };
    const Spec specs[] = {
        {"🖥  Как в системе", "🖥  Follow system", "system",  "system"},
        {"Breeze",            "Breeze",            "Breeze",  "system"},
        {"Oxygen",            "Oxygen",            "Oxygen",  "auto"},
        {"Union",             "Union",             "Union",   "system"},
        {"Fusion",            "Fusion",            "Fusion",  "builtin:fusion-dark"},
        {"🪟 Windows 9x",     "🪟 Windows 9x",     "Windows", "builtin:win9x"},
    };
    for (const auto& sp : specs) {
        auto* b = new QPushButton();
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QString stMode = QString::fromLatin1(sp.st);
        QString scMode = QString::fromLatin1(sp.sc);
        connect(b, &QPushButton::clicked, this, [this, stMode, scMode]() { applyPreset(stMode, scMode); });
        presetsRow->addWidget(b);
        m_presetBtns.append({b, sp.ru, sp.en, stMode, scMode});
    }
    themeV->addLayout(presetsRow);

    m_lblPalettePreview = new QLabel();
    m_lblPalettePreview->setWordWrap(true);
    themeV->addWidget(m_lblPalettePreview);

    m_lblThemeNote = new QLabel();
    m_lblThemeNote->setWordWrap(true);
    themeV->addWidget(m_lblThemeNote);
    layout->addWidget(m_grpTheme);

    // 3. Behavior & Reset
    m_grpBeh = new QGroupBox();
    auto* behV = new QVBoxLayout(m_grpBeh);
    m_chkMinTray = new QCheckBox();
    m_chkMinTray->setChecked(minimizeToTray);
    connect(m_chkMinTray, &QCheckBox::toggled, this, &SettingsTab::minimizeToTrayToggled);
    behV->addWidget(m_chkMinTray);

    auto* resetRow = new QHBoxLayout();
    m_btnReset = new QPushButton();
    connect(m_btnReset, &QPushButton::clicked, this, &SettingsTab::onResetDefaults);
    resetRow->addWidget(m_btnReset);
    resetRow->addStretch();
    behV->addLayout(resetRow);
    layout->addWidget(m_grpBeh);

    layout->addStretch();

    populateCombos();
    retranslateUi();
    refreshTheme();
}

void SettingsTab::setMinimizeToTray(bool checked) {
    m_chkMinTray->blockSignals(true);
    m_chkMinTray->setChecked(checked);
    m_chkMinTray->blockSignals(false);
}

void SettingsTab::populateCombos() {
    m_updating = true;
    const QString langMode = getLanguageMode();
    const auto themeModes = getThemeSettings();
    const QString sysLang = detectSystemLanguage(nullptr);
    const QString sysLangDisp = (sysLang == "ru") ? "Русский" : "English";

    m_cmbLang->clear();
    m_cmbLang->addItem(
        tx(QString("Как в системе (%1)").arg(sysLangDisp), QString("Follow system (%1)").arg(sysLangDisp)),
        "system");
    m_cmbLang->addItem("English", "en");
    m_cmbLang->addItem("Русский", "ru");
    int idx = m_cmbLang->findData(langMode);
    m_cmbLang->setCurrentIndex(idx >= 0 ? idx : 0);

    m_cmbStyle->clear();
    for (const auto& sc : availableQtStyles()) {
        QString disp = sc.available ? sc.label : QString("%1 (%2)").arg(sc.label, tx("не установлен", "not installed"));
        m_cmbStyle->addItem(disp, sc.key);
        if (!sc.available) {
            if (auto* mdl = qobject_cast<QStandardItemModel*>(m_cmbStyle->model())) {
                if (auto* item = mdl->item(m_cmbStyle->count() - 1)) item->setEnabled(false);
            }
        }
    }
    idx = m_cmbStyle->findData(themeModes.first);
    m_cmbStyle->setCurrentIndex(idx >= 0 ? idx : 0);

    m_cmbScheme->clear();
    for (const auto& cs : discoverColorSchemes()) {
        m_cmbScheme->addItem(cs.label, cs.id);
    }
    idx = m_cmbScheme->findData(themeModes.second);
    m_cmbScheme->setCurrentIndex(idx >= 0 ? idx : 0);
    m_updating = false;
}

void SettingsTab::onControlsChanged() {
    if (m_updating) return;
    emit appearanceChanged(
        m_cmbLang->currentData().toString(),
        m_cmbStyle->currentData().toString(),
        m_cmbScheme->currentData().toString());
}

void SettingsTab::applyPreset(const QString& styleMode, const QString& schemeModeIn) {
    QString schemeMode = schemeModeIn;
    const bool isDark = QApplication::palette().color(QPalette::Window).lightness() < 128;
    if (styleMode == "Oxygen" && schemeMode == "auto") {
        QString ox = findColorSchemeByStem(isDark ? "OxygenDark" : "Oxygen");
        if (!ox.isEmpty()) schemeMode = ox;
    } else if (styleMode == "Breeze" && schemeMode == "auto") {
        QString br = findColorSchemeByStem(isDark ? "BreezeDark" : "BreezeLight");
        if (!br.isEmpty()) schemeMode = br;
    }

    const QString langMode = m_cmbLang->currentData().toString();
    m_updating = true;
    int stIdx = m_cmbStyle->findData(styleMode);
    if (stIdx >= 0) m_cmbStyle->setCurrentIndex(stIdx);
    int scIdx = m_cmbScheme->findData(schemeMode);
    if (scIdx >= 0) m_cmbScheme->setCurrentIndex(scIdx);
    m_updating = false;
    emit appearanceChanged(langMode, styleMode, schemeMode);
}

void SettingsTab::onResetDefaults() {
    m_updating = true;
    m_cmbLang->setCurrentIndex(0);
    m_cmbStyle->setCurrentIndex(0);
    m_cmbScheme->setCurrentIndex(0);
    m_updating = false;
    emit appearanceChanged("system", "system", "system");
}

void SettingsTab::retranslateUi() {
    populateCombos();

    m_grpLang->setTitle(tx("🌐  Язык интерфейса", "🌐  Interface Language"));
    m_lblLangCap->setText(tx("Язык приложения:", "Language:"));

    QString sysSrc;
    const QString sysLang = detectSystemLanguage(&sysSrc);
    const QString effLang = resolveEffectiveLanguage();
    const QString sysName = (sysLang == "ru") ? "Русский" : "English";
    const QString effName = (effLang == "ru") ? "Русский" : "English";
    m_lblLangInfo->setText(tx(
        QString("Определённый язык системы: <b>%1</b> (источник: <code>%2</code>)  •  Активный язык: <b>%3</b>.<br>"
                "Правило: если в системе выбран русский — включается русский; для английского и любых других языков по умолчанию ставится английский.")
            .arg(sysName, sysSrc, effName),
        QString("Detected system language: <b>%1</b> (source: <code>%2</code>)  •  Active language: <b>%3</b>.<br>"
                "Rule: Russian system locale defaults to Russian; English and all other locales default to English.")
            .arg(sysName, sysSrc, effName)));

    m_grpTheme->setTitle(tx("🎨  Тема оформления и цвета", "🎨  Theme & Color Palette"));
    m_lblStyleCap->setText(tx("Стиль виджетов (тема):", "Widget style (theme):"));
    m_lblSchemeCap->setText(tx("Цветовая палитра:", "Color palette:"));
    m_lblPresetsCap->setText(tx("Быстрые профили оформления:", "Quick theme presets:"));

    for (const auto& pb : m_presetBtns) {
        pb.btn->setText(tx(pb.ru, pb.en));
    }

    const QString sysSt = detectSystemStyleName();
    m_lblThemeNote->setText(tx(
        QString("Системный стиль KDE: <code>%1</code>. Поддерживаются KDE Breeze, Oxygen, Union, Fusion и классический Microsoft Windows 9x.").arg(sysSt),
        QString("Detected KDE system style: <code>%1</code>. Supports KDE Breeze, Oxygen, Union, Fusion, and classic Microsoft Windows 9x.").arg(sysSt)));

    m_grpBeh->setTitle(tx("⚙️  Поведение и сброс", "⚙️  Behavior & Reset"));
    m_chkMinTray->setText(tx("Сворачивать приложение в системный трей при закрытии окна",
                             "Minimize application to system tray when closing window"));
    m_btnReset->setText(tx("↺  Сбросить тему и язык на «Как в системе»",
                           "↺  Reset theme & language to \"Follow system\""));

    refreshTheme();
}

void SettingsTab::refreshTheme() {
    m_lblLangInfo->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));
    m_lblThemeNote->setStyleSheet(QString("color: %1;").arg(m_theme.muted()));

    const QPalette pal = QApplication::palette();
    const QString winC = pal.color(QPalette::Window).name();
    const QString baseC = pal.color(QPalette::Base).name();
    auto chip = [](const QString& label, const QString& color) {
        return QString("<span style=\"color:%1; font-weight:bold;\">■ %2</span>").arg(color, label);
    };

    m_lblPalettePreview->setText(
        tx("Палитра: ", "Active palette: ")
        + QString("<code>Window %1</code>  •  <code>Base %2</code>  •  ").arg(winC, baseC)
        + QStringList{
            chip("Accent", m_theme.accent()),
            chip(tx("Норма", "OK"), m_theme.positive()),
            chip(tx("Внимание", "Warn"), m_theme.neutral()),
            chip(tx("Критично", "Hot"), m_theme.negative()),
        }.join("  "));
}

} // namespace ps5
