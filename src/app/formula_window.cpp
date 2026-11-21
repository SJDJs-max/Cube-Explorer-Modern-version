#include "formula_window.hpp"
#include "ui_i18n.hpp"
#include "ui_icons.hpp"
#include <QMenu>
#include <QScrollArea>
#include <QTextBlock>
#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <numeric>

namespace {
struct FormulaDelivery {
    std::mutex mutex;
    std::condition_variable_any ready;
    bool delivered = false;
    std::exception_ptr error;
};

QString moveTokenLabel(int symbolIndex, int powerIndex) {
    QString text(move_names[symbolIndex]);
    if (powerIndex == 1)
        text += '2';
    else if (powerIndex == 2)
        text += '\'';
    return text;
}
} // namespace

QSpinBox *FormulaWindow::spin(QFormLayout *form, QLabel *&labelOut, const QString &label, int low, int high,
                              int value) {
    auto *box = new QSpinBox;
    box->setRange(low, high);
    box->setValue(value);
    box->setMinimumWidth(75);
    box->setMaximumWidth(130);
    labelOut = new QLabel(label);
    form->addRow(labelOut, box);
    settings_.push_back(box);
    return box;
}

FormulaWindow::FormulaWindow(PatternState input, bool generate, std::function<void(ResultData)> sink,
                             QWidget *parent)
    : QDialog(parent), input_(std::move(input)), generate_(generate), sink_(std::move(sink)) {
    for (auto &row : movePowerEnabled_)
        row.fill(true);
    maxSymbolUses_.fill(-1);

    setObjectName("formulaWindow");
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    setWindowModality(Qt::NonModal);
    setWindowTitle(generate ? "公式窗口 · 生成当前情况" : "公式窗口 · 求解当前情况");
    resize(550, 670);
    setMinimumSize(qRound(610 * .75), qRound(740 * .75));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    output_ = new QPlainTextEdit;
    output_->setObjectName("formulaOutput");
    output_->setReadOnly(true);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font;
    font.setFamilies({"Consolas", "Microsoft YaHei UI", "Microsoft YaHei"});
    font.setPixelSize(15);
    output_->setFont(font);
    outer->addWidget(output_, 1);

    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 0, 0);
    bottom->setSpacing(12);
    auto *moveColumn = new QVBoxLayout;
    moveColumn->setSpacing(6);
    moveGroup_ = new QGroupBox;
    auto *grid = new QGridLayout(moveGroup_);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    for (int i = 0; i < 18; ++i) {
        auto *box = new QCheckBox(QString(move_names[i]));
        box->setObjectName("formulaMove_" + QString(move_names[i]));
        box->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(box, &QWidget::customContextMenuRequested, this, [this, i, box](const QPoint &pos) {
            if (!busy_)
                showMoveContextMenu(i, box->mapToGlobal(pos));
        });
        // Optional groups start with an empty subset. Switching a master does
        // not silently add moves that the user has not selected.
        box->setChecked(i < 6);
        moveBoxes_[i] = box;
        const int row = i < 12 ? i / 6 : i < 15 ? 2 : 3;
        const int column = i < 12 ? i % 6 : i < 15 ? i - 12 : i - 15;
        grid->addWidget(box, row, column);
        settings_.push_back(box);
    }
    moveColumn->addWidget(moveGroup_);
    auto *masters = new QVBoxLayout;
    masters->setSpacing(6);
    for (int i = 0; i < 3; ++i) {
        categoryBoxes_[i] = new QCheckBox;
        categoryBoxes_[i]->setObjectName(QString("formulaCategory_%1").arg(i));
        settings_.push_back(categoryBoxes_[i]);
        masters->addWidget(categoryBoxes_[i]);
        connect(categoryBoxes_[i], &QCheckBox::toggled, this, [this] { refreshControls(); });
    }
    moveColumn->addLayout(masters);
    bottom->addLayout(moveColumn, 1);
    auto *buttons = new QVBoxLayout;
    buttons->setSpacing(10);
    start_ = new QPushButton;
    clear_ = new QPushButton;
    add_ = new QPushButton;
    start_->setObjectName("formulaStart");
    clear_->setObjectName("formulaClear");
    add_->setObjectName("formulaAdd");
    for (auto *button : {start_, clear_, add_}) {
        button->setFixedWidth(240);
        button->setFixedHeight(50);
        button->setAutoDefault(false);
        buttons->addWidget(button, 1);
    }
    bottom->addLayout(buttons);
    outer->addLayout(bottom);

    fold_ = new QToolButton;
    fold_->setObjectName("formulaSettingsToggle");
    fold_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    fold_->setArrowType(Qt::RightArrow);
    fold_->setCheckable(true);

    // Compact MCC sort toolbar sits on the foldRow line — zero vertical height taken from output_.
    mccSort_ = new QComboBox;
    mccSort_->setObjectName("formulaMccSort");
    mccSort_->addItems({"", "", ""});
    mccSort_->setCurrentIndex(0);
    connect(mccSort_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!results_.empty())
            rebuildOutput();
    });

    mccParamsBtn_ = new QToolButton;
    mccParamsBtn_->setObjectName("formulaMccParams");
    mccParamsBtn_->setAutoRaise(true);
    connect(mccParamsBtn_, &QToolButton::clicked, this, &FormulaWindow::showMccParamsDialog);

    auto *foldRow = new QHBoxLayout;
    foldRow->setContentsMargins(0, 0, 0, 0);
    foldRow->setSpacing(6);
    foldRow->addWidget(fold_, 0, Qt::AlignLeft);
    foldRow->addStretch();
    foldRow->addWidget(mccSort_);
    foldRow->addWidget(mccParamsBtn_);
    outer->addLayout(foldRow);

    auto *advanced = new QWidget;
    advanced->setObjectName("formulaAdvancedSettings");
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 2, 0, 0);
    advancedLayout->setSpacing(6);
    auto *powers = new QHBoxLayout;
    powersLabel_ = new QLabel;
    powers->addWidget(powersLabel_);
    for (int i = 0; i < 3; ++i) {
        powerBoxes_[i] = new QCheckBox;
        powerBoxes_[i]->setChecked(true);
        powerBoxes_[i]->setObjectName(QString("formulaPower_%1").arg(i + 1));
        powers->addWidget(powerBoxes_[i]);
        settings_.push_back(powerBoxes_[i]);
    }
    powers->addStretch();
    fineMovesBtn_ = new QPushButton;
    fineMovesBtn_->setObjectName("formulaFineMoves");
    fineMovesBtn_->setAutoDefault(false);
    connect(fineMovesBtn_, &QPushButton::clicked, this, &FormulaWindow::showFineMovesDialog);
    powers->addWidget(fineMovesBtn_);
    settings_.push_back(fineMovesBtn_);
    advancedLayout->addLayout(powers);

    auto *columns = new QHBoxLayout;
    auto *left = new QFormLayout;
    auto *right = new QFormLayout;
    minimum_ = spin(left, minLabel_, "", 0, std::numeric_limits<int>::max(), 0);
    maximum_ = spin(left, maxLabel_, "", -1, std::numeric_limits<int>::max(), -1);
    resultLimit_ = spin(left, resLabel_, "", 0, std::numeric_limits<int>::max(), 0);
    maxRotations_ = spin(left, maxRotLabel_, "", -1, 20, -1);
    timeLimit_ = spin(right, timeLabel_, "", 0, std::numeric_limits<int>::max(), 0);
    minimum_->setObjectName("formulaMinimum");
    maximum_->setObjectName("formulaMaximum");
    resultLimit_->setObjectName("formulaResultLimit");
    maxRotations_->setObjectName("formulaMaxRotations");
    timeLimit_->setObjectName("formulaTimeLimit");
    nodes_ = new QDoubleSpinBox;
    nodes_->setMaximumWidth(150);
    nodes_->setDecimals(0);
    nodes_->setRange(0, 1000000000000.0);
    nodes_->setSingleStep(1000000);
    nodes_->setValue(0);
    nodesLabel_ = new QLabel;
    right->addRow(nodesLabel_, nodes_);
    settings_.push_back(nodes_);
    heuristic_ = new QComboBox;
    heuristic_->setObjectName("formulaHeuristic");
    heuristic_->addItems({"", "", ""});
    heurLabel_ = new QLabel;
    right->addRow(heurLabel_, heuristic_);
    settings_.push_back(heuristic_);
    columns->addLayout(left, 1);
    columns->addSpacing(14);
    columns->addLayout(right, 1);
    advancedLayout->addLayout(columns);

    auto *rules = new QGridLayout;
    modU_ = new QCheckBox;
    symmetry_ = new QCheckBox;
    dedup_ = new QCheckBox;
    triples_ = new QCheckBox;
    memoize_ = new QCheckBox;
    rotationQuotient_ = new QCheckBox;
    const std::array<QCheckBox *, 6> checks = {modU_,    symmetry_, dedup_,
                                               triples_, memoize_,  rotationQuotient_};
    for (int i = 0; i < 6; ++i) {
        checks[i]->setChecked(true);
        rules->addWidget(checks[i], i / 2, i % 2);
        settings_.push_back(checks[i]);
    }
    advancedLayout->addLayout(rules);
    advanced->hide();
    outer->addWidget(advanced);
    connect(fold_, &QToolButton::toggled, this, [this, advanced](bool expanded) {
        fold_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        advanced->setVisible(expanded);
    });

    connect(start_, &QPushButton::clicked, this, [this] {
        if (busy_)
            stopSearch();
        else
            startSearch();
    });
    connect(clear_, &QPushButton::clicked, this, &FormulaWindow::clearResults);
    connect(add_, &QPushButton::clicked, this, &FormulaWindow::addResults);
    for (auto *layout : findChildren<QLayout *>()) {
        int horizontal = -1, vertical = -1;
        if (auto *g = qobject_cast<QGridLayout *>(layout)) {
            horizontal = g->horizontalSpacing();
            vertical = g->verticalSpacing();
        }
        if (auto *f = qobject_cast<QFormLayout *>(layout)) {
            horizontal = f->horizontalSpacing();
            vertical = f->verticalSpacing();
        }
        layoutMetrics_.push_back(
            {layout, layout->contentsMargins(), layout->spacing(), horizontal, vertical});
    }
    retranslateUi();
    setUiScale(std::clamp(std::min(width() / 610.0, height() / 740.0), .75, 2.0));
    refreshControls();
}

