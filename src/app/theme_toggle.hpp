#pragma once
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <cmath>

inline QIcon themeToggleIcon(bool dark) {
    QPixmap bitmap(64, 64);
    bitmap.setDevicePixelRatio(2);
    bitmap.fill(Qt::transparent);
    QPainter painter(&bitmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto color = dark ? QColor("#f2c97d") : qApp->palette().color(QPalette::WindowText);
    if (dark) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(16, 16), 5.6, 5.6);
        painter.setPen(QPen(color, 2.0, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 8; ++i) {
            const auto angle = i * 3.141592653589793 / 4;
            const QPointF direction(std::cos(angle), std::sin(angle));
            painter.drawLine(QPointF(16, 16) + 9.2 * direction, QPointF(16, 16) + 12.2 * direction);
        }
    } else {
        QPainterPath outer, cut;
        outer.addEllipse(QRectF(6, 5, 21, 21));
        cut.addEllipse(QRectF(13.5, 1.5, 20, 20));
        painter.setBrush(color);
        painter.setPen(Qt::NoPen);
        painter.drawPath(outer.subtracted(cut));
    }
    return QIcon(bitmap);
}
