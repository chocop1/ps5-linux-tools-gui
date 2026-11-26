#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPair>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>
#include <functional>

#include "config.h"
#include "sysinfo.h"
#include "widgets.h"
#include "workers.h"

namespace ps5 {

using ActionCallback = std::function<void(const QString& action, const QStringList& args, const QString& label, std::function<void()> onDone)>;
using SimpleCallback = std::function<void()>;
using LogCallback = std::function<void(const QString&)>;

class ControlTab : public QScrollArea {
    Q_OBJECT
public:
    ControlTab(const KDETheme& theme, ActionCallback runAction, QWidget* parent = nullptr);
    QVector<QWidget*> actionWidgets() const { return m_actionWidgets; }
    void updateServiceStatus(const ServiceStatusMap& statuses);
    void updateLiveSensors(const SensorSnapshot& data);
    void loadFanCurve();
    void retranslateUi();
    void refreshTheme();

private slots:
    void onLoadPreset();
    void refreshChartFromInputs();
    void onApplyManualPwm();
    void onApplyCurve();
    void onPersistCurve();

private:
    void updateCurveGroupTitle();
    void setSrvLabel(QLabel* lbl, const QString& name, const ServiceStatusInfo& inf);
    void confirmAutostart(const QString& unit, int state, QCheckBox* chk);
    QStringList collectCurveArgs();

    const KDETheme& m_theme;
    ActionCallback m_runAction;
    QVector<QWidget*> m_actionWidgets;
    ServiceStatusMap m_lastStatuses;
    SensorSnapshot m_lastSensors;
    bool m_hasStatuses = false;
    bool m_hasSensors = false;
    bool m_curveAvailable = true;

    QGroupBox* m_grpFan = nullptr;
    QPushButton* m_btnFanOn = nullptr;
    QPushButton* m_btnFanOff = nullptr;
    QPushButton* m_btnFanMax = nullptr;
    QLabel* m_lblFanStatus = nullptr;
    QLabel* m_lblFanLive = nullptr;
    QCheckBox* m_chkFanAutostart = nullptr;

    QGroupBox* m_grpBoost = nullptr;
    QPushButton* m_btnBoostOn = nullptr;
    QPushButton* m_btnBoostOff = nullptr;
    QLabel* m_lblBoostStatus = nullptr;
    QLabel* m_lblBoostClocks = nullptr;
    QCheckBox* m_chkBoostAutostart = nullptr;

    QGroupBox* m_grpCurve = nullptr;
    QLabel* m_lblManualPwm = nullptr;
    QSlider* m_sldPwm = nullptr;
    QLabel* m_lblPwmVal = nullptr;
    QPushButton* m_btnPwmApply = nullptr;
    QPushButton* m_btnPwmAuto = nullptr;
    FanCurveChart* m_chart = nullptr;
    QLabel* m_lblPreset = nullptr;
    QComboBox* m_cmbPreset = nullptr;
    QPushButton* m_btnLoadPreset = nullptr;
    QLabel* m_lblHyst = nullptr;
    QSpinBox* m_spnHyst = nullptr;
    QVector<QLabel*> m_ptLabels;
    QVector<QPair<QSpinBox*, QSpinBox*>> m_ptSpins;
    QPushButton* m_btnCurveApply = nullptr;
    QPushButton* m_btnCurveReset = nullptr;
    QPushButton* m_btnCurveReload = nullptr;
    QPushButton* m_btnCurvePersist = nullptr;
};

class MonitorTab : public QScrollArea {
    Q_OBJECT
public:
    MonitorTab(const KDETheme& theme,
               const PS5SystemInfo& ps5Info,
               std::function<void(bool)> toggleOverlay,
               SimpleCallback openOverlayCfg,
               SimpleCallback detachOverlay,
               bool isDaemonMode,
               LogCallback logCb,
               QWidget* parent = nullptr);

    void setOverlayChecked(bool visible);
    void updateSensors(const SensorSnapshot& data);
    void updateSystemInfo(const PS5SystemInfo& info);
    void retranslateUi();
    void refreshTheme();
    QPushButton* overlayButton() const { return m_btnOverlay; }

private slots:
    void onMangohudConfig();

private:
    const KDETheme& m_theme;
    PS5SystemInfo m_ps5Info;
    bool m_isDaemonMode = false;
    bool m_overlayVisible = false;
    SensorSnapshot m_lastSensors;
    bool m_hasSensors = false;
    LogCallback m_log;

    QGroupBox* m_topBox = nullptr;
    QLabel* m_sysLbl = nullptr;
    QPushButton* m_btnOverlay = nullptr;
    QPushButton* m_btnOvCfg = nullptr;
    QPushButton* m_btnDetach = nullptr;
    QPushButton* m_btnMango = nullptr;
    QLabel* m_modeLbl = nullptr;

    struct TempBarRow {
        QLabel* name = nullptr;
        QLabel* val = nullptr;
        TempBar* bar = nullptr;
    };
    QGroupBox* m_grpPs5 = nullptr;
    TempBarRow m_ps5Rows[4];

