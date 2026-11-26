#pragma once

#include <QPair>
#include <QProgressBar>
#include <QVector>
#include <QWidget>

#include "config.h"

namespace ps5 {

inline QString levelColor(double ratio, const KDETheme& theme) {
    if (ratio < 0.60) return theme.positive();
    if (ratio < 0.80) return theme.neutral();
    return theme.negative();
}

inline QString tempColor(double t, const KDETheme& theme, double warn = 65.0, double hot = 82.0) {
    if (t < warn) return theme.positive();
    if (t < hot) return theme.neutral();
    return theme.negative();
}

class TempBar : public QProgressBar {
    Q_OBJECT
public:
    explicit TempBar(int maxVal = 100, bool showText = false, QWidget* parent = nullptr);
    void setColoredValue(int value, const QString& colorHex, int maxVal = -1);
    void resetTheme();
};

class FanCurveChart : public QWidget {
    Q_OBJECT
public:
    explicit FanCurveChart(const KDETheme& theme, QWidget* parent = nullptr);
    void setData(const QVector<QPair<int, int>>& points, double currentTemp = -1.0);
    void setCurrentTemp(double temp);

protected:
    void paintEvent(QPaintEvent* ev) override;

private:
    const KDETheme& m_theme;
    QVector<QPair<int, int>> m_points;
    double m_currentTemp = -1.0;
};

} // namespace ps5
