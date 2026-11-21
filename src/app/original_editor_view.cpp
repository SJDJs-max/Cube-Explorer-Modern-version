#include "original_editor_view.hpp"
#include "ui_i18n.hpp"
#include "ui_icons.hpp"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <stdexcept>

OriginalEditorView::OriginalEditorView(QWidget *parent)
    : QWidget(parent), canvas_(new CubeCanvas(false, this)), autoFix_(new QCheckBox(this)),
      brushCaption_(new QLabel(this)), brushSwatch_(new QLabel(this)), viewBar_(new QFrame(this)),
      resetCamera_(new QToolButton(this)) {
    setObjectName("originalEditorView");
    auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    setMinimumWidth(375); // 75% of the 500 logical-pixel reference panel.
    resize(sizeHint());

    canvas_->setObjectName("editorCanvas");
    canvas_->setMinimumSize(0, 0);
    canvas_->onSticker = [this](int index, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
        if (onSticker)
            onSticker(index, button, modifiers);
    };

    viewBar_->setObjectName("viewSegment");
    auto *barLayout = new QHBoxLayout(viewBar_);
    barLayout->setContentsMargins(2, 2, 2, 2);
    barLayout->setSpacing(2);
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QToolButton(viewBar_);
        btn->setObjectName(QString("viewPill_%1").arg(i));
        btn->setProperty("viewPill", true);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(btn, &QToolButton::clicked, this, [this, i] {
            setView(i);
            if (onViewChanged)
                onViewChanged(i);
        });
        viewButtons_[i] = btn;
        barLayout->addWidget(btn);
    }
    viewButtons_[0]->setChecked(true);

    resetCamera_->setObjectName("resetCameraButton");
    resetCamera_->setProperty("subtleIcon", true);
    resetCamera_->setCursor(Qt::PointingHandCursor);
    resetCamera_->hide();
    connect(resetCamera_, &QToolButton::clicked, this, [this] { canvas_->resetCamera(); });

    autoFix_->setObjectName("autoFix");
    autoFix_->setChecked(true);
    connect(autoFix_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (onAutoFix)
            onAutoFix(enabled);
    });

    brushCaption_->setObjectName("brushCaption");
    brushCaption_->setAlignment(Qt::AlignCenter);
    brushCaption_->setAttribute(Qt::WA_TransparentForMouseEvents);

    brushSwatch_->setObjectName("brushSwatch");
    brushSwatch_->setAttribute(Qt::WA_TransparentForMouseEvents);

    retranslateUi();
    updateBrush();
    updateIcons();
    arrangeChildren();
}

QSize OriginalEditorView::sizeHint() const {
    return {500, heightForWidth(500)};
}
QSize OriginalEditorView::minimumSizeHint() const {
    return {375, heightForWidth(375)};
}
int OriginalEditorView::heightForWidth(int width) const {
    return static_cast<int>(std::ceil(std::max(0, width) * 234.0 / 332.0));
}

void OriginalEditorView::setFacelets(std::string raw, const std::array<std::int8_t, 6> &centers) {
    canvas_->setFacelets(std::move(raw), centers);
}
void OriginalEditorView::setColors(const std::array<QColor, 7> &colors) {
    canvas_->setColors(colors);
    updateBrush();
}
void OriginalEditorView::setColors(const std::array<QColor, 6> &colors) {
    canvas_->setColors(colors);
    updateBrush();
}
void OriginalEditorView::setDarkMode(bool dark) {
    dark_ = dark;
    canvas_->setDarkMode(dark);
    updateBrush();
    updateIcons();
}
void OriginalEditorView::setView(int view) {
    if (view < 0 || view > 2)
        throw std::invalid_argument("Unknown cube editor view.");
    view_ = view;
    canvas_->setView(view == 0 ? CubeCanvas::View::Classic
                               : view == 1 ? CubeCanvas::View::Net2D : CubeCanvas::View::Cube3D);
    for (int i = 0; i < 3; ++i) {
        const QSignalBlocker blocker(viewButtons_[i]);
        viewButtons_[i]->setChecked(i == view_);
    }
    resetCamera_->setVisible(view_ == 2);
    arrangeChildren();
}
void OriginalEditorView::setBrush(char brush) {
    brush_ = brush;
    updateBrush();
}
void OriginalEditorView::setAutoFix(bool enabled) {
    const QSignalBlocker blocker(autoFix_);
    autoFix_->setChecked(enabled);
}
bool OriginalEditorView::autoFix() const {
    return autoFix_->isChecked();
}