FormulaWindow::~FormulaWindow() {
    // Queued callbacks target this QObject and are discarded when it is destroyed.
    ++generation_;
    if (worker_.joinable()) {
        worker_.request_stop();
        worker_.join();
    }
}

QString FormulaWindow::startText() const {
    return start_->text();
}

void FormulaWindow::refreshControls() {
    for (auto *widget : settings_)
        widget->setEnabled(!busy_);
    for (int i = 6; i < 18; ++i) {
        const int category = i < 12 ? 1 : i < 15 ? 0 : 2;
        moveBoxes_[i]->setEnabled(!busy_ && categoryBoxes_[category]->isChecked());
    }
    if (maxRotations_)
        maxRotations_->setEnabled(!busy_ && categoryBoxes_[2]->isChecked());
    clear_->setEnabled(!busy_);
    add_->setEnabled(true); // Original BAdd stays enabled even for an empty memo.
}

void FormulaWindow::updateMoveBoxDecoration(int symbolIndex) {
    if (symbolIndex < 0 || symbolIndex >= 18 || !moveBoxes_[symbolIndex])
        return;
    const auto &powers = movePowerEnabled_[symbolIndex];
    const bool anyDisabled = !powers[0] || !powers[1] || !powers[2];
    const int limit = maxSymbolUses_[symbolIndex];
    QString text(move_names[symbolIndex]);
    if (anyDisabled || limit >= 0)
        text += '*';
    moveBoxes_[symbolIndex]->setText(text);

    QStringList allowedList, bannedList;
    for (int p = 0; p < 3; ++p) {
        const QString tok = moveTokenLabel(symbolIndex, p);
        if (powers[p])
            allowedList << tok;
        else
            bannedList << tok;
    }
    QString tip = trUi("右键可单独禁用 %1 / %2 / %3 或设置次数上限",
                       "Right-click to toggle %1 / %2 / %3 or set max uses")
                      .arg(moveTokenLabel(symbolIndex, 0), moveTokenLabel(symbolIndex, 1),
                           moveTokenLabel(symbolIndex, 2));
    if (anyDisabled) {
        tip += "\n" + trUi("允许：%1 · 已禁：%2", "Allowed: %1 · Disabled: %2")
                          .arg(allowedList.isEmpty() ? trUi("无", "None") : allowedList.join(", "),
                               bannedList.join(", "));
    }
    if (limit >= 0) {
        tip += "\n" + trUi("最多出现次数：%1", "Max uses: %1").arg(limit);
    }
    moveBoxes_[symbolIndex]->setToolTip(tip);
}

