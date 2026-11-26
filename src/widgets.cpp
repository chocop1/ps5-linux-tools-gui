#include "widgets.h"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QSizePolicy>

namespace ps5 {

TempBar::TempBar(int maxVal, bool showText, QWidget* parent)
    : QProgressBar(parent) {
    setRange(0, maxVal);
    setValue(0);
    setTextVisible(showText);
    setFixedHeight(showText ? 16 : 12);
}

void TempBar::setColoredValue(int value, const QString& colorHex, int maxVal) {
    if (maxVal > 0) setMaximum(maxVal);
    setValue(qBound(0, value, maximum()));
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Highlight, QColor(colorHex));
    setPalette(pal);
}

void TempBar::resetTheme() {
    setPalette(QApplication::palette());
}

FanCurveChart::FanCurveChart(const KDETheme& theme, QWidget* parent)
    : QWidget(parent), m_theme(theme) {
    setMinimumHeight(175);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FanCurveChart::setData(const QVector<QPair<int, int>>& points, double currentTemp) {
    m_points = points;
    if (currentTemp >= 0.0) m_currentTemp = currentTemp;
    update();
}

void FanCurveChart::setCurrentTemp(double temp) {
    m_currentTemp = temp;
    update();
}

void FanCurveChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette pal = QApplication::palette();
    const QColor bg = pal.color(QPalette::Base);
    const QColor fg = pal.color(QPalette::Text);
    const QColor muted(m_theme.muted());
    const QColor border = pal.color(QPalette::Mid);
    const QColor accent(m_theme.accent());
    const QColor warnC(m_theme.neutral());

    const int ml = 42, mr = 16, mt = 14, mb = 26;
    const int pw = qMax(10, width() - ml - mr);
    const int ph = qMax(10, height() - mt - mb);

    p.setPen(QPen(border, 1));
    p.setBrush(bg);
    p.drawRoundedRect(ml, mt, pw, ph, 4, 4);

    constexpr double tMin = 30.0, tMax = 95.0;
    auto mapX = [&](double t) -> double {
        double r = qBound(0.0, (t - tMin) / (tMax - tMin), 1.0);
        return ml + r * pw;
    };
    auto mapY = [&](double pct) -> double {
        double r = qBound(0.0, pct / 100.0, 1.0);
        return mt + ph - r * ph;
    };

    QFont f = font();
    f.setPointSize(8);
    p.setFont(f);

    QPen gridPen(border, 1, Qt::DotLine);
    for (int pct : {0, 25, 50, 75, 100}) {
        double y = mapY(pct);
        p.setPen(gridPen);
        p.drawLine(QPointF(ml, y), QPointF(ml + pw, y));
        p.setPen(muted);
        p.drawText(2, int(y) - 7, ml - 6, 14, Qt::AlignRight | Qt::AlignVCenter, QString::number(pct) + "%");
    }

    for (int t : {40, 50, 60, 70, 80, 90}) {
        double x = mapX(t);
        p.setPen(gridPen);
        p.drawLine(QPointF(x, mt), QPointF(x, mt + ph));
        p.setPen(muted);
        p.drawText(int(x) - 18, mt + ph + 4, 36, 16, Qt::AlignCenter, QString::number(t) + "°");
    }

    if (m_currentTemp >= 0.0) {
        double cx = mapX(m_currentTemp);
        p.setPen(QPen(warnC, 1.5, Qt::DashLine));
        p.drawLine(QPointF(cx, mt + 2), QPointF(cx, mt + ph - 2));
        p.setPen(warnC);
        p.drawText(int(cx) - 24, mt + 2, 48, 14, Qt::AlignCenter, QString::number(m_currentTemp, 'f', 1) + "°");
    }

    if (!m_points.isEmpty()) {
        QVector<QPointF> pts;
        pts.reserve(m_points.size());
        for (const auto& pt : m_points)
            pts.append(QPointF(mapX(pt.first), mapY(qRound(pt.second / 255.0 * 100.0))));

        QPainterPath path;
        path.moveTo(pts.first());
        for (int i = 1; i < pts.size(); ++i) path.lineTo(pts[i]);

        QPainterPath fillPath(path);
        fillPath.lineTo(pts.last().x(), mt + ph);
        fillPath.lineTo(pts.first().x(), mt + ph);
        fillPath.closeSubpath();

        QColor fillC(accent);
        fillC.setAlpha(45);
        p.setPen(Qt::NoPen);
        p.setBrush(fillC);
        p.drawPath(fillPath);

        p.setPen(QPen(accent, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        p.setPen(QPen(fg, 1.5));
        p.setBrush(accent);
        for (const auto& pt : pts)
            p.drawEllipse(pt, 4.0, 4.0);
    } else {
        p.setPen(muted);
        p.drawText(ml, mt, pw, ph, Qt::AlignCenter, tx("Данные кривой недоступны", "Fan curve data unavailable"));
    }
}

} // namespace ps5
