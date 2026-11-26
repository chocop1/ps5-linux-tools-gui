#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QLabel>
#include <QMap>
#include <QPoint>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QWidget>

#include "config.h"
#include "sysinfo.h"

namespace LayerShellQt {
class Window;
}

namespace ps5 {

class OverlaySettingsDialog : public QDialog {
    Q_OBJECT
public:
    OverlaySettingsDialog(const QMap<QString, bool>& visibleKeys, int opacityPct, QWidget* parent = nullptr);
    QMap<QString, bool> visibleKeys() const;
    int opacityPct() const;

private:
    QSlider* m_slider = nullptr;
    QLabel* m_lblOp = nullptr;
    QMap<QString, QCheckBox*> m_checks;
};

class MonitorOverlay : public QWidget {
    Q_OBJECT
public:
    explicit MonitorOverlay(const KDETheme& theme, QWidget* settingsParent = nullptr);

    bool isLayerShellActive() const { return m_layerWin != nullptr; }

public slots:
    void showOverlay();
    void openSettings();
    void updateSensors(const ps5::SensorSnapshot& data, bool force = false);
    void retranslateUi();
    void refreshAppearance();

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private:
    struct MetricRow {
        QLabel* key = nullptr;
        QLabel* val = nullptr;
    };

    void applyStyles();
    void applyVisibility();
    void syncGeometry();
    void clampPosition(const QSize& sz);
    void restorePosition();
    void setVal(const QString& key, const QString& text, const QString& color = "#efeee1");
    QString valTempColor(double val, double warn = 70.0, double hot = 85.0) const;

    const KDETheme& m_theme;
    QWidget* m_settingsParent = nullptr;
    QSettings m_settings;
    LayerShellQt::Window* m_layerWin = nullptr;

    QWidget* m_card = nullptr;
    QLabel* m_titleLbl = nullptr;
    QPushButton* m_btnCfg = nullptr;
    QPushButton* m_btnClose = nullptr;
    QLabel* m_hintLbl = nullptr;
    QMap<QString, MetricRow> m_rows;
    QMap<QString, bool> m_visibleMetrics;
    int m_opacityPct = 85;
    int m_posX = 20;
    int m_posY = 40;
    bool m_dragging = false;
    QPoint m_dragOffset;
    bool m_hasLastData = false;
    SensorSnapshot m_lastData;
};

class HybridOverlayManager : public QObject {
    Q_OBJECT
public:
    explicit HybridOverlayManager(const KDETheme& theme, QWidget* settingsParent = nullptr);

    bool isDaemonMode() const { return m_useDaemon; }
    bool isVisible() const;
    int daemonPid() const;

public slots:
    void showOverlay();
    void hide();
    void openSettings();
    void updateSensors(const ps5::SensorSnapshot& data);
    void refreshAppearance();
    void shutdown(bool keepDaemon = true);

signals:
    void visibilityChanged(bool visible);

private slots:
    void checkDaemonState();

private:
    void emitVisibility(bool vis);
    void readConf(QMap<QString, bool>& vis, int& opacity, QMap<QString, QString>& extra) const;
    void writeConf(const QMap<QString, bool>& vis, int opacity, const QString& accent = QString()) const;
    static QString pidFilePath();

    const KDETheme& m_theme;
    QWidget* m_settingsParent = nullptr;
    QString m_confPath;
    QString m_daemonBin;
    bool m_useDaemon = false;
    bool m_lastVisible = false;
    QTimer* m_watchTimer = nullptr;
    MonitorOverlay* m_widgetOverlay = nullptr;
};

} // namespace ps5