void FormulaWindow::showMoveContextMenu(int symbolIndex, const QPoint &globalPos) {
    if (symbolIndex < 0 || symbolIndex >= 18)
        return;
    QMenu menu(this);
    for (int p = 0; p < 3; ++p) {
        auto *act = menu.addAction(moveTokenLabel(symbolIndex, p));
        act->setCheckable(true);
        act->setChecked(movePowerEnabled_[symbolIndex][p]);
        connect(act, &QAction::triggered, this, [this, symbolIndex, p](bool checked) {
            setMovePowerEnabled(symbolIndex, p, checked);
        });
    }
    menu.addSeparator();
    auto *allowAll = menu.addAction(trUi("全部允许（90° / 180° / 逆90°）", "Allow All Angles"));
    connect(allowAll, &QAction::triggered, this, [this, symbolIndex] {
        for (int p = 0; p < 3; ++p)
            movePowerEnabled_[symbolIndex][p] = true;
        updateMoveBoxDecoration(symbolIndex);
    });
    auto *noDouble = menu.addAction(trUi("仅禁止 180°（%1）", "Disable 180° Only (%1)")
                                        .arg(moveTokenLabel(symbolIndex, 1)));
    connect(noDouble, &QAction::triggered, this, [this, symbolIndex] {
        movePowerEnabled_[symbolIndex] = {true, false, true};
        updateMoveBoxDecoration(symbolIndex);
    });
    menu.addSeparator();
    const int curLim = maxSymbolUses_[symbolIndex];
    auto *setLim = menu.addAction(
        curLim < 0 ? trUi("设置最大出现次数（当前：不限）…", "Set Max Uses (Unlimited)…")
                   : trUi("设置最大出现次数（当前：≤ %1）…", "Set Max Uses (≤ %1)…").arg(curLim));
    connect(setLim, &QAction::triggered, this, [this, symbolIndex] {
        bool ok = false;
        const int v = QInputDialog::getInt(
            this, trUi("单层次数上限", "Per-Layer Max Uses"),
            trUi("符号 %1 的最大出现次数（-1 表示不限，0 表示禁用）：",
                 "Max occurrences for %1 (-1 = unlimited, 0 = forbid):")
                .arg(QString(move_names[symbolIndex])),
            maxSymbolUses_[symbolIndex], -1, 30, 1, &ok);
        if (ok)
            setMaxSymbolUses(symbolIndex, v);
    });
    menu.exec(globalPos);
}

