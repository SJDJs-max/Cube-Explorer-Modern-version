#pragma once
#include "app_common.hpp"
#include "cube_canvas.hpp"
#include "cube/optimal.hpp"
#include "ui_i18n.hpp"

// One original-style result entry owns its input and its independent searches.
// Switching the optimal checkbox never relabels a two-phase result as optimal.
class ResultRow final : public QFrame {
    PatternState input_;
    bool formula_ = false, generate_ = false, busy_ = false, selected_ = false;
    int number_ = 0;
    std::optional<ResultData> ordinary_, optimal_;
    std::vector<std::string> excluded_;
    std::jthread worker_;
    std::uint64_t generation_ = 0;
    double uiScale_ = 1.0;
    CubeCanvas *thumbnail_;
    QLabel *numberLabel_, *formulaLabel_, *nameLabel_;
    QString statusText_ = "状态：尚未运行";
    QLineEdit *name_;
    QCheckBox *optimalBox_;
    QToolButton *run_ = nullptr, *direction_;
    static QIcon runIcon(bool busy, double scale = 1.0) {
        QPixmap image(qRound(48 * scale), qRound(48 * scale));
        image.setDevicePixelRatio(2);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(4.0 / 3.0 * scale, 4.0 / 3.0 * scale);
        if (busy) {
            painter.setPen(QPen(QColor("#b03a37"), 0.85));
            painter.setBrush(QColor("#d9534f"));
            painter.drawRoundedRect(QRectF(4.2, 4.2, 9.6, 9.6), 1.8, 1.8);
        } else {
            QLinearGradient green(4, 2.5, 14, 15.5);
            green.setColorAt(0, QColor("#39c466"));
            green.setColorAt(1, QColor("#1e9649"));
            painter.setBrush(green);
            painter.setPen(QPen(QColor("#197c3c"), 0.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolygon(QPolygonF{QPointF(5.2, 2.8), QPointF(14.2, 9.0), QPointF(5.2, 15.2)});
        }
        return QIcon(image);
    }
    void setStatus(QString status) {
        statusText_ = std::move(status);
        render();
    }
    QRectF cubeHitRect() const {
        QRectF bounds;
        for (int sticker = 0; sticker < 54; ++sticker)
            bounds = bounds.united(thumbnail_->stickerPolygon(sticker).boundingRect());
        return bounds.translated(thumbnail_->mapTo(this, QPoint{}));
    }
    void stopAndJoin() {
        // Queued updates from the previous input must not touch a replacement.
        ++generation_;
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        setBusy(false);
    }
    template <class F> void post(std::uint64_t generation, F fn) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, fn = std::move(fn)]() mutable {
                if (generation == generation_)
                    fn();
            },
            Qt::QueuedConnection);
    }
    void setBusy(bool busy) {
        busy_ = busy;
        run_->setIcon(runIcon(busy, uiScale_));
        run_->setToolTip(busy ? "停止搜索" : "继续寻找下一条公式");
        if (onBusy)
            onBusy();
    }
    void finish(std::uint64_t generation, QString status) {
        post(generation, [this, status] {
            if (worker_.joinable())
                worker_.join();
            setBusy(false);
            setStatus(status);
            if (onFinished)
                onFinished();
        });
    }
    ResultData ordinaryData(const CubeState &input, std::span<const Move> moves, bool proof = false) const {
        ResultData d;
        d.target = input_;
        d.start = input_;
        d.moves.assign(moves.begin(), moves.end());
        d.formula = false;
        d.generate = false;
        d.turns = turn_count(moves);
        d.rotations = rotation_count(moves);
        d.terminal = pattern_from_cube(cube::apply(input, moves));
        d.optimalityProven = proof;
        return d;
    }
    void layoutChildren() {
        if (!run_)
            return;
        // Keep the original controls, with aligned labels and a compact row.
        // Qt applies monitor DPI after this logical content scale.
        const auto px = [this](double value) { return qRound(value * uiScale_); };
        const int formulaWidth = std::max(px(50), width() - px(240) - px(12));
        formulaLabel_->ensurePolished();
        const int formulaHeight =
            std::max(formulaLabel_->fontMetrics().height(), formulaLabel_->heightForWidth(formulaWidth));
        const int extra = std::max(0, formulaHeight - px(42));
        const int bottom = px(64) + extra;
        thumbnail_->setGeometry(px(8), px(10), px(168), px(122));
        numberLabel_->setGeometry(px(5), px(10), px(40), px(24));
        direction_->setGeometry(px(186), px(10), px(50), px(28));
        formulaLabel_->setGeometry(px(240), px(14), formulaWidth, formulaHeight);
        nameLabel_->setGeometry(px(186), bottom, px(50), px(30));
        name_->setGeometry(px(240), bottom, px(210), px(30));
        optimalBox_->setGeometry(px(466), bottom, px(80), px(30));
        run_->setGeometry(px(552), bottom - px(2), px(30), px(30));
        if (height() != px(140) + extra)
            setFixedHeight(px(140) + extra);
    }
    void render() {
        direction_->setText(generate_ ? trUi("生成：", "Gen:") : trUi("求解：", "Solve:"));
        nameLabel_->setText(trUi("名称：", "Name:"));
        optimalBox_->setText(trUi("最优", "Opt"));
        const auto d = result();
        if (!d) {
            formulaLabel_->setText(statusText_);
            formulaLabel_->setToolTip(statusText_);
            setToolTip(statusText_);
            layoutChildren();
            return;
        }
        const auto text = qstr(format_moves(d->moves, d->formula));
        QString displayed = text + (text.isEmpty() ? "" : "  ") +
                            QString("(%1f%2)").arg(d->turns).arg(d->optimalityProven ? "*" : "");
        if (d->formula && d->mcc >= 0.0)
            displayed += QString("  [MCC %1]").arg(d->mcc, 0, 'f', 1);
        formulaLabel_->setText(displayed);
        layoutChildren();
        QString tip = trUi("%1 步 · %2", "%1 moves · %2")
                          .arg(d->turns)
                          .arg(d->optimalityProven ? trUi("已证明最优", "Optimal proven")
                                                   : trUi("已独立回放验证", "Independently verified"));
        if (d->formula && d->mcc >= 0.0)
            tip += QString(" · MCC %1").arg(d->mcc, 0, 'f', 1);
        formulaLabel_->setToolTip(tip + "\n" + statusText_);
        setToolTip(statusText_);
    }

  protected:
    void changeEvent(QEvent *event) override {
        QFrame::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            layoutChildren();
    }
    void resizeEvent(QResizeEvent *event) override {
        QFrame::resizeEvent(event);
        layoutChildren();
    }
    void paintEvent(QPaintEvent *event) override {
        QFrame::paintEvent(event);
        if (selected_) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);
            const auto accent = palette().color(QPalette::Highlight);
            QColor fill = accent;
            fill.setAlpha(20);
            painter.setPen(QPen(accent, std::max(1.2, 1.5 * uiScale_)));
            painter.setBrush(fill);
            painter.drawRoundedRect(
                cubeHitRect().adjusted(-2.5 * uiScale_, -2.5 * uiScale_, 2.5 * uiScale_, 2.5 * uiScale_),
                3.0 * uiScale_, 3.0 * uiScale_);
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus(Qt::MouseFocusReason);
        if (cubeHitRect().contains(event->position())) {
            if (event->button() == Qt::RightButton) {
                if (onContext)
                    onContext(this, event->globalPosition().toPoint());
            } else if (onSelect)
                onSelect(this, event->modifiers());
        } else if (event->button() == Qt::LeftButton) {
            if (onClearSelection)
                onClearSelection();
        } else if (event->button() == Qt::RightButton && onBackgroundContext)
            onBackgroundContext(event->globalPosition().toPoint());
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        // The source output paintbox handles a second click as a normal click.
        mousePressEvent(event);
    }

  public:
    std::function<void(ResultRow *, Qt::KeyboardModifiers)> onSelect;
    std::function<void(ResultRow *, QPoint)> onContext;
    std::function<void()> onClearSelection;
    std::function<void(QPoint)> onBackgroundContext;
    std::function<void(PatternState)> onLoad;
    std::function<void(ResultData)> onReplay;
    std::function<void()> onRun, onBusy, onFinished;
    ResultRow(PatternState input, bool generate, bool formula = false, QWidget *parent = nullptr)
        : QFrame(parent), input_(std::move(input)), formula_(formula), generate_(generate) {
        setObjectName("resultRow");
        setFrameShape(QFrame::NoFrame);
        setFocusPolicy(Qt::ClickFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(590);
        setFixedHeight(140);
        thumbnail_ = new CubeCanvas(true, this);
        thumbnail_->setView(CubeCanvas::View::Classic);
        thumbnail_->setDrawingMargin(0);
        thumbnail_->setPattern(input_);
        thumbnail_->setAttribute(Qt::WA_TransparentForMouseEvents);
        numberLabel_ = new QLabel(this);
        numberLabel_->setObjectName("muted");
        numberLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        direction_ = new QToolButton(this);
        direction_->setObjectName("resultDirection");
        direction_->setAutoRaise(true);
        direction_->setCursor(Qt::PointingHandCursor);
        direction_->setToolTip("切换显示求解 / 生成公式");
        direction_->setEnabled(!formula_);
        formulaLabel_ = new QLabel(this);
        formulaLabel_->setObjectName("formulaText");
        formulaLabel_->setWordWrap(true);
        formulaLabel_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        formulaLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        nameLabel_ = new QLabel("名称：", this);
        nameLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        nameLabel_->setAttribute(Qt::WA_TransparentForMouseEvents);
        name_ = new QLineEdit(this);
        name_->setMaxLength(30);
        optimalBox_ = new QCheckBox("最优", this);
        optimalBox_->setVisible(!formula_);
        optimalBox_->setToolTip("使用原版三轴下界的精确最优搜索；勾选后点击运行箭头");
        run_ = new QToolButton(this);
        run_->setObjectName("resultRun");
        run_->setAutoRaise(true);
        run_->setIconSize(QSize(24, 24));
        run_->setIcon(runIcon(false));
        run_->setToolTip("继续寻找下一条公式");
        run_->setVisible(!formula_);
        connect(run_, &QToolButton::clicked, this, [this] {
            if (busy_)
                stop();
            else if (onRun) {
                if (wantsOptimal() && optimal_ && optimal_->optimalityProven &&
                    QMessageBox::question(
                        this, "公式窗口",
                        "当前公式已证明最优，继续搜索得到的下一条公式可能不再最优。是否继续？",
                        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                    return;
                onRun();
            }
        });
        connect(optimalBox_, &QCheckBox::toggled, this, [this] {
            stop();
            if (!optimalBox_->isChecked() && optimal_ && optimal_->optimalityProven) {
                QSignalBlocker blocked(optimalBox_);
                optimalBox_->setChecked(true);
            }
            render();
        });
        connect(direction_, &QToolButton::clicked, this, [this] {
            generate_ = !generate_;
            render();
        });
        setUiScale(1.0);
        render();
    }
    ~ResultRow() override {
        stop();
        if (worker_.joinable())
            worker_.join();
    }
    void retranslateUi() {
        direction_->setToolTip(trUi("切换显示求解 / 生成公式", "Toggle Solve / Generator display"));
        optimalBox_->setToolTip(trUi("使用原版三轴下界的精确最优搜索；勾选后点击运行箭头",
                                     "Exact optimal solver with 3-axis bounds; check and click Run"));
        run_->setToolTip(busy_ ? trUi("停止搜索", "Stop search") : trUi("继续寻找下一条公式", "Find next solution"));
        if (!result() && (statusText_ == "状态：尚未运行" || statusText_ == "Status: Idle"))
            statusText_ = trUi("状态：尚未运行", "Status: Idle");
        render();
    }
    void setUiScale(double scale) {
        uiScale_ = std::clamp(scale, .75, 2.0);
        const auto px = [this](double value) { return qRound(value * uiScale_); };
        setMinimumWidth(px(590));
        setStyleSheet(QString("QFrame#resultRow { font-family:'Segoe UI','Microsoft YaHei UI','Microsoft YaHei'; } "
                              "QFrame#resultRow QLabel, QFrame#resultRow QLineEdit, "
                              "QFrame#resultRow QCheckBox, QFrame#resultRow QToolButton { font-size:%1px; } "
                              "QFrame#resultRow QLabel#formulaText { font-family:'Consolas','Segoe UI','Microsoft YaHei UI'; font-size:%1px; } "
                              "QFrame#resultRow QLabel#muted { font-size:%2px; } "
                              "QFrame#resultRow QLineEdit { padding:%3px %4px; min-height:%5px; } "
                              "QFrame#resultRow QCheckBox { spacing:%4px; } "
                              "QFrame#resultRow QCheckBox::indicator { width:%6px; height:%6px; }")
                          .arg(px(15))
                          .arg(px(13))
                          .arg(px(3))
                          .arg(px(6))
                          .arg(px(19))
                          .arg(px(13)));
        direction_->setStyleSheet(QString("QToolButton { padding:0; min-height:%1px; font-size:%2px; }")
                                      .arg(px(20))
                                      .arg(px(15)));
        run_->setStyleSheet(QString("QToolButton { padding:0; min-height:%1px; }").arg(px(20)));
        run_->setIconSize(QSize(px(24), px(24)));
        run_->setIcon(runIcon(busy_, uiScale_));
        layoutChildren();
    }
    double uiScale() const noexcept {
        return uiScale_;
    }
    void setNumber(int n) {
        number_ = n;
        numberLabel_->setText(QString::number(n) + ".");
    }
    int number() const {
        return number_;
    }
    CubeCanvas *thumbnail() const {
        return thumbnail_;
    }
    const PatternState &input() const {
        return input_;
    }
    void replaceInput(PatternState input, bool resetControls = true) {
        if (!formula_)
            (void)completeCube(input);
        stopAndJoin();
        input_ = std::move(input);
        ordinary_.reset();
        optimal_.reset();
        excluded_.clear();
        if (resetControls) {
            QSignalBlocker blocked(optimalBox_);
            optimalBox_->setChecked(false);
            setSelected(false);
        }
        thumbnail_->setPattern(input_);
        setStatus("状态：尚未运行");
    }
    void invertInput() {
        if (formula_)
            throw std::invalid_argument("不完整魔方结果不能使用逆置换。");
        auto inverted = completeCube(input_);
        inverted.cubies = cube::inverse(inverted.cubies);
        const auto target = pattern_from_cube(inverted);
        stopAndJoin();
        const auto transform = [&](std::optional<ResultData> data) {
            if (data) {
                data->moves = cube::inverse(data->moves);
                const auto terminal = cube::apply(inverted, data->moves);
                if (!is_solved(terminal) || !verify_sequence(inverted, data->moves, terminal))
                    throw std::logic_error("逆置换公式回放验证失败");
                data->target = target;
                data->start = target;
                data->terminal = pattern_from_cube(terminal);
                data->turns = turn_count(data->moves);
                data->rotations = rotation_count(data->moves);
            }
            return data;
        };
        auto ordinary = transform(ordinary_);
        auto optimal = transform(optimal_);
        std::vector<std::string> excluded;
        excluded.reserve(excluded_.size());
        for (const auto &formula : excluded_)
            excluded.push_back(format_moves(cube::inverse(parse_moves(formula))));
        input_ = target;
        ordinary_ = std::move(ordinary);
        optimal_ = std::move(optimal);
        excluded_ = std::move(excluded);
        thumbnail_->setPattern(input_);
        setStatus("已逆置换；已有公式均已独立回放验证");
    }
    bool isFormula() const {
        return formula_;
    }
    bool generated() const {
        return generate_;
    }
    bool busy() const {
        return busy_;
    }
    bool selected() const {
        return selected_;
    }
    void setSelected(bool selected) {
        selected_ = selected;
        setProperty("selected", selected);
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
    QString patternName() const {
        return name_->text();
    }
    void setPatternName(const QString &name) {
        name_->setText(name);
    }
    bool wantsOptimal() const {
        return optimalBox_->isChecked();
    }
    void setOptimal(bool value) {
        optimalBox_->setChecked(value);
    }
    void setAppearance(bool dark, const std::array<QColor, 7> &colors) {
        thumbnail_->setDarkMode(dark);
        thumbnail_->setColors(colors);
    }
    void setFormulaResult(ResultData data) {
        ordinary_ = std::move(data);
        setStatus(QString("已验证 · %1 步").arg(ordinary_->turns));
        render();
    }
    std::optional<ResultData> result(bool bestAvailable = false) const {
        auto data = bestAvailable                   ? (optimal_ ? optimal_ : ordinary_)
                    : (!formula_ && wantsOptimal()) ? optimal_
                                                    : ordinary_;
        if (data && !formula_) {
            data->generate = generate_;
            if (generate_) {
                data->moves = cube::inverse(data->moves);
                CubeState start;
                start.frame = input_.frame;
                data->start = pattern_from_cube(start);
                const auto end = cube::apply(start, data->moves);
                if (end.cubies != completeCube(input_).cubies || !verify_sequence(start, data->moves, end))
                    throw std::logic_error("生成公式回放验证失败");
                data->terminal = pattern_from_cube(end);
            }
        }
        return data;
    }
    void restoreResult(ResultData data) {
        if (data.formula) {
            setFormulaResult(std::move(data));
            return;
        }
        if (data.generate)
            data.moves = cube::inverse(data.moves);
        data.generate = false;
        data.start = input_;
        data.terminal = pattern_from_cube(cube::apply(completeCube(input_), data.moves));
        if (data.optimalityProven) {
            optimal_ = data;
            optimalBox_->setChecked(true);
        } else
            ordinary_ = data;
        setStatus(data.optimalityProven ? "已验证 · 已证明最优 (*)" : "已验证 · 从文件载入");
        render();
    }
    void stop() {
        if (busy_) {
            worker_.request_stop();
            run_->setToolTip("正在停止…");
            setStatus("正在停止…");
        }
    }
    void start(TwoPhaseOptions options, OptimalOptions optimum) {
        if (busy_ || formula_)
            return;
        if (worker_.joinable())
            worker_.join();
        const auto input = completeCube(input_);
        const bool optimal = wantsOptimal();
        const bool hadOrdinary = ordinary_.has_value();
        const auto generation = ++generation_;
        const auto tables = fsPath(tableDirectory());
        if (optimal) {
            optimum.excluded_solutions = excluded_;
            if (ordinary_)
                optimum.initial_solution = ordinary_->moves;
        } else if (ordinary_)
            options.max_moves = std::min(options.max_moves, std::max(0, ordinary_->turns - 1));
        setBusy(true);
        setStatus("正在准备搜索…");
        worker_ = std::jthread([this, input, options, optimum, optimal, hadOrdinary, tables,
                                generation](std::stop_token stop) {
            try {
                auto preparation = [this, generation](std::string_view) {
                    post(generation, [this] { setStatus("正在载入原版剪枝表…"); });
                };
                if (optimal) {
                    OptimalSolver solver(tables, preparation, stop);
                    auto report =
                        solver.solve(input, optimum, stop, [this, generation](const OptimalProgress &p) {
                            post(generation, [this, p] {
                                setStatus(QString("最优搜索：深度 %1 · %2 节点 · %3 秒")
                                              .arg(p.depth)
                                              .arg(p.nodes)
                                              .arg(p.elapsed.count() / 1000.0, 0, 'f', 1));
                            });
                        });
                    if (report.solved) {
                        auto data = ordinaryData(input, report.solution.moves, report.optimality_proven);
                        data.turns = report.solution.turn_count;
                        post(generation, [this, data, reason = report.stop_reason] {
                            optimal_ = data;
                            if (reason == OptimalStop::found)
                                excluded_.push_back(format_moves(data.moves));
                            render();
                        });
                    }
                    QString status = report.optimality_proven ? "已证明最优 (*)"
                                     : report.solved          ? "已有有效公式，尚未证明最优"
                                                              : "当前范围内未找到新公式";
                    if (report.stop_reason == OptimalStop::cancelled)
                        status += " · 已停止";
                    if (report.stop_reason == OptimalStop::time_limit ||
                        report.stop_reason == OptimalStop::node_limit)
                        status += " · 达到搜索限制";
                    finish(generation, status);
                } else {
                    TwoPhaseSolver solver(tables, preparation, stop);
                    auto report = solver.solve(input, options, stop,
                                               [this, input, generation](const TwoPhaseSolution &solution) {
                                                   auto data = ordinaryData(input, solution.moves);
                                                   data.turns = solution.turn_count;
                                                   post(generation, [this, data] {
                                                       ordinary_ = data;
                                                       render();
                                                   });
                                               });
                    QString status = report.solved ? "已独立回放验证"
                                     : hadOrdinary ? "当前搜索限制内未找到更短公式"
                                                   : "当前搜索限制内未找到公式";
                    if (report.stop_reason == TwoPhaseStop::cancelled)
                        status = "搜索已停止，已有公式保留";
                    finish(generation, status);
                }
            } catch (const std::exception &e) {
                finish(generation,
                       stop.stop_requested() ? "搜索已停止" : "搜索失败：" + friendlyError(e.what()));
            }
        });
    }
};
