#include "app_theme.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QString>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>
#include <algorithm>
#include <cmath>

namespace {
class CubeThemeStyle final : public QProxyStyle {
  public:
    CubeThemeStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {
        setObjectName("cubeThemeFusion");
    }

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override {
        if (!option || !painter) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        const bool dark = qApp && qApp->property("cubeDarkMode").toBool();
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool hovered = enabled && option->state.testFlag(State_MouseOver);
        const bool sunken = enabled && option->state.testFlag(State_Sunken);

        if (element == PE_IndicatorCheckBox) {
            const bool checked = option->state.testFlag(State_On);
            const bool partial = option->state.testFlag(State_NoChange);
            const QRectF r = QRectF(option->rect).adjusted(0.75, 0.75, -0.75, -0.75);
            const qreal radius = std::clamp(r.height() * 0.22, 2.0, 4.5);

            const QColor accent(dark ? "#3a7ab2" : "#2b73ad");
            const QColor accentHover(dark ? "#4b8cc6" : "#226094");
            const QColor fillUnchecked(dark ? (hovered ? "#243548" : "#1d2b3a")
                                            : (hovered ? "#eef6fd" : "#ffffff"));
            const QColor borderUnchecked(dark ? (hovered ? "#79b5ec" : "#566c84")
                                              : (hovered ? "#286ea8" : "#9db0c4"));
            const QColor fillDisabled(dark ? "#182330" : "#edf1f5");
            const QColor borderDisabled(dark ? "#2c3b4c" : "#cad4de");

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            if (checked || partial) {
                const QColor bg = !enabled ? (dark ? "#2e4154" : "#9cb3c9")
                                           : (sunken || hovered ? accentHover : accent);
                painter->setPen(QPen(bg, 1.0));
                painter->setBrush(bg);
                painter->drawRoundedRect(r, radius, radius);

                const QColor markColor = enabled ? QColor("#ffffff")
                                                 : (dark ? QColor("#9cb0c4") : QColor("#eef3f8"));
                const qreal stroke = std::clamp(r.width() * 0.145, 1.5, 3.0);
                painter->setPen(QPen(markColor, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->setBrush(Qt::NoBrush);
                if (partial) {
                    const qreal y = r.center().y();
                    painter->drawLine(QPointF(r.left() + r.width() * 0.24, y),
                                      QPointF(r.right() - r.width() * 0.24, y));
                } else {
                    QPainterPath tick;
                    tick.moveTo(r.left() + r.width() * 0.21, r.top() + r.height() * 0.53);
                    tick.lineTo(r.left() + r.width() * 0.42, r.top() + r.height() * 0.74);
                    tick.lineTo(r.left() + r.width() * 0.79, r.top() + r.height() * 0.29);
                    painter->drawPath(tick);
                }
            } else {
                painter->setPen(QPen(enabled ? borderUnchecked : borderDisabled, 1.1));
                painter->setBrush(enabled ? fillUnchecked : fillDisabled);
                painter->drawRoundedRect(r, radius, radius);
            }
            painter->restore();
            return;
        }

        if (element == PE_IndicatorRadioButton) {
            const bool checked = option->state.testFlag(State_On);
            const QRectF r = QRectF(option->rect).adjusted(0.75, 0.75, -0.75, -0.75);
            const QColor accent(dark ? "#3a7ab2" : "#2b73ad");
            const QColor borderUnchecked(dark ? (hovered ? "#79b5ec" : "#566c84")
                                              : (hovered ? "#286ea8" : "#9db0c4"));
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            if (checked) {
                const QColor bg = enabled ? accent : (dark ? "#2e4154" : "#9cb3c9");
                painter->setPen(QPen(bg, 1.1));
                painter->setBrush(bg);
                painter->drawEllipse(r);
                const qreal inset = r.width() * 0.29;
                painter->setPen(Qt::NoPen);
                painter->setBrush(enabled ? QColor("#ffffff")
                                          : (dark ? QColor("#9cb0c4") : QColor("#eef3f8")));
                painter->drawEllipse(r.adjusted(inset, inset, -inset, -inset));
            } else {
                painter->setPen(QPen(enabled ? borderUnchecked : (dark ? "#2c3b4c" : "#cad4de"), 1.1));
                painter->setBrush(enabled ? (dark ? "#1d2b3a" : "#ffffff")
                                          : (dark ? "#182330" : "#edf1f5"));
                painter->drawEllipse(r);
            }
            painter->restore();
            return;
        }

        if (element == PE_IndicatorSpinUp || element == PE_IndicatorSpinDown ||
            element == PE_IndicatorArrowDown || element == PE_IndicatorArrowUp ||
            element == PE_IndicatorArrowRight || element == PE_IndicatorArrowLeft) {
            const QRectF r(option->rect);
            const QPointF c = r.center();
            const qreal span = std::clamp(std::min(r.width(), r.height()) * 0.22, 2.2, 8.0);
            const qreal halfH = span * 0.54;
            const QColor color = !enabled ? QColor(dark ? "#586878" : "#9aa7b5")
                                 : hovered ? QColor(dark ? "#79b5ec" : "#286ea8")
                                           : QColor(dark ? "#b7c7d8" : "#4a5f75");
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(color, std::clamp(span * 0.44, 1.35, 2.2), Qt::SolidLine, Qt::RoundCap,
                                 Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            QPainterPath chevron;
            if (element == PE_IndicatorSpinUp || element == PE_IndicatorArrowUp) {
                chevron.moveTo(c.x() - span, c.y() + halfH * 0.85);
                chevron.lineTo(c.x(), c.y() - halfH * 0.85);
                chevron.lineTo(c.x() + span, c.y() + halfH * 0.85);
            } else if (element == PE_IndicatorSpinDown || element == PE_IndicatorArrowDown) {
                chevron.moveTo(c.x() - span, c.y() - halfH * 0.85);
                chevron.lineTo(c.x(), c.y() + halfH * 0.85);
                chevron.lineTo(c.x() + span, c.y() - halfH * 0.85);
            } else if (element == PE_IndicatorArrowRight) {
                chevron.moveTo(c.x() - halfH * 0.85, c.y() - span);
                chevron.lineTo(c.x() + halfH * 0.85, c.y());
                chevron.lineTo(c.x() - halfH * 0.85, c.y() + span);
            } else {
                chevron.moveTo(c.x() + halfH * 0.85, c.y() - span);
                chevron.lineTo(c.x() - halfH * 0.85, c.y());
                chevron.lineTo(c.x() + halfH * 0.85, c.y() + span);
            }
            painter->drawPath(chevron);
            painter->restore();
            return;
        }

        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter,
                            const QWidget *widget = nullptr) const override {
        if (!option || !painter) {
            QProxyStyle::drawComplexControl(control, option, painter, widget);
            return;
        }
        const bool dark = qApp && qApp->property("cubeDarkMode").toBool();
        const bool enabled = option->state.testFlag(State_Enabled);
        const bool focused = enabled && option->state.testFlag(State_HasFocus);
        const bool hovered = enabled && option->state.testFlag(State_MouseOver);

        const QColor panel(dark ? "#192533" : "#ffffff");
        const QColor disabledBg(dark ? "#1b2735" : "#eff3f6");
        const QColor line(dark ? "#3d5064" : "#bccad8");
        const QColor softLine(dark ? "#2c3c4d" : "#d6e0e8");
        const QColor accent(dark ? "#79b5ec" : "#286ea8");
        const QColor hoverBg(dark ? "#2c425b" : "#e3eff9");
        const QColor pressedBg(dark ? "#34536f" : "#dcebf6");
        const QColor arrowIdle(dark ? "#b7c7d8" : "#4a5f75");
        const QColor arrowDisabled(dark ? "#586878" : "#9aa7b5");

        auto drawChevron = [&](const QRectF &rect, bool up, bool activeHover, bool subEnabled) {
            const QPointF c = rect.center();
            const qreal span = std::clamp(std::min(rect.width(), rect.height()) * 0.24, 2.3, 4.8);
            const qreal halfH = span * 0.54;
            const QColor color = !subEnabled ? arrowDisabled : activeHover ? accent : arrowIdle;
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(color, std::clamp(span * 0.44, 1.3, 2.0), Qt::SolidLine, Qt::RoundCap,
                                 Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            QPainterPath path;
            if (up) {
                path.moveTo(c.x() - span, c.y() + halfH * 0.8);
                path.lineTo(c.x(), c.y() - halfH * 0.8);
                path.lineTo(c.x() + span, c.y() + halfH * 0.8);
            } else {
                path.moveTo(c.x() - span, c.y() - halfH * 0.8);
                path.lineTo(c.x(), c.y() + halfH * 0.8);
                path.lineTo(c.x() + span, c.y() - halfH * 0.8);
            }
            painter->drawPath(path);
            painter->restore();
        };

        if (control == CC_SpinBox) {
            if (const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option)) {
                const QRectF outer = QRectF(spin->rect).adjusted(0.5, 0.5, -0.5, -0.5);
                const qreal radius = std::clamp(outer.height() * 0.14, 3.0, 6.0);
                const QRectF upRect = subControlRect(CC_SpinBox, spin, SC_SpinBoxUp, widget);
                const QRectF downRect = subControlRect(CC_SpinBox, spin, SC_SpinBoxDown, widget);

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                QPainterPath clip;
                clip.addRoundedRect(outer, radius, radius);
                painter->fillPath(clip, enabled ? panel : disabledBg);

                painter->save();
                painter->setClipPath(clip);
                const bool upEnabled = enabled && (spin->stepEnabled & QAbstractSpinBox::StepUpEnabled);
                const bool downEnabled = enabled && (spin->stepEnabled & QAbstractSpinBox::StepDownEnabled);
                const bool upHover = upEnabled && hovered && (spin->activeSubControls & SC_SpinBoxUp);
                const bool downHover = downEnabled && hovered && (spin->activeSubControls & SC_SpinBoxDown);
                const bool upPressed =
                    upHover && option->state.testFlag(State_Sunken);
                const bool downPressed =
                    downHover && option->state.testFlag(State_Sunken);

                if (upRect.isValid() && (upHover || upPressed))
                    painter->fillRect(upRect.adjusted(0, -1, 1, 0), upPressed ? pressedBg : hoverBg);
                if (downRect.isValid() && (downHover || downPressed))
                    painter->fillRect(downRect.adjusted(0, 0, 1, 1), downPressed ? pressedBg : hoverBg);

                const qreal dividerX = std::floor(std::min(upRect.left(), downRect.left())) + 0.5;
                painter->setRenderHint(QPainter::Antialiasing, false);
                painter->setPen(QPen(softLine, 1.0));
                painter->drawLine(QPointF(dividerX, outer.top()), QPointF(dividerX, outer.bottom()));
                const qreal midY = std::floor(upRect.bottom()) + 0.5;
                painter->drawLine(QPointF(dividerX, midY), QPointF(outer.right(), midY));
                painter->restore();

                drawChevron(upRect, true, upHover, upEnabled);
                drawChevron(downRect, false, downHover, downEnabled);

                painter->setPen(QPen(!enabled ? softLine : focused ? accent : hovered ? accent : line, 1.0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(outer, radius, radius);
                painter->restore();
                return;
            }
        }

        if (control == CC_ComboBox) {
            if (const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option)) {
                const QRectF outer = QRectF(combo->rect).adjusted(0.5, 0.5, -0.5, -0.5);
                const qreal radius = std::clamp(outer.height() * 0.14, 3.0, 6.0);
                const QRectF arrowRect = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
                const bool open = combo->state.testFlag(State_On);
                const bool arrowHover = enabled && (hovered || open);

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                QPainterPath clip;
                clip.addRoundedRect(outer, radius, radius);
                painter->fillPath(clip, enabled ? panel : disabledBg);

                painter->save();
                painter->setClipPath(clip);
                if (arrowRect.isValid()) {
                    if (open)
                        painter->fillRect(arrowRect.adjusted(0, -1, 1, 1), pressedBg);
                    else if (hovered)
                        painter->fillRect(arrowRect.adjusted(0, -1, 1, 1), hoverBg);
                    const qreal dividerX = std::floor(arrowRect.left()) + 0.5;
                    painter->setRenderHint(QPainter::Antialiasing, false);
                    painter->setPen(QPen(softLine, 1.0));
                    painter->drawLine(QPointF(dividerX, outer.top()), QPointF(dividerX, outer.bottom()));
                }
                painter->restore();

                if (arrowRect.isValid())
                    drawChevron(arrowRect, false, arrowHover, enabled);

                painter->setPen(
                    QPen(!enabled ? softLine : (focused || open || hovered) ? accent : line, 1.0));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(outer, radius, radius);
                painter->restore();
                return;
            }
        }

        QProxyStyle::drawComplexControl(control, option, painter, widget);
    }
};
} // namespace

void applyAppTheme(QApplication &application, bool dark, double scale) {
    scale = std::clamp(scale, .75, 2.0);
    if (!application.property("cubeThemeStyleInstalled").toBool()) {
        application.setStyle(new CubeThemeStyle);
        application.setProperty("cubeThemeStyleInstalled", true);
    }
    const QColor window(dark ? "#0f1722" : "#f4f6fa");
    const QColor panel(dark ? "#16202e" : "#ffffff");
    const QColor alternate(dark ? "#141d2a" : "#f8fafc");
    const QColor text(dark ? "#e6edf6" : "#1b2838");
    const QColor groupTitle(dark ? "#b8cae0" : "#3a4e63");
    const QColor muted(dark ? "#95a8be" : "#5e7186");
    const QColor disabled(dark ? "#66788c" : "#94a3b5");
    const QColor button(dark ? "#1e2c3e" : "#ffffff");
    const QColor line(dark ? "#35485e" : "#c5d1de");
    const QColor softLine(dark ? "#243447" : "#dce4ee");
    const QColor accent(dark ? "#6bb0f2" : "#2568b0");
    const QColor highlight(dark ? "#2c669e" : "#2b70b8");
    const QColor hover(dark ? "#263b54" : "#eaf2fb");
    const QColor pressed(dark ? "#2e4968" : "#dceafb");
    const QColor selected(dark ? "#1c3650" : "#eaf3fc");
    const QColor selectedLine(dark ? "#3b6b99" : "#8fb8e0");
    const QColor disabledBackground(dark ? "#15202d" : "#f0f3f7");

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, dark ? QColor("#ff9b9f") : QColor("#b22d3c"));
    palette.setColor(QPalette::Light, dark ? QColor("#455c74") : QColor("#ffffff"));
    palette.setColor(QPalette::Midlight, dark ? QColor("#2f4256") : QColor("#eaf0f5"));
    palette.setColor(QPalette::Mid, line);
    palette.setColor(QPalette::Dark, dark ? QColor("#090f17") : QColor("#8394a5"));
    palette.setColor(QPalette::Shadow, dark ? QColor("#05080c") : QColor("#526779"));
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::LinkVisited, dark ? QColor("#be9fe0") : QColor("#7759a5"));
    palette.setColor(QPalette::PlaceholderText, muted);
    palette.setColor(QPalette::ToolTipBase, dark ? QColor("#223244") : QColor("#1b2838"));
    palette.setColor(QPalette::ToolTipText, QColor("#f5f8fc"));
    for (const auto role :
         {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText})
        palette.setColor(QPalette::Disabled, role, disabled);
    for (const auto role : {QPalette::Base, QPalette::Button})
        palette.setColor(QPalette::Disabled, role, disabledBackground);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, line);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, text);
    application.setPalette(palette);
    QFont font;
    font.setFamilies({"Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei"});
    font.setPixelSize(qRound(14 * scale));
    application.setFont(font);