void FormulaWindow::showFineMovesDialog() {
    if (busy_)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(trUi("精细动作与次数限制", "Fine-Grained Moves & Limits"));
    auto *root = new QVBoxLayout(&dlg);
    auto *hint = new QLabel(trUi("可单独勾选/取消任意具体转动（如禁 F2 / L2），或限制单个符号的最大出现次数（-1 为不限）。",
                                 "Toggle individual move powers (e.g. ban F2 / L2) or set per-symbol max uses (-1 = unlimited)."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(4);
    grid->addWidget(new QLabel(trUi("符号", "Move")), 0, 0);
    grid->addWidget(new QLabel("90°"), 0, 1);
    grid->addWidget(new QLabel("180°"), 0, 2);
    grid->addWidget(new QLabel(trUi("逆90°", "90° CCW")), 0, 3);
    grid->addWidget(new QLabel(trUi("次数上限", "Max Uses")), 0, 4);

    std::array<std::array<QCheckBox *, 3>, 18> checks{};
    std::array<QSpinBox *, 18> limits{};
    const QString unlimited = trUi("不限", "Unlimited");
    for (int s = 0; s < 18; ++s) {
        auto *lbl = new QLabel(QString("<b>%1</b>").arg(QString(move_names[s])));
        grid->addWidget(lbl, s + 1, 0);
        for (int p = 0; p < 3; ++p) {
            checks[s][p] = new QCheckBox(moveTokenLabel(s, p));
            checks[s][p]->setChecked(movePowerEnabled_[s][p]);
            if (!powerBoxes_[p]->isChecked()) {
                checks[s][p]->setEnabled(false);
                checks[s][p]->setToolTip(trUi("全局“转动角度”已关闭此列", "Disabled by global Turn Angles"));
            }
            grid->addWidget(checks[s][p], s + 1, p + 1);
        }
        limits[s] = new QSpinBox;
        limits[s]->setRange(-1, 30);
        limits[s]->setFixedWidth(84);
        limits[s]->setSpecialValueText(unlimited);
        limits[s]->setValue(maxSymbolUses_[s]);
        grid->addWidget(limits[s], s + 1, 4);
    }
    scroll->setWidget(content);
    scroll->setMinimumSize(460, 380);
    root->addWidget(scroll, 1);

    auto *quickRow = new QHBoxLayout;
    auto *banAwkward2 = new QPushButton(trUi("禁 F2 / B2 / L2 / D2", "Ban F2 / B2 / L2 / D2"));
    connect(banAwkward2, &QPushButton::clicked, &dlg, [&] {
        // URFDLB indices: F=2, D=3, L=4, B=5
        for (int s : {2, 3, 4, 5})
            checks[s][1]->setChecked(false);
    });
    auto *resetAll = new QPushButton(trUi("恢复全部默认", "Reset All"));
    connect(resetAll, &QPushButton::clicked, &dlg, [&] {
        for (int s = 0; s < 18; ++s) {
            for (int p = 0; p < 3; ++p)
                checks[s][p]->setChecked(true);
            limits[s]->setValue(-1);
        }
    });
    quickRow->addWidget(banAwkward2);
    quickRow->addWidget(resetAll);
    quickRow->addStretch();
    root->addLayout(quickRow);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted) {
        for (int s = 0; s < 18; ++s) {
            for (int p = 0; p < 3; ++p)
                movePowerEnabled_[s][p] = checks[s][p]->isChecked();
            maxSymbolUses_[s] = limits[s]->value();
            updateMoveBoxDecoration(s);
        }
    }
}

void FormulaWindow::showMccParamsDialog() {
    QDialog dlg(this);
    dlg.setWindowTitle(trUi("MCC 手感评分参数", "MCC Ergonomics Parameters"));
    dlg.setFixedWidth(345);
    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(10);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(6);
    struct Field {
        QString label;
        double *target;
    };
    MccParams edit = mccParams_;
    const std::vector<Field> fields = {
        {trUi("p1 手腕转动 (wristMult)", "p1 Wrist turn (wristMult)"), &edit.wrist_mult},
        {trUi("p2 推指系数 (pushMult)", "p2 Push flick (pushMult)"), &edit.push_mult},
        {trUi("p3 无名指系数 (ringMult)", "p3 Ring finger (ringMult)"), &edit.ring_mult},
        {trUi("p4 失稳惩罚 (destabilize)", "p4 Destabilize"), &edit.destabilize},
        {trUi("p5 换手代价 (addRegrip)", "p5 Regrip penalty (addRegrip)"), &edit.add_regrip},
        {trUi("p6 180° 双拨 (double)", "p6 Double flick (double)"), &edit.double_mult},
        {trUi("p7 S/E 中层 (sesliceMult)", "p7 S/E slice (sesliceMult)"), &edit.se_slice_mult},
        {trUi("p8 手指过劳 (overWorkMult)", "p8 Finger overwork"), &edit.overwork_mult},
        {trUi("p9 动作阻挡 (moveblock)", "p9 Move block"), &edit.moveblock},
        {trUi("p10 整体转体 (rotation)", "p10 Rotation"), &edit.rotation},
    };
    std::vector<QDoubleSpinBox *> boxes;
    boxes.reserve(fields.size());
    for (const auto &f : fields) {
        auto *sb = new QDoubleSpinBox;
        sb->setRange(0.0, 20.0);
        sb->setDecimals(2);
        sb->setSingleStep(0.05);
        sb->setFixedWidth(96);
        sb->setValue(*f.target);
        form->addRow(f.label, sb);
        boxes.push_back(sb);
    }
    root->addLayout(form);

    auto *sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    auto *ignoreAufBox = new QCheckBox(trUi("去起手 U（忽略 pre-AUF）", "Ignore pre-AUF (leading U)"));
    ignoreAufBox->setObjectName("formulaMccIgnoreAuf");
    ignoreAufBox->setChecked(mccIgnoreAuf_);
    root->addWidget(ignoreAufBox);

    auto *aufHint = new QLabel(trUi("仅在计算 MCC 时忽略开头的 U（或 d/D 后的 U），不修改公式本身与步数。",
                                    "Excludes leading U (or U after D/d) from MCC only; moves and turn count stay unchanged."));
    aufHint->setWordWrap(true);
    auto hintPal = aufHint->palette();
    hintPal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
    aufHint->setPalette(hintPal);
    aufHint->setStyleSheet("font-size: 12px;");
    root->addWidget(aufHint);

    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                      QDialogButtonBox::RestoreDefaults);
    connect(btns->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dlg, [&] {
        const MccParams def{};
        const std::array<double, 10> vals = {def.wrist_mult,    def.push_mult,   def.ring_mult,
                                             def.destabilize,   def.add_regrip,  def.double_mult,
                                             def.se_slice_mult, def.overwork_mult, def.moveblock,
                                             def.rotation};
        for (std::size_t i = 0; i < boxes.size(); ++i)
            boxes[i]->setValue(vals[i]);
        ignoreAufBox->setChecked(true);
    });
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    root->addWidget(btns);

    if (dlg.exec() == QDialog::Accepted) {
        for (std::size_t i = 0; i < fields.size(); ++i)
            *fields[i].target = boxes[i]->value();
        const bool nextIgnoreAuf = ignoreAufBox->isChecked();
        if (!(mccParams_ == edit) || mccIgnoreAuf_ != nextIgnoreAuf) {
            mccParams_ = edit;
            mccIgnoreAuf_ = nextIgnoreAuf;
            recomputeAllMcc();
            if (!results_.empty())
                rebuildOutput();
        }
    }
}

void FormulaWindow::recomputeAllMcc() {
    for (auto &r : results_)
        r.mcc = compute_mcc(r.moves, mccParams_, mccIgnoreAuf_);
    updateStatusLine();
}