    QGroupBox* m_grpFan = nullptr;
    QLabel* m_lblFanPct = nullptr;
    TempBar* m_barFan = nullptr;
    QLabel* m_lblFanMeta = nullptr;

    QGroupBox* m_grpGpu = nullptr;
    TempBar* m_barGpuTemp = nullptr;
    QLabel* m_lblGpuTemp = nullptr;
    QLabel* m_lblGpuLoadCap = nullptr;
    TempBar* m_barGpuBusy = nullptr;
    TempBar* m_barVram = nullptr;
    QLabel* m_lblGpuMeta = nullptr;

    QGroupBox* m_grpSys = nullptr;
    TempBar* m_barCpu = nullptr;
    QLabel* m_lblCpu = nullptr;
    TempBarRow m_nvmeRows[2];
};

class DisplayRevertDialog : public QMessageBox {
    Q_OBJECT
public:
    explicit DisplayRevertDialog(const QString& summary, QWidget* parent = nullptr);
    void done(int result) override;

private slots:
    void tick();

private:
    void updateText();
    QString m_summary;
    int m_seconds = 15;
    QTimer* m_timer = nullptr;
};

class DisplayTab : public QScrollArea {
    Q_OBJECT
public:
    DisplayTab(const KDETheme& theme, SimpleCallback refreshCb, LogCallback logCb, QWidget* parent = nullptr);
    void updateDisplayData(const DisplayAndPeripheralsInfo& data);
    void retranslateUi();
    void refreshTheme();

private slots:
    void onOutputSelected();
    void onApplyDisplay();

private:
    struct ConnectorWidgets {
        QGroupBox* grp = nullptr;
        QLabel* capStatus = nullptr;
        QLabel* capCur = nullptr;
        QLabel* capModes = nullptr;
        QLabel* status = nullptr;
        QLabel* dpms = nullptr;
        QLabel* current = nullptr;
        QLabel* vh = nullptr;
        QLabel* modes = nullptr;
    };
    ConnectorWidgets ensureConnectorCard(const QString& drmName);

    const KDETheme& m_theme;
    SimpleCallback m_refreshCb;
    LogCallback m_log;

    DisplayAndPeripheralsInfo m_lastData;
    bool m_hasData = false;
    QGroupBox* m_grpDrm = nullptr;
    QVBoxLayout* m_drmLayout = nullptr;
    QLabel* m_lblDrmEmpty = nullptr;
    QMap<QString, ConnectorWidgets> m_connectorWidgets;
    QMap<QString, DisplayConnectorInfo> m_lastConnectors;
    QString m_knownSignature;

    QGroupBox* m_grpCtrl = nullptr;
    QLabel* m_lblOutCap = nullptr;
    QLabel* m_lblModeCap = nullptr;
    QLabel* m_lblScaleCap = nullptr;
    QLabel* m_lblBrightCap = nullptr;
    QComboBox* m_cmbOutput = nullptr;
    QComboBox* m_cmbMode = nullptr;
    QComboBox* m_cmbScale = nullptr;
    QSlider* m_sldBright = nullptr;
    QLabel* m_lblBright = nullptr;
    QComboBox* m_cmbVrr = nullptr;
    QComboBox* m_cmbHdr = nullptr;
    QPushButton* m_btnApplyDisp = nullptr;
    QPushButton* m_btnRefresh = nullptr;

    QGroupBox* m_grpPcie = nullptr;
    QLabel* m_lblPcieCap = nullptr;
    QLabel* m_lblPcie = nullptr;
    QLabel* m_lblHdcp = nullptr;
    QLabel* m_lblVbios = nullptr;
    QLabel* m_lblReset = nullptr;
    QLabel* m_lblFw = nullptr;
    QLabel* m_lblBtInfo = nullptr;
    QLabel* m_lblWifiInfo = nullptr;
};

class M2Tab : public QScrollArea {
    Q_OBJECT
public:
    M2Tab(const KDETheme& theme, ActionCallback runAction, SimpleCallback refreshM2, LogCallback logCb, QWidget* parent = nullptr);
    QVector<QWidget*> actionWidgets() const { return m_actionWidgets; }
    void updateM2Data(const M2EnvironmentInfo& data);
    void reapplyRootGuard();
    void retranslateUi();
    void refreshTheme();

private slots:
    void onM2Init();
    void onChooseImg();
    void onMount();
    void onOpenMnt();
    void onInstall();
    void onKexec();
    void onWarmReboot();

private:
    const KDETheme& m_theme;
    ActionCallback m_runAction;
    SimpleCallback m_refreshM2;
    LogCallback m_log;
    QVector<QWidget*> m_actionWidgets;

    QString m_imgPath;
    M2EnvironmentInfo m_m2State;
    bool m_hasState = false;

    QGroupBox* m_grpInfo = nullptr;
    QTableWidget* m_tblDisks = nullptr;
    QLabel* m_lblEfiStatus = nullptr;
    QPushButton* m_btnRefM2 = nullptr;
    QLabel* m_lblRootGuard = nullptr;

    QGroupBox* m_grpInit = nullptr;
    QLabel* m_warnInit = nullptr;
    QPushButton* m_btnM2Init = nullptr;