void OriginalEditorView::retranslateUi() {
    autoFix_->setText(trUi("自动修正", "Auto-Fix"));
    autoFix_->setToolTip(trUi("按原版规则在左键染色后自动修正魔方颜色。",
                              "Automatically fix sticker conflicts after left-click painting."));
    brushCaption_->setText(trUi("画笔", "Brush"));
    brushSwatch_->setAccessibleName(trUi("当前画笔颜色", "Current brush color"));
    viewButtons_[0]->setText(trUi("经典", "Classic"));
    viewButtons_[1]->setText("2D");
    viewButtons_[2]->setText("3D");
    resetCamera_->setToolTip(trUi("重置 3D 观察视角", "Reset 3D camera angle"));
    updateBrush();
}

void OriginalEditorView::updateIcons() {
    const double uiScale = std::max(0.75, width() / 500.0);
    const QColor ink = dark_ ? QColor("#9cb0c6") : QColor("#52667a");
    resetCamera_->setIcon(uiIcon(UiIcon::ResetCamera, ink, qRound(14 * uiScale)));
    resetCamera_->setIconSize(QSize(qRound(14 * uiScale), qRound(14 * uiScale)));
}

void OriginalEditorView::updateBrush() {
    const auto index = cube::face_names.find(brush_);
    const auto &color = canvas_->colors()[index == std::string_view::npos ? 6 : index];
    const double uiScale = std::max(0.75, width() / 500.0);
    const int radius = std::max(4, qRound(6 * uiScale));
    brushSwatch_->setStyleSheet(QString("background:%1;border:2px solid %2;border-radius:%3px;")
                                    .arg(color.name(), dark_ ? "#7a90a8" : "#64788c")
                                    .arg(radius));
    brushSwatch_->setToolTip(
        trUi("当前画笔：%1 面颜色（%2）。点击中心色块选取画笔。",
             "Active brush: %1 face (%2). Left-click a center sticker to pick.")
            .arg(QChar::fromLatin1(brush_), color.name()));
}

void OriginalEditorView::arrangeChildren() {
    const double scale = width() / 332.0;
    const double uiScale = width() / 500.0;
    const auto px = [uiScale](double value) { return qRound(value * uiScale); };

    autoFix_->setStyleSheet(QString("QCheckBox { font-size:%1px; spacing:%2px; } "
                                    "QCheckBox::indicator { width:%3px; height:%3px; }")
                                .arg(px(14))
                                .arg(px(5))
                                .arg(px(13)));
    brushCaption_->setStyleSheet(
        QString("QLabel { font-size:%1px; color:%2; background:transparent; }")
            .arg(px(11))
            .arg(dark_ ? "#8da1b5" : "#64748b"));
    const auto pillStyle = QString("QToolButton { font-size:%1px; padding:0 %2px; min-height:0; }")
                               .arg(px(12))
                               .arg(px(5));
    for (auto *btn : viewButtons_)
        btn->setStyleSheet(pillStyle);

    const auto area = [scale](int x, int y, int w, int h) {
        return QRect(qRound(x * scale), qRound(y * scale), qRound(w * scale), qRound(h * scale));
    };

    // Top row: inline view switcher on top-left, optional 3D camera reset on top-right.
    viewBar_->setGeometry(area(4, 2, 118, 19));
    resetCamera_->setGeometry(area(306, 2, 22, 19));
    // Bottom-left: Auto-Fix; Bottom-right: compact brush indicator.
    autoFix_->setGeometry(area(4, 212, 92, 19));
    brushCaption_->setGeometry(area(276, 174, 42, 14));
    brushSwatch_->setGeometry(area(283, 190, 28, 28));

    switch (view_) {
    case 0:
        // Generous breathing room around Classic unfolded cube.
        canvas_->setDrawingMargin(px(8));
        canvas_->setGeometry(area(0, 22, 332, 210));
        break;
    case 1:
        canvas_->setDrawingMargin(px(8));
        canvas_->setGeometry(area(0, 22, 332, 206));
        break;
    case 2:
        // Centered 3D cube with clear margin away from top pill and compact brush.
        canvas_->setDrawingMargin(px(6));
        canvas_->setGeometry(area(26, 20, 280, 210));
        break;
    }
    updateBrush();
    updateIcons();
    viewBar_->raise();
    resetCamera_->raise();
    autoFix_->raise();
    brushCaption_->raise();
    brushSwatch_->raise();
}

void OriginalEditorView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    const int expectedHeight = heightForWidth(width());
    if (minimumHeight() != expectedHeight || maximumHeight() != expectedHeight)
        setFixedHeight(expectedHeight);
    arrangeChildren();
}