std::vector<std::size_t> FormulaWindow::displayOrder() const {
    std::vector<std::size_t> order(results_.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    const int mode = mccSort_ ? mccSort_->currentIndex() : 0;
    if (mode == 1) {
        // Moves ascending -> MCC ascending -> original arrival index
        std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
            if (results_[a].turns != results_[b].turns)
                return results_[a].turns < results_[b].turns;
            const double ma = results_[a].mcc >= 0.0 ? results_[a].mcc : 1e9;
            const double mb = results_[b].mcc >= 0.0 ? results_[b].mcc : 1e9;
            if (ma != mb)
                return ma < mb;
            return a < b;
        });
    } else if (mode == 2) {
        // MCC ascending -> Moves ascending -> original arrival index
        std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
            const double ma = results_[a].mcc >= 0.0 ? results_[a].mcc : 1e9;
            const double mb = results_[b].mcc >= 0.0 ? results_[b].mcc : 1e9;
            if (ma != mb)
                return ma < mb;
            if (results_[a].turns != results_[b].turns)
                return results_[a].turns < results_[b].turns;
            return a < b;
        });
    }
    return order;
}

QString FormulaWindow::formatResultLine(const ResultData &data) const {
    const auto moves = qstr(format_moves(data.moves, true));
    QString line = QString("%1%2(%3f%4)")
                       .arg(moves)
                       .arg(moves.isEmpty() ? "" : " ")
                       .arg(data.turns)
                       .arg(data.optimalityProven ? "*" : "");
    if (data.mcc >= 0.0)
        line += QString("  [MCC %1]").arg(data.mcc, 0, 'f', 1);
    return line;
}

void FormulaWindow::rebuildOutput() {
    QStringList lines;
    if (!preambleText_.isEmpty())
        lines << preambleText_ << "";
    const auto order = displayOrder();
    const int mode = mccSort_ ? mccSort_->currentIndex() : 0;
    int lastDepth = -1;
    for (std::size_t idx : order) {
        const auto &r = results_[idx];
        if (mode == 1 && r.turns != lastDepth) {
            if (lastDepth >= 0)
                lines << "";
            lines << trUi("—— 深度 %1（按 MCC 排序）——", "—— Depth %1 (by MCC) ——").arg(r.turns);
            lastDepth = r.turns;
        }
        lines << formatResultLine(r);
    }
    if (!epilogueText_.isEmpty())
        lines << "" << epilogueText_;
    output_->setPlainText(lines.join('\n'));
}

void FormulaWindow::updateButtonIcons() {
    const int iconPx = qRound(16 * uiScale_);
    const QColor ink = palette().color(QPalette::ButtonText);
    const QColor startInk = busy_ ? QColor("#ffffff") : palette().color(QPalette::Link);
    start_->setIcon(uiIcon(busy_ ? UiIcon::Stop : UiIcon::Play, startInk, iconPx));
    clear_->setIcon(uiIcon(UiIcon::Clear, ink, iconPx));
    add_->setIcon(uiIcon(UiIcon::AddToMain, ink, iconPx));
    for (auto *button : {start_, clear_, add_})
        button->setIconSize(QSize(iconPx, iconPx));
}

void FormulaWindow::refreshAppearance() {
    updateButtonIcons();
}

void FormulaWindow::setModuloU(bool enabled) {
    if (modU_)
        modU_->setChecked(enabled);
}

void FormulaWindow::setMovePowerEnabled(int symbolIndex, int powerIndex, bool enabled) {
    if (symbolIndex < 0 || symbolIndex >= 18 || powerIndex < 0 || powerIndex >= 3)
        return;
    movePowerEnabled_[symbolIndex][powerIndex] = enabled;
    updateMoveBoxDecoration(symbolIndex);
}

void FormulaWindow::setMaxSymbolUses(int symbolIndex, int limit) {
    if (symbolIndex < 0 || symbolIndex >= 18)
        return;
    maxSymbolUses_[symbolIndex] = std::max(-1, limit);
    updateMoveBoxDecoration(symbolIndex);
}

void FormulaWindow::setMaxRotations(int limit) {
    if (maxRotations_)
        maxRotations_->setValue(std::clamp(limit, -1, 20));
}

void FormulaWindow::setMccSortMode(int mode) {
    if (mccSort_)
        mccSort_->setCurrentIndex(std::clamp(mode, 0, 2));
}