    QGroupBox* m_grpImg = nullptr;
    QPushButton* m_btnChooseImg = nullptr;
    QLabel* m_lblImgPath = nullptr;
    QLabel* m_lblMountState = nullptr;
    QPushButton* m_btnMount = nullptr;
    QPushButton* m_btnUnmount = nullptr;
    QPushButton* m_btnOpenMnt = nullptr;
    QPushButton* m_btnInstall = nullptr;

    QGroupBox* m_grpBoot = nullptr;
    QLabel* m_warnBoot = nullptr;
    QPushButton* m_btnKexec = nullptr;
    QPushButton* m_btnWarmReboot = nullptr;
};

class ServicesTab : public QWidget {
    Q_OBJECT
public:
    ServicesTab(const KDETheme& theme,
                const PS5SystemInfo& ps5Info,
                ActionCallback runAction,
                std::function<void(SimpleCallback)> installIntegration,
                SimpleCallback refreshStatuses,
                QWidget* parent = nullptr);

    QVector<QWidget*> actionWidgets() const { return m_actionWidgets; }
    void refreshIntegrationStatus();
    void updateStatuses(const ServiceStatusMap& statuses);
    void updateSystemInfo(const PS5SystemInfo& info);
    void refreshJournal();
    void waitPollers();
    void retranslateUi();
    void refreshTheme();

private slots:
    void onViewUnitLog();
    void onCopyReport();
    void onInstallIntegration();
    void onJournalReady(const QString& title, const QString& text);

private:
    QString selectedUnit() const;
    void actSelected(const QString& op);
    void startJournalPoller(const QString& mode, const QString& unit = "", const QString& filter = "");

    const KDETheme& m_theme;
    PS5SystemInfo m_ps5Info;
    ActionCallback m_runAction;
    std::function<void(SimpleCallback)> m_installIntegration;
    SimpleCallback m_refreshStatuses;
    QVector<QWidget*> m_actionWidgets;
    QVector<JournalPoller*> m_pollers;
    ServiceStatusMap m_lastStatuses;
    bool m_hasStatuses = false;

    QGroupBox* m_grpPk = nullptr;
    QLabel* m_lblPolkit = nullptr;
    QPushButton* m_btnInstallPolkit = nullptr;

    QGroupBox* m_grpSrv = nullptr;
    QTableWidget* m_tblServices = nullptr;
    QPushButton* m_btnRestart = nullptr;
    QPushButton* m_btnStop = nullptr;
    QPushButton* m_btnEnable = nullptr;
    QPushButton* m_btnDisable = nullptr;
    QPushButton* m_btnLog = nullptr;
    QPushButton* m_btnRef = nullptr;

    QGroupBox* m_grpDiag = nullptr;
    QLabel* m_lblFlt = nullptr;
    QComboBox* m_cmbFilter = nullptr;
    QPushButton* m_btnDiagRef = nullptr;
    QPushButton* m_btnCopyRep = nullptr;
    QTextEdit* m_txtJournal = nullptr;
};

class SettingsTab : public QScrollArea {
    Q_OBJECT
public:
    SettingsTab(const KDETheme& theme, bool minimizeToTray, QWidget* parent = nullptr);
    void setMinimizeToTray(bool checked);
    void retranslateUi();
    void refreshTheme();

signals:
    void appearanceChanged(const QString& langMode, const QString& styleMode, const QString& schemeMode);
    void minimizeToTrayToggled(bool checked);

private slots:
    void onControlsChanged();
    void onResetDefaults();

private:
    void populateCombos();
    void applyPreset(const QString& styleMode, const QString& schemeMode);

    const KDETheme& m_theme;
    bool m_updating = false;

    QGroupBox* m_grpLang = nullptr;
    QLabel* m_lblLangCap = nullptr;
    QComboBox* m_cmbLang = nullptr;
    QLabel* m_lblLangInfo = nullptr;

    QGroupBox* m_grpTheme = nullptr;
    QLabel* m_lblStyleCap = nullptr;
    QComboBox* m_cmbStyle = nullptr;
    QLabel* m_lblSchemeCap = nullptr;
    QComboBox* m_cmbScheme = nullptr;
    QLabel* m_lblPresetsCap = nullptr;
    struct PresetBtn {
        QPushButton* btn = nullptr;
        const char* ru = nullptr;
        const char* en = nullptr;
        QString styleMode;
        QString schemeMode;
    };
    QVector<PresetBtn> m_presetBtns;
    QLabel* m_lblPalettePreview = nullptr;
    QLabel* m_lblThemeNote = nullptr;

    QGroupBox* m_grpBeh = nullptr;
    QCheckBox* m_chkMinTray = nullptr;
    QPushButton* m_btnReset = nullptr;
};

class LogTab : public QWidget {
    Q_OBJECT
public:
    explicit LogTab(QWidget* parent = nullptr);
    void append(const QString& text);
    void clear();
    void retranslateUi();
    void refreshTheme() {}

private:
    QLabel* m_lblHdr = nullptr;
    QPushButton* m_btnCopy = nullptr;
    QPushButton* m_btnClear = nullptr;
    QTextEdit* m_logView = nullptr;
};

} // namespace ps5
