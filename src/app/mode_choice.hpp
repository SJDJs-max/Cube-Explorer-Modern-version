#pragma once
#include "ui_i18n.hpp"
#include "ui_icons.hpp"
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <array>

class ModeChoiceDialog final : public QDialog {
  public:
    explicit ModeChoiceDialog(double scale, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle(trUi("选择求解方式", "Choose Solving Method"));
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(qRound(14 * scale), qRound(14 * scale), qRound(14 * scale),
                                   qRound(14 * scale));
        layout->setSpacing(qRound(12 * scale));
        setMinimumHeight(qRound(176 * scale));
        const std::array<QString, 2> titles = {trUi("方法一", "Method 1"), trUi("方法二", "Method 2")};
        const std::array<QString, 2> descriptions = {
            trUi("常规求解\nKociemba 二阶段", "Standard Solver\nKociemba Two-Phase"),
            trUi("公式求解\n完整 / 不完整魔方", "Formula Search\nFull / Partial Cube")};
        const std::array<UiIcon, 2> icons = {UiIcon::Zap, UiIcon::Sliders};
        const QColor iconColor = palette().color(QPalette::Link);
        for (int i = 0; i < 2; ++i) {
            auto *button = new QPushButton;
            button->setObjectName(QString("methodChoice%1").arg(i + 1));
            button->setCursor(Qt::PointingHandCursor);
            button->setMinimumSize(qRound(226 * scale), qRound(148 * scale));
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            button->setStyleSheet(QString("QPushButton { min-width:%1px;min-height:%2px;border-radius:%3px; }")
                                      .arg(qRound(226 * scale))
                                      .arg(qRound(148 * scale))
                                      .arg(qRound(8 * scale)));
            button->setAccessibleName(titles[i] + "，" + descriptions[i]);
            auto *inside = new QVBoxLayout(button);
            inside->setContentsMargins(qRound(12 * scale), qRound(12 * scale), qRound(12 * scale),
                                       qRound(12 * scale));
            inside->setSpacing(qRound(6 * scale));
            auto *badge = new QLabel;
            badge->setAlignment(Qt::AlignCenter);
            badge->setStyleSheet("background:transparent;");
            badge->setPixmap(uiIcon(icons[i], iconColor, qRound(22 * scale)).pixmap(qRound(22 * scale)));
            auto *title = new QLabel(titles[i]);
            title->setObjectName("methodTitle");
            title->setAlignment(Qt::AlignCenter);
            title->setStyleSheet(
                QString("font-size:%1px;font-weight:700;background:transparent;").arg(qRound(22 * scale)));
            auto *description = new QLabel(descriptions[i]);
            description->setObjectName("methodDescription");
            description->setAlignment(Qt::AlignCenter);
            description->setStyleSheet(
                QString("font-size:%1px;font-weight:400;background:transparent;").arg(qRound(14 * scale)));
            for (auto *label : {badge, title, description})
                label->setAttribute(Qt::WA_TransparentForMouseEvents);
            inside->addStretch();
            inside->addWidget(badge);
            inside->addWidget(title);
            inside->addWidget(description);
            inside->addStretch();
            layout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [this, i] { done(i + 1); });
        }
    }
};