void FormulaWindow::retranslateUi() {
    setWindowTitle(generate_ ? trUi("公式窗口 · 生成当前情况", "Formula Window · Generate")
                             : trUi("公式窗口 · 求解当前情况", "Formula Window · Solve"));
    output_->setPlaceholderText(
        trUi("尚未开始搜索。选择允许使用的转动，然后点击“开始搜索”。",
             "Ready to search. Select allowed moves and click \"Start Search\"."));
    moveGroup_->setTitle(trUi("允许使用的转动（右键可禁单角度）", "Allowed Moves (Right-click for angles)"));
    for (int i = 0; i < 18; ++i)
        updateMoveBoxDecoration(i);
    categoryBoxes_[0]->setText(trUi("允许中层转动", "Allow Slice Moves"));
    categoryBoxes_[1]->setText(trUi("允许双层转动", "Allow Wide Moves"));
    categoryBoxes_[2]->setText(trUi("允许整体旋转", "Allow Rotations"));
    start_->setText(stopping_ ? trUi("停止中…", "Stopping…")
                    : busy_   ? trUi("停止搜索", "Stop Search")
                              : trUi("开始搜索", "Start Search"));
    clear_->setText(trUi("清空窗口", "Clear Window"));
    add_->setText(trUi("将解添加到主窗口", "Add to Main Window"));
    add_->setToolTip(trUi("把窗口内的全部结果按当前顺序加入主窗口；再次点击会再次加入全部结果，沿用原版行为。",
                          "Copy all verified results (in current order) into the main window."));
    fold_->setText(trUi("搜索设置", "Search Settings"));
    mccSort_->setItemText(0, trUi("搜索顺序", "Search Order"));
    mccSort_->setItemText(1, trUi("步数 → MCC", "Moves → MCC"));
    mccSort_->setItemText(2, trUi("MCC → 步数", "MCC → Moves"));
    mccSort_->setToolTip(trUi("不改变搜索算法，按选定口径对当前结果实时无损重排。",
                              "Losslessly re-sorts current results without changing search semantics."));
    mccParamsBtn_->setText(trUi("MCC 设置", "MCC"));
    mccParamsBtn_->setToolTip(trUi("调整 MCC 手感参数与“去起手 U”开关",
                                   "Configure MCC ergonomics parameters and pre-AUF option"));
    powersLabel_->setText(trUi("转动角度", "Turn Angles"));
    powerBoxes_[0]->setText("90°");
    powerBoxes_[1]->setText("180°");
    powerBoxes_[2]->setText(trUi("逆向 90°", "90° CCW"));
    fineMovesBtn_->setText(trUi("精细动作…", "Fine Moves…"));
    fineMovesBtn_->setToolTip(trUi("自由禁用任意单个动作（如 F2、L2）或限制某层最多出现次数",
                                   "Individually ban specific moves (e.g. F2, L2) or set per-layer max uses"));
    minLabel_->setText(trUi("最少步数", "Min Moves"));
    maxLabel_->setText(trUi("最多步数", "Max Moves"));
    resLabel_->setText(trUi("结果上限", "Max Results"));
    maxRotLabel_->setText(trUi("最大转体", "Max Rotations"));
    timeLabel_->setText(trUi("时间 / 秒", "Time (s)"));
    nodesLabel_->setText(trUi("节点上限", "Node Limit"));
    heurLabel_->setText(trUi("剪枝", "Pruning"));
    const QString unlimited = trUi("不限", "Unlimited");
    maximum_->setSpecialValueText(unlimited);
    maximum_->setToolTip(trUi("-1 表示持续逐层搜索，直到停止；0 仅搜索零步。",
                              "-1 searches depth by depth until stopped; 0 checks 0-move only."));
    resultLimit_->setSpecialValueText(unlimited);
    maxRotations_->setSpecialValueText(unlimited);
    maxRotations_->setToolTip(trUi("仅在勾选“允许整体旋转”时生效；-1 表示不限，1 表示最多转体 1 次。",
                                   "Active only when Rotations are enabled; -1 = unlimited."));
    timeLimit_->setSpecialValueText(unlimited);
    nodes_->setSpecialValueText(unlimited);
    heuristic_->setItemText(0, trUi("标准剪枝", "Standard Pruning"));
    heuristic_->setItemText(1, trUi("强化底层剪枝", "Strong Bottom Pruning"));
    heuristic_->setItemText(2, trUi("无剪枝（研究对照）", "No Pruning (Reference)"));
    modU_->setText(trUi("模 U：仅差 U / U' / U2 即完成", "Mod-U: Allow final U / U' / U2"));
    symmetry_->setText(trUi("U 对称时省去冗余首步", "Prune U-symmetric first moves"));
    dedup_->setText(trUi("合并可交换动作的重复公式", "Deduplicate commuting moves"));
    triples_->setText(trUi("禁止 R r R 类交替三连", "Reject R r R alternating triples"));
    memoize_->setText(trUi("缓存已证明无解的搜索分支", "Memoize dead-end branches"));
    rotationQuotient_->setText(trUi("合并免费旋转的等价状态", "Quotient free rotation states"));
    memoize_->setToolTip(trUi("只缓存完整搜索且没有合格结果的分支，不删不同的成功公式。",
                              "Caches only proven dead-end branches without dropping valid formulas."));
    rotationQuotient_->setToolTip(
        trUi("仅在核心证明动作集合适用时启用；需同时启用无解分支缓存。",
             "Enabled when move set permits; requires dead-end branch memoization."));
    updateButtonIcons();
}

void FormulaWindow::configureState(bool busy) {
    busy_ = busy;
    refreshControls();
    start_->setText(busy ? trUi("停止搜索", "Stop Search") : trUi("开始搜索", "Start Search"));
    start_->setEnabled(true);
    start_->setProperty("running", busy);
    start_->style()->unpolish(start_);
    start_->style()->polish(start_);
    if (!busy)
        stopping_ = false;
    updateButtonIcons();
    if (onBusy)
        onBusy(busy);
}

void FormulaWindow::appendResult(ResultData data) {
    data.mcc = compute_mcc(data.moves, mccParams_, mccIgnoreAuf_);
    results_.push_back(std::move(data));
    if (!mccSort_ || mccSort_->currentIndex() == 0) {
        output_->appendPlainText(formatResultLine(results_.back()));
    } else if (results_.size() <= 1000) {
        const auto now = std::chrono::steady_clock::now();
        if (results_.size() == 1 || now - lastLiveSort_ >= std::chrono::milliseconds(400)) {
            lastLiveSort_ = now;
            rebuildOutput();
        }
    }
    updateStatusLine();
    add_->setEnabled(true);
}

void FormulaWindow::clearResults() {
    if (busy_)
        return;
    results_.clear();
    output_->clear();
    statusText_.clear();
    statisticsText_.clear();
    preambleText_.clear();
    epilogueText_.clear();
    lastReport_.reset();
    output_->setToolTip({});
    add_->setEnabled(true);
}

void FormulaWindow::addResults() {
    if (!sink_)
        return;
    // BAddClick never clears ManInfo and copies every line on every click,
    // respecting the currently selected MCC sort order.
    for (std::size_t idx : displayOrder())
        sink_(results_[idx]);
}

void FormulaWindow::smokeConfigure(PatternState input, int maximum) {
    if (busy_)
        return;
    input_ = std::move(input);
    maximum_->setValue(maximum);
    minimum_->setValue(0);
    heuristic_->setCurrentIndex(0);
}