    QString css = QString::fromUtf8(R"(
QWidget { font-family: "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei"; font-size: 14px; }
QMainWindow, QDialog { background: @window; }
QWidget:disabled { color: @disabled; }
QGroupBox { background: @alternate; border: 1px solid @softline; border-radius: 8px; margin-top: 9px; padding: 3px 6px; }
QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 5px; color: @grouptitle; font-size: 13px; font-weight: 600; }
QFrame#panel { background: @panel; border: 1px solid @softline; border-radius: 8px; }
QFrame#resultRow { background: @panel; border: none; border-bottom: 1px solid @softline; }
QFrame#viewSegment { background: @alternate; border: 1px solid @softline; border-radius: 6px; }
QFrame#brushSwatch, QLabel#brushSwatch { border: 1px solid @line; border-radius: 6px; }
QLabel#paneTitle { color: @text; font-size: 13px; font-weight: 600; }
QLabel#muted, QLabel#smallMuted { color: @muted; font-size: 12px; }
QLabel#methodDescription { color: @muted; }
QLabel#formulaText { color: @text; font-family: "Consolas", "Segoe UI", "Microsoft YaHei UI"; font-size: 15px; }
QPushButton, QToolButton { color: @text; background: @button; border: 1px solid @line; border-radius: 6px; padding: 4px 8px; min-height: 20px; }
QPushButton:focus, QToolButton:focus { border-color: @accent; }
QPushButton:hover, QToolButton:hover { background: @hover; border-color: @accent; }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked, QPushButton:checked { background: @pressed; border-color: @accent; }
QPushButton:disabled, QToolButton:disabled { color: @disabled; background: @disabledbackground; border-color: @softline; }
QToolButton#themeToggle { padding: 0px; border-radius: 6px; }
QToolButton#langToggle { padding: 0px 6px; border-radius: 6px; font-size: 12px; font-weight: 600; }
QToolButton[viewPill="true"] { background: transparent; border: 1px solid transparent; border-radius: 4px; color: @muted; font-weight: 500; }
QToolButton[viewPill="true"]:hover { background: @hover; color: @text; border-color: transparent; }
QToolButton[viewPill="true"]:checked { background: @panel; color: @accent; border: 1px solid @softline; font-weight: 600; }
QToolButton[subtleIcon="true"] { background: @alternate; border: 1px solid @softline; border-radius: 5px; padding: 0px; min-height: 0px; }
QToolButton[subtleIcon="true"]:hover { background: @hover; border-color: @accent; }
QToolButton[presetChip="true"] { background: @panel; border: 1px solid @softline; border-radius: 11px; padding: 2px 8px; min-height: 18px; font-size: 12px; font-weight: 500; color: @text; }
QToolButton[presetChip="true"]:hover { background: @hover; border-color: @accent; color: @accent; }
QToolButton[presetChip="true"]:pressed { background: @pressed; border-color: @accent; }
QToolButton[presetAccent="true"] { background: @selected; border: 1px solid @selectedline; color: @accent; font-weight: 600; }
QToolButton[presetAccent="true"]:hover { background: @hover; border-color: @accent; }
QPushButton#moveButton { font-family: "Consolas", "Segoe UI", "Microsoft YaHei UI"; font-size: 15px; font-weight: 600; padding: 2px 4px; min-height: 19px; min-width: 24px; }
QPushButton#solveCurrent, QPushButton#generateCurrent { font-weight: normal; }
QPushButton#formulaStart { font-weight: 600; }
QPushButton#solveCurrent:enabled:!hover:!pressed,
QPushButton#generateCurrent:enabled:!hover:!pressed,
QPushButton#formulaStart:enabled:!hover:!pressed { background: @selected; border-color: @selectedline; }
QPushButton#formulaStart[running="true"]:enabled { border-color: #a36c43; color: #ffffff; }
QPushButton#formulaStart[running="true"]:enabled:!pressed { background: #9b5c31; }
QPushButton#formulaStart[running="true"]:enabled:hover,
QPushButton#formulaStart[running="true"]:enabled:focus { border-color: @accent; }
QPushButton#formulaStart[running="true"]:enabled:pressed { background: #814820; }
QPushButton#smallButton { padding: 2px 6px; min-height: 18px; font-size: 11px; }
QToolButton#resultDirection, QToolButton#resultRun { background: transparent; border: none; border-radius: 5px; }
QToolButton#resultDirection:enabled:hover, QToolButton#resultRun:enabled:hover,
QToolButton#resultDirection:enabled:focus, QToolButton#resultRun:enabled:focus { background: @hover; }
QToolButton#resultDirection:enabled:pressed, QToolButton#resultRun:enabled:pressed { background: @pressed; }
QLineEdit, QTextEdit, QPlainTextEdit {
    color: @text; background: @panel; border: 1px solid @line; border-radius: 6px;
    padding: 3px 7px; min-height: 19px; selection-background-color: @highlight; selection-color: #ffffff;
}
QSpinBox QLineEdit, QDoubleSpinBox QLineEdit {
    border: none; border-radius: 0px; background: transparent; padding: 0px 4px; min-height: 0px;
}
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus { border-color: @accent; }
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled { color: @disabled; background: @disabledbackground; border-color: @softline; }
QLineEdit#formulaInput { font-family: "Consolas", "Segoe UI", "Microsoft YaHei UI"; font-size: 14px; }
QPlainTextEdit#formulaOutput, QPlainTextEdit#searchStatistics { font-family: "Consolas", "Microsoft YaHei UI", "Microsoft YaHei"; }
QComboBox QAbstractItemView { background: @panel; color: @text; border: 1px solid @line; selection-background-color: @highlight; selection-color: #ffffff; }
QAbstractItemView { background: @panel; color: @text; alternate-background-color: @alternate; border: 1px solid @line; selection-background-color: @highlight; selection-color: #ffffff; }
QCheckBox, QRadioButton { spacing: 6px; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox:disabled, QRadioButton:disabled { color: @disabled; }
QMenuBar { background: @window; color: @text; border-bottom: 1px solid @softline; }
QMenuBar::item { background: transparent; padding: 5px 10px; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: @hover; border-radius: 5px; }
QMenu { background: @panel; color: @text; border: 1px solid @line; border-radius: 6px; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 18px; border-radius: 4px; }
QMenu::item:selected { background: @highlight; color: #ffffff; }
QMenu::item:disabled { color: @disabled; }
QMenu::separator { height: 1px; background: @softline; margin: 4px 7px; }
QToolBar { background: @window; border: none; border-bottom: 1px solid @softline; spacing: 4px; padding: 3px; }
QToolBar::separator { width: 1px; background: @softline; margin: 3px; }
QToolTip { color: #f5f8fc; background: #1b2838; border: 1px solid #415870; padding: 5px 8px; border-radius: 5px; font-size: 12px; }
QTabWidget::pane { border: 1px solid @line; background: @panel; }
QTabBar::tab { color: @muted; background: @alternate; border: 1px solid @line; padding: 5px 10px; }
QTabBar::tab:selected { color: @text; background: @panel; border-bottom-color: @panel; }
QHeaderView::section { color: @text; background: @alternate; border: none; border-right: 1px solid @softline; border-bottom: 1px solid @line; padding: 4px; }
QProgressBar { background: @alternate; color: @text; border: 1px solid @softline; border-radius: 3px; text-align: center; min-height: 9px; }
QProgressBar::chunk { background: @highlight; border-radius: 2px; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QWidget#mainResultsBody { background: @panel; }
QWidget#stickerEditor { background: @window; }
QScrollBar:vertical { background: transparent; width: 9px; margin: 2px 1px; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 1px 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: @line; border-radius: 4px; min-height: 28px; min-width: 28px; }
QScrollBar::handle:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QSplitter::handle { background: @softline; width: 3px; }
QSplitter::handle:hover { background: @accent; }
QStatusBar { background: @window; color: @muted; border-top: 1px solid @softline; }
QStatusBar::item { border: none; }
)");
    for (const auto &pair :
         {std::pair{"@disabledbackground", disabledBackground}, std::pair{"@selectedline", selectedLine},
          std::pair{"@grouptitle", groupTitle}, std::pair{"@highlight", highlight},
          std::pair{"@softline", softLine}, std::pair{"@selected", selected},
          std::pair{"@alternate", alternate}, std::pair{"@disabled", disabled}, std::pair{"@window", window},
          std::pair{"@panel", panel}, std::pair{"@button", button}, std::pair{"@accent", accent},
          std::pair{"@pressed", pressed}, std::pair{"@hover", hover}, std::pair{"@muted", muted},
          std::pair{"@line", line}, std::pair{"@text", text}})
        css.replace(QString::fromLatin1(pair.first), pair.second.name());
    // These are logical CSS pixels. Do not multiply by devicePixelRatio():
    // Qt already applies monitor DPI when rasterizing the resulting widgets.
    const QRegularExpression pixels(R"((\d+(?:\.\d+)?)px)");
    QString scaled;
    qsizetype cursor = 0;
    auto matches = pixels.globalMatch(css);
    while (matches.hasNext()) {
        const auto match = matches.next();
        scaled += css.mid(cursor, match.capturedStart() - cursor);
        scaled += QString::number(qRound(match.captured(1).toDouble() * scale)) + "px";
        cursor = match.capturedEnd();
    }
    scaled += css.mid(cursor);
    application.setProperty("cubeDarkMode", dark);
    application.setProperty("cubeUiScale", scale);
    application.setStyleSheet(scaled);
}
