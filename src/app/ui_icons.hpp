#pragma once
#include <QApplication>
#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <cmath>

enum class UiIcon {
    Sparkles,
    Generate,
    Apply,
    Inverse,
    Eraser,
    Clean,
    Random,
    Play,
    Stop,
    Clear,
    AddToMain,
    Globe,
    Zap,
    Sliders,
    Layers,
    Crosshair,
    MaskTop,
    ResetCamera,
    Copy,
    Check
};

inline QIcon uiIcon(UiIcon kind, const QColor &color = QColor(), int size = 16) {
    const int px = std::max(12, size);
    QPixmap pm(px * 2, px * 2);
    pm.setDevicePixelRatio(2.0);
    pm.fill(Qt::transparent);

    const QColor ink =
        color.isValid() ? color : (qApp ? qApp->palette().color(QPalette::WindowText) : QColor("#243649"));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 20.0, px / 20.0);
    p.setPen(QPen(ink, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case UiIcon::Sparkles: {
        // Four-point AI sparkle + small secondary star
        QPainterPath star;
        star.moveTo(9.5, 2.5);
        star.quadTo(9.5, 9.5, 16.5, 9.5);
        star.quadTo(9.5, 9.5, 9.5, 16.5);
        star.quadTo(9.5, 9.5, 2.5, 9.5);
        star.quadTo(9.5, 9.5, 9.5, 2.5);
        p.drawPath(star);
        p.setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(15.8, 13.8), QPointF(15.8, 17.6));
        p.drawLine(QPointF(13.9, 15.7), QPointF(17.7, 15.7));
        break;
    }
    case UiIcon::Generate: {
        // Cube wireframe + tiny spark
        QPainterPath hex;
        hex.moveTo(10, 2.5);
        hex.lineTo(16.5, 6.2);
        hex.lineTo(16.5, 13.8);
        hex.lineTo(10, 17.5);
        hex.lineTo(3.5, 13.8);
        hex.lineTo(3.5, 6.2);
        hex.closeSubpath();
        p.drawPath(hex);
        p.drawLine(QPointF(10, 10), QPointF(16.5, 6.2));
        p.drawLine(QPointF(10, 10), QPointF(3.5, 6.2));
        p.drawLine(QPointF(10, 10), QPointF(10, 17.5));
        break;
    }
    case UiIcon::Apply: {
        // Corner down-left return arrow
        QPainterPath path;
        path.moveTo(15.5, 4.5);
        path.lineTo(15.5, 11.5);
        path.lineTo(5.0, 11.5);
        p.drawPath(path);
        p.drawLine(QPointF(8.2, 8.3), QPointF(4.8, 11.5));
        p.drawLine(QPointF(8.2, 14.7), QPointF(4.8, 11.5));
        break;
    }
    case UiIcon::Inverse: {
        // Reverse / undo arc arrow
        QPainterPath arc;
        arc.moveTo(5.5, 8.5);
        arc.lineTo(13.0, 8.5);
        arc.arcTo(QRectF(9.5, 8.5, 7.0, 7.0), 90, -180);
        arc.lineTo(7.5, 15.5);
        p.drawPath(arc);
        p.drawLine(QPointF(8.3, 5.5), QPointF(5.0, 8.5));
        p.drawLine(QPointF(8.3, 11.5), QPointF(5.0, 8.5));
        break;
    }
    case UiIcon::Eraser: {
        // Dashed empty square
        p.setPen(QPen(ink, 1.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(QRectF(3.5, 3.5, 13, 13), 2.5, 2.5);
        break;
    }
    case UiIcon::Clean: {
        // Rounded square with check
        p.drawRoundedRect(QRectF(3.2, 3.2, 13.6, 13.6), 2.8, 2.8);
        QPainterPath tick;
        tick.moveTo(6.6, 10.2);
        tick.lineTo(9.1, 12.7);
        tick.lineTo(13.6, 7.5);
        p.drawPath(tick);
        break;
    }
    case UiIcon::Random: {
        // Shuffle crossed arrows
        p.drawLine(QPointF(3.5, 6.2), QPointF(7.2, 6.2));
        p.drawLine(QPointF(7.2, 6.2), QPointF(12.8, 13.8));
        p.drawLine(QPointF(12.8, 13.8), QPointF(16.2, 13.8));
        p.drawLine(QPointF(3.5, 13.8), QPointF(7.2, 13.8));
        p.drawLine(QPointF(7.2, 13.8), QPointF(12.8, 6.2));
        p.drawLine(QPointF(12.8, 6.2), QPointF(16.2, 6.2));
        p.drawLine(QPointF(14.0, 4.0), QPointF(16.4, 6.2));
        p.drawLine(QPointF(14.0, 8.4), QPointF(16.4, 6.2));
        p.drawLine(QPointF(14.0, 11.6), QPointF(16.4, 13.8));
        p.drawLine(QPointF(14.0, 16.0), QPointF(16.4, 13.8));
        break;
    }
    case UiIcon::Play: {
        QPainterPath tri;
        tri.moveTo(6.2, 4.2);
        tri.lineTo(15.6, 10.0);
        tri.lineTo(6.2, 15.8);
        tri.closeSubpath();
        p.setBrush(ink);
        p.drawPath(tri);
        break;
    }
    case UiIcon::Stop: {
        p.setBrush(ink);
        p.drawRoundedRect(QRectF(5.2, 5.2, 9.6, 9.6), 2.0, 2.0);
        break;
    }
    case UiIcon::Clear: {
        // Trash bin
        p.drawLine(QPointF(4.0, 5.8), QPointF(16.0, 5.8));
        p.drawLine(QPointF(7.8, 3.6), QPointF(12.2, 3.6));
        p.drawRoundedRect(QRectF(5.5, 5.8, 9.0, 10.8), 1.6, 1.6);
        p.drawLine(QPointF(8.4, 8.8), QPointF(8.4, 13.6));
        p.drawLine(QPointF(11.6, 8.8), QPointF(11.6, 13.6));
        break;
    }
    case UiIcon::AddToMain: {
        // Plus inside rounded square
        p.drawRoundedRect(QRectF(3.2, 3.2, 13.6, 13.6), 2.8, 2.8);
        p.drawLine(QPointF(10.0, 6.6), QPointF(10.0, 13.4));
        p.drawLine(QPointF(6.6, 10.0), QPointF(13.4, 10.0));
        break;
    }
    case UiIcon::Globe: {
        p.setPen(QPen(ink, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawEllipse(QRectF(3.0, 3.0, 14.0, 14.0));
        p.drawEllipse(QRectF(6.8, 3.0, 6.4, 14.0));
        p.drawLine(QPointF(3.4, 10.0), QPointF(16.6, 10.0));
        break;
    }
    case UiIcon::Zap: {
        QPainterPath bolt;
        bolt.moveTo(11.2, 2.5);
        bolt.lineTo(4.5, 11.0);
        bolt.lineTo(9.8, 11.0);
        bolt.lineTo(8.8, 17.5);
        bolt.lineTo(15.5, 9.0);
        bolt.lineTo(10.2, 9.0);
        bolt.closeSubpath();
        p.drawPath(bolt);
        break;
    }
    case UiIcon::Sliders: {
        p.drawLine(QPointF(3.5, 6.0), QPointF(16.5, 6.0));
        p.drawLine(QPointF(3.5, 10.0), QPointF(16.5, 10.0));
        p.drawLine(QPointF(3.5, 14.0), QPointF(16.5, 14.0));
        p.setBrush(ink);
        p.drawEllipse(QPointF(7.2, 6.0), 1.8, 1.8);
        p.drawEllipse(QPointF(13.2, 10.0), 1.8, 1.8);
        p.drawEllipse(QPointF(8.8, 14.0), 1.8, 1.8);
        break;
    }
    case UiIcon::Layers: {
        QPainterPath top;
        top.moveTo(10.0, 3.5);
        top.lineTo(16.5, 7.0);
        top.lineTo(10.0, 10.5);
        top.lineTo(3.5, 7.0);
        top.closeSubpath();
        p.drawPath(top);
        QPainterPath mid;
        mid.moveTo(3.5, 10.8);
        mid.lineTo(10.0, 14.2);
        mid.lineTo(16.5, 10.8);
        p.drawPath(mid);
        QPainterPath bot;
        bot.moveTo(3.5, 14.2);
        bot.lineTo(10.0, 17.4);
        bot.lineTo(16.5, 14.2);
        p.drawPath(bot);
        break;
    }
    case UiIcon::Crosshair: {
        p.drawEllipse(QRectF(4.2, 4.2, 11.6, 11.6));
        p.drawLine(QPointF(10.0, 2.2), QPointF(10.0, 6.2));
        p.drawLine(QPointF(10.0, 13.8), QPointF(10.0, 17.8));
        p.drawLine(QPointF(2.2, 10.0), QPointF(6.2, 10.0));
        p.drawLine(QPointF(13.8, 10.0), QPointF(17.8, 10.0));
        break;
    }
    case UiIcon::MaskTop: {
        // 3x3 top grid outline with top highlight
        p.drawRoundedRect(QRectF(3.5, 3.5, 13.0, 13.0), 2.2, 2.2);
        p.drawLine(QPointF(3.5, 8.2), QPointF(16.5, 8.2));
        p.drawLine(QPointF(8.0, 8.2), QPointF(8.0, 16.5));
        p.drawLine(QPointF(12.0, 8.2), QPointF(12.0, 16.5));
        break;
    }
    case UiIcon::ResetCamera: {
        QPainterPath arc;
        arc.arcMoveTo(QRectF(4.0, 4.0, 12.0, 12.0), 40);
        arc.arcTo(QRectF(4.0, 4.0, 12.0, 12.0), 40, 275);
        p.drawPath(arc);
        p.drawLine(QPointF(14.5, 3.4), QPointF(15.2, 6.8));
        p.drawLine(QPointF(11.8, 6.2), QPointF(15.2, 6.8));
        break;
    }
    case UiIcon::Copy: {
        // Minimalist overlapping double-sheet copy icon
        p.setPen(QPen(ink, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawRoundedRect(QRectF(7.2, 6.8, 9.0, 10.2), 2.0, 2.0);
        QPainterPath back;
        back.moveTo(12.6, 6.8);
        back.lineTo(12.6, 4.8);
        back.quadTo(12.6, 3.2, 11.0, 3.2);
        back.lineTo(5.2, 3.2);
        back.quadTo(3.6, 3.2, 3.6, 4.8);
        back.lineTo(3.6, 12.4);
        back.quadTo(3.6, 14.0, 5.2, 14.0);
        back.lineTo(7.2, 14.0);
        p.drawPath(back);
        break;
    }
    case UiIcon::Check: {
        p.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath tick;
        tick.moveTo(4.2, 10.5);
        tick.lineTo(8.4, 14.6);
        tick.lineTo(16.0, 5.8);
        p.drawPath(tick);
        break;
    }
    }
    return QIcon(pm);
}