void FormulaWindow::stopSearch() {
    if (!busy_ || stopping_.exchange(true))
        return;
    worker_.request_stop();
    start_->setText(trUi("停止中…", "Stopping…"));
    start_->setEnabled(false);
    setStatus(trUi("正在停止搜索，等待当前任务退出…", "Stopping search, waiting for worker…"));
}

void FormulaWindow::setStatus(QString message) {
    statusText_ = std::move(message);
    updateStatusLine();
}

void FormulaWindow::updateStatusLine() {
    QString header = statusText_;
    header.replace('\n', ' ');
    if (!statisticsText_.isEmpty())
        header += " · " + statisticsText_;
    header += QString(" · 已验证 %1 条").arg(results_.size());
    if (!results_.empty()) {
        double bestMcc = std::numeric_limits<double>::infinity();
        for (const auto &r : results_)
            if (r.mcc >= 0.0 && r.mcc < bestMcc)
                bestMcc = r.mcc;
        if (std::isfinite(bestMcc))
            header += QString(" · 最佳 MCC %1").arg(bestMcc, 0, 'f', 1);
    }
    output_->setToolTip(header);
}

void FormulaWindow::appendMessage(const QString &message) {
    if (!output_->document()->isEmpty() && !output_->document()->lastBlock().text().isEmpty())
        output_->appendPlainText({});
    output_->appendPlainText(message);
    output_->appendPlainText({});
}

bool FormulaWindow::postAndWait(std::uint64_t generation, std::stop_token stop,
                                std::function<void()> action) {
    if (stop.stop_requested())
        return false;
    auto delivery = std::make_shared<FormulaDelivery>();
    QMetaObject::invokeMethod(
        this,
        [this, generation, delivery, action = std::move(action)] {
            try {
                if (generation == generation_)
                    action();
            } catch (...) {
                delivery->error = std::current_exception();
            }
            {
                const std::lock_guard lock(delivery->mutex);
                delivery->delivered = true;
            }
            delivery->ready.notify_all();
        },
        Qt::QueuedConnection);
    std::unique_lock lock(delivery->mutex);
    const bool delivered = delivery->ready.wait(lock, stop, [&] { return delivery->delivered; });
    if (delivered && delivery->error)
        std::rethrow_exception(delivery->error);
    return delivered;
}

void FormulaWindow::finish(std::uint64_t generation, QString message, std::optional<PatternReport> report) {
    post(generation, [this, message = std::move(message), report = std::move(report)] {
        if (worker_.joinable())
            worker_.join();
        configureState(false);
        lastReport_ = report;
        epilogueText_ = message;
        if (mccSort_ && mccSort_->currentIndex() != 0 && !results_.empty())
            rebuildOutput();
        else
            appendMessage(message);
        setStatus(message);
        const auto finished = onFinished;
        QPointer<FormulaWindow> guard(this);
        if (finished)
            finished();
        if (guard && closePending_)
            close();
    });
}

void FormulaWindow::closeEvent(QCloseEvent *event) {
    if (busy_) {
        closePending_ = true;
        stopSearch();
        event->ignore();
        return;
    }
    closePending_ = false;
    clearResults();
    event->accept();
}

void FormulaWindow::reject() {
    close();
}

PatternRequest FormulaWindow::request() const {
    PatternRequest request;
    request.pattern = input_;
    request.direction = generate_ ? PatternDirection::Generate : PatternDirection::Solve;
    request.allowed_moves.clear();
    for (int m = 0; m < 18; ++m) {
        const bool categoryAllowed = m < 6 || categoryBoxes_[m < 12 ? 1 : m < 15 ? 0 : 2]->isChecked();
        if (categoryAllowed && moveBoxes_[m]->isChecked())
            for (int p = 0; p < 3; ++p)
                if (powerBoxes_[p]->isChecked() && movePowerEnabled_[m][p])
                    request.allowed_moves.push_back({move_names[m], static_cast<std::uint8_t>(p + 1)});
    }
    request.min_turns = minimum_->value();
    request.max_turns = maximum_->value();
    request.max_results = static_cast<std::size_t>(resultLimit_->value());
    request.max_rotations = (categoryBoxes_[2]->isChecked() && maxRotations_) ? maxRotations_->value() : -1;
    request.max_symbol_uses = maxSymbolUses_;
    request.time_limit = std::chrono::seconds(timeLimit_->value());
    request.max_nodes = static_cast<std::uint64_t>(nodes_->value());
    request.collect_solutions = false;
    request.modulo_u = modU_->isChecked();
    request.remove_u_symmetric_prefix = symmetry_->isChecked();
    request.deduplicate_commuting = dedup_->isChecked();
    request.reject_alternating_triples = triples_->isChecked();
    request.memoize_dead_ends = memoize_->isChecked();
    request.quotient_free_rotations = rotationQuotient_->isChecked();
    request.heuristic = heuristic_->currentIndex() == 0   ? PatternHeuristic::Standard
                        : heuristic_->currentIndex() == 1 ? PatternHeuristic::Strong
                                                          : PatternHeuristic::None;
    return request;
}

void FormulaWindow::startSearch() {
    if (busy_)
        return;
    if (maximum_->value() >= 0 && minimum_->value() > maximum_->value()) {
        QMessageBox::warning(this, "搜索设置", "最少步数不能大于最多步数。");
        return;
    }
    auto request = this->request();
    if (worker_.joinable())
        worker_.join();
    closePending_ = false;
    configureState(true);
    lastReport_.reset();
    statisticsText_.clear();
    epilogueText_.clear();
    // Match the original accelerator/start preamble without naming a PDB that
    // this implementation does not use. A zero-turn request skips all tables.
    const QString accelerator = request.heuristic == PatternHeuristic::None || request.max_turns == 0
                                    ? "不使用加速表。"
                                : request.heuristic == PatternHeuristic::Strong ? "使用加速表（强化底层）。"
                                                                                : "使用加速表。";
    preambleText_ = accelerator + "\n计算开始…";
    appendMessage(preambleText_);
    setStatus(generate_ ? "生成方向：正在准备搜索…" : "求解方向：正在准备搜索…");
    const auto generation = ++generation_;
    worker_ = std::jthread([this, request, generation](std::stop_token stop) mutable {
        try {
            request.cancelled = [stop] { return stop.stop_requested(); };
            const auto report = enumerate_pattern(
                request,
                [this, &request, generation, stop](const PatternSolution &solution) {
                    ResultData data;
                    data.target = request.pattern;
                    data.start = request.pattern;
                    data.terminal = solution.terminal;
                    data.moves = solution.moves;
                    data.turns = solution.turn_cost;
                    data.rotations = solution.rotations;
                    data.generate = request.direction == PatternDirection::Generate;
                    data.optimalityProven = solution.optimality_proven;
                    if (data.generate) {
                        CubeState initial;
                        initial.frame = request.pattern.frame;
                        data.start = pattern_from_cube(initial);
                        for (int face = 0; face < 6; ++face)
                            if (request.pattern.center_ori[face] >= 0)
                                data.start.center_ori[face] = 0;
                    }
                    postAndWait(generation, stop,
                                [this, data = std::move(data)]() mutable { appendResult(std::move(data)); });
                },
                [this, generation, stop](const PatternProgress &progress) {
                    postAndWait(generation, stop, [this, progress] {
                        if (progress.depth_started) {
                            if (mccSort_ && mccSort_->currentIndex() != 0 && !results_.empty())
                                rebuildOutput();
                            appendMessage(QString("正在搜索深度 %1").arg(progress.depth));
                        }
                        statisticsText_ = QString("%1 步 · %2 个节点 · %3 秒")
                                              .arg(progress.depth)
                                              .arg(progress.nodes)
                                              .arg(progress.elapsed.count() / 1000.0, 0, 'f', 1);
                        if (!stopping_)
                            setStatus(progress.preparing ? "正在准备搜索…" : "正在枚举公式…");
                        else
                            updateStatusLine();
                    });
                });
            finish(generation,
                   (report.stop == PatternStop::Cancelled   ? "计算停止。"
                    : report.stop == PatternStop::Exhausted ? "计算完成。"
                                                            : stopText(report.stop)) +
                       (report.message.empty() ? QString{} : "\n" + friendlyError(report.message)),
                   report);
        } catch (const std::exception &error) {
            finish(generation, stop.stop_requested()
                                   ? "计算停止。"
                                   : QString("搜索失败：%1").arg(friendlyError(error.what())));
        }
    });
}

void FormulaWindow::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (!scaling_ && start_)
        setUiScale(std::clamp(std::min(width() / 610.0, height() / 740.0), .75, 2.0));
}

void FormulaWindow::setUiScale(double scale) {
    if (scaling_)
        return;
    scaling_ = true;
    uiScale_ = std::clamp(scale, .75, 2.0);
    const auto px = [this](int value) { return qRound(value * uiScale_); };
    setStyleSheet(QString("QDialog#formulaWindow, QDialog#formulaWindow QWidget { "
                          "font-family:'Segoe UI','Microsoft YaHei UI','Microsoft YaHei'; font-size:%1px; } "
                          "QDialog#formulaWindow QGroupBox { margin-top:%2px; padding:%2px %3px %3px; } "
                          "QDialog#formulaWindow QPushButton, QDialog#formulaWindow QToolButton { "
                          "padding:%4px %5px; min-height:%6px; } "
                          "QDialog#formulaWindow QPlainTextEdit { padding:%4px %5px; min-height:0; "
                          "font-family:'Consolas','Microsoft YaHei UI','Microsoft YaHei'; font-size:%8px; } "
                          "QDialog#formulaWindow QCheckBox { spacing:%3px; } "
                          "QDialog#formulaWindow QCheckBox::indicator { width:%7px; height:%7px; }")
                      .arg(px(14))
                      .arg(px(9))
                      .arg(px(6))
                      .arg(px(4))
                      .arg(px(8))
                      .arg(px(20))
                      .arg(px(13))
                      .arg(px(15)));
    for (auto *button : {start_, clear_, add_})
        button->setFixedSize(px(240), px(50));
    if (fold_)
        fold_->setIconSize(QSize(px(16), px(16)));
    updateButtonIcons();
    for (auto *box : {minimum_, maximum_, resultLimit_, maxRotations_, timeLimit_}) {
        box->setMinimumWidth(px(75));
        box->setMaximumWidth(px(130));
        box->setMinimumHeight(px(27));
    }
    nodes_->setMaximumWidth(px(150));
    nodes_->setMinimumHeight(px(27));
    heuristic_->setMinimumHeight(px(27));
    if (mccSort_)
        mccSort_->setMinimumHeight(px(24));
    for (const auto &metric : layoutMetrics_) {
        metric.layout->setContentsMargins(px(metric.margins.left()), px(metric.margins.top()),
                                          px(metric.margins.right()), px(metric.margins.bottom()));
        metric.layout->setSpacing(px(std::max(0, metric.spacing)));
        if (auto *grid = qobject_cast<QGridLayout *>(metric.layout)) {
            grid->setHorizontalSpacing(px(std::max(0, metric.horizontal)));
            grid->setVerticalSpacing(px(std::max(0, metric.vertical)));
        }
        if (auto *form = qobject_cast<QFormLayout *>(metric.layout)) {
            form->setHorizontalSpacing(px(std::max(0, metric.horizontal)));
            form->setVerticalSpacing(px(std::max(0, metric.vertical)));
        }
    }
    QFont font;
    font.setFamilies({"Consolas", "Microsoft YaHei UI", "Microsoft YaHei"});
    font.setPixelSize(px(15));
    output_->setFont(font);
    scaling_ = false;
}
