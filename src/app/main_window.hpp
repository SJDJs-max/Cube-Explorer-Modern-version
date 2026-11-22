#pragma once
#include "app_common.hpp"
#include "app_theme.hpp"
#include "cube_canvas.hpp"
#include "editor_state.hpp"
#include "result_row.hpp"
#include "formula_window.hpp"
#include "original_editor_view.hpp"
#include "mode_choice.hpp"
#include "theme_toggle.hpp"
#include "ui_i18n.hpp"
#include "ui_icons.hpp"
#include "xcross_dialog.hpp"
#include <QKeySequenceEdit>
#include <QPrintDialog>
#include <QPageSetupDialog>
#include <QPrinter>
#include <QShortcut>
#include <QTextDocument>
#include <QScopeGuard>
#include <set>
#include <map>
#include <deque>

class MoveButton final : public QPushButton {
    char symbol_;
    bool rightDown_ = false;

  protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            rightDown_ = true;
            setText(QString(symbol_) + "'");
            setDown(true);
            e->accept();
        } else
            QPushButton::mousePressEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton && rightDown_) {
            rightDown_ = false;
            setText(QString(symbol_));
            setDown(false);
            if (onMove)
                onMove({symbol_, 3});
            e->accept();
        } else
            QPushButton::mouseReleaseEvent(e);
    }

  public:
    std::function<void(Move)> onMove;
    explicit MoveButton(char symbol, QWidget *parent = nullptr)
        : QPushButton(QString(symbol), parent), symbol_(symbol) {
        setObjectName("moveButton");
        setProperty("symbol", QString(symbol));
        setMinimumSize(35, 25);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setToolTip("左键：顺时针 90°；右键：逆时针 90°。半转可连按两次，或输入 2。");
        connect(this, &QPushButton::clicked, this, [this] {
            if (onMove)
                onMove({symbol_, 1});
        });
    }
};

class MainWindow final : public QMainWindow {
    std::string raw_ = format_facelets(solved_facelets());
    std::array<std::int8_t, 6> centers_{};
    std::array<std::int8_t, 6> rememberedCenters_{};
    // User-specified default palette, in logical URFDLB order.
    std::array<QColor, 7> colors_ = {QColor(255, 255, 255), QColor(238, 0, 0),   QColor(0, 216, 0),
                                     QColor(254, 254, 0),   QColor(255, 161, 0), QColor(0, 0, 242),
                                     QColor("#808080")};
    char brush_ = 'U';
    bool dark_ = false, english_ = false, closing_ = false;
    int currentView_ = 0;
    bool dirty_ = false;
    bool loading_ = false, cancelLoad_ = false;
    QSettings preferences_;
    QString currentFile_;
    QSplitter *split_;
    QWidget *left_, *right_;
    QVBoxLayout *ordinaryRows_;
    QScrollArea *ordinaryScroll_;
    QPointer<FormulaWindow> formulaPanel_;
    OriginalEditorView *editorView_;
    QWidget *cornerBar_ = nullptr;
    QToolButton *langToggle_ = nullptr;
    QToolButton *themeToggle_ = nullptr;
    double uiScale_ = 1.0;
    bool scalePending_ = false, scaling_ = false;
    struct LayoutScale {
        QLayout *layout;
        QMargins margins;
        int spacing;
    };
    std::vector<LayoutScale> scalableLayouts_;
    struct BilingualAction {
        QAction *action;
        const char *zh;
        const char *en;
    };
    std::vector<BilingualAction> menuTexts_;
    struct ShortcutBinding {
        QString id;
        const char *zh;
        const char *en;
        QKeySequence defaultKey;
        QKeySequence currentKey;
        std::function<void()> trigger;
        QShortcut *primary = nullptr;
        QShortcut *alt = nullptr;
    };
    std::vector<ShortcutBinding> shortcuts_;
    QMenu *fileMenu_ = nullptr, *editMenu_ = nullptr, *runMenu_ = nullptr, *optionsMenu_ = nullptr,
          *helpMenu_ = nullptr, *viewsMenu_ = nullptr, *perfMenu_ = nullptr;
    QAction *nightAction_ = nullptr, *toggleLeftAction_ = nullptr;
    std::array<QAction *, 3> viewActions_{};
    std::array<QAction *, 3> perfActions_{};
    QGroupBox *selectBox_ = nullptr, *moveBox_ = nullptr, *resetBox_ = nullptr, *inputBox_ = nullptr;
    QLineEdit *formulaInput_;
    QPushButton *solve_, *generate_;
    QPushButton *applyButton_ = nullptr, *inverseButton_ = nullptr;
    std::array<QPushButton *, 9> resetButtons_{};
    QToolButton *presetZbls_ = nullptr, *presetOll_ = nullptr, *presetF2l_ = nullptr,
                *presetOllcp_ = nullptr, *presetXCross_ = nullptr;
    std::vector<ResultRow *> rows_;
    ResultRow *anchor_ = nullptr;
    TwoPhaseOptions twoPhase_;
    OptimalOptions optimal_;
    QTimer replayTimer_, autorunTimer_;
    std::optional<ResultData> replay_;
    std::size_t replayPosition_ = 0;
    bool autoRun_ = false, autoOptimal_ = false;
    int maxThreads_ = 4;
    std::set<ResultRow *> autoAttempted_;
    std::map<ResultRow *, int> autoLengths_;
    std::size_t autoIndex_ = 0;
    std::deque<ResultRow *> generatorQueue_;
    bool pumpingGenerators_ = false;
    QPrinter printer_;
    static QString settingsFile() {
        const auto path = QApplication::instance()->property("testMode").toBool()
                              ? QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v2-test.ini"
                              : baseDirectory() + "/data/preferences.ini";
        QDir().mkpath(QFileInfo(path).absolutePath());
        return path;
    }
    void warn(const QString &message) {
        QMessageBox::warning(this, "Cube Explorer", message);
    }
    void refresh() {
        if (std::any_of(centers_.begin(), centers_.end(), [](int n) { return n >= 0; }))
            rememberedCenters_ = centers_;
        editorView_->setFacelets(raw_, centers_);
        editorView_->setBrush(brush_);
    }
    void applyEditorMoves(std::span<const Move> moves) {
        if (std::none_of(centers_.begin(), centers_.end(), [](int n) { return n >= 0; })) {
            auto shadow = raw_;
            editor::apply_moves_raw(shadow, rememberedCenters_, moves);
        }
        editor::apply_moves_raw(raw_, centers_, moves);
    }
    void showStatistics() {
        std::map<int, int> optimal, ordinary, formula;
        int unsolved = 0;
        for (const auto *row : rows_) {
            const auto result = row->result(true);
            if (!result) {
                ++unsolved;
                continue;
            }
            auto &distribution = result->formula ? formula : result->optimalityProven ? optimal : ordinary;
            ++distribution[result->turns];
        }
        QString text = QString("魔方数量：%1\n尚未求解的魔方：%2\n").arg(rows_.size()).arg(unsolved);
        const auto append = [&text](const QString &title, const std::map<int, int> &counts, bool proven) {
            int total = 0;
            for (const auto &[depth, n] : counts)
                total += n;
            text += QString("\n%1：%2\n\n").arg(title).arg(total);
            for (const auto &[depth, n] : counts)
                text += QString("%1f%2：%3\n").arg(depth).arg(proven ? "*" : "").arg(n);
        };
        append("已最优求解的魔方", optimal, true);
        append("已用二阶段算法求解的魔方", ordinary, false);
        if (!formula.empty())
            append("公式求解结果", formula, false);
        QDialog dialog(this);
        dialog.setWindowTitle("搜索统计");
        dialog.resize(qRound(510 * uiScale_), qRound(520 * uiScale_));
        auto *layout = new QVBoxLayout(&dialog);
        auto *output = new QPlainTextEdit(text);
        output->setObjectName("searchStatistics");
        output->setReadOnly(true);
        layout->addWidget(output);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(close);
        dialog.exec();
    }
    void updateBusy() {
        int running = 0;
        for (auto *row : rows_)
            if (row->busy())
                ++running;
        if (formulaPanel_ && formulaPanel_->busy())
            ++running;
        statusBar()->showMessage(
            running ? trUi("%1 个搜索正在运行", "%1 search(es) running").arg(running)
                    : trUi("左键染色 · Shift＋左键忽略朝向 · Ctrl＋左键仅朝向 · 右键清空单个贴纸 · 中心左键取色",
                           "Left: paint · Shift+Left: ignore ori · Ctrl+Left: ori only · Right: clear · Center: pick color"));
        pumpGenerators();
    }
    void pumpGenerators() {
        if (closing_ || pumpingGenerators_)
            return;
        pumpingGenerators_ = true;
        auto guard = qScopeGuard([this] { pumpingGenerators_ = false; });
        int running =
            static_cast<int>(std::count_if(rows_.begin(), rows_.end(), [](auto *r) { return r->busy(); }));
        while (!generatorQueue_.empty() && running < maxThreads_) {
            auto *row = generatorQueue_.front();
            generatorQueue_.pop_front();
            if (std::find(rows_.begin(), rows_.end(), row) == rows_.end() || row->busy())
                continue;
            row->start(twoPhase_, optimal_);
            ++running;
        }
    }
    void updateToggleLeftText() {
        if (!toggleLeftAction_)
            return;
        const bool hidden = !left_ || left_->isHidden();
        toggleLeftAction_->setText(
            hidden ? trUi("展开主窗口", "Expand Main Window")
                   : trUi("收起主窗口", "Collapse Main Window"));
    }
    void showLeft(int = 0) {
        const bool wasHidden = left_->isHidden();
        setMinimumSize(840, 530);
        left_->show();
        updateToggleLeftText();
        if (wasHidden) {
            const auto available = screen()->availableGeometry();
            const auto desired = qRound(1120 * uiScale_);
            const auto desiredHeight = qRound(730 * uiScale_);
            resize(std::min(desired, std::max(minimumWidth(), available.width() - 36)),
                   std::min(desiredHeight, std::max(minimumHeight(), available.height() - 44)));
            split_->setSizes({std::max(540, width() - qRound(510 * uiScale_)), qRound(510 * uiScale_)});
            updateScale(true);
            QTimer::singleShot(0, this, [this] {
                const auto available = screen()->availableGeometry();
                move(available.center() - QPoint(frameGeometry().width() / 2, frameGeometry().height() / 2));
            });
        }
    }
    void hideLeft() {
        left_->hide();
        updateToggleLeftText();
        setMinimumSize(380, 530);
        resize(qRound(510 * uiScale_), qRound(730 * uiScale_));
        updateScale(true);
    }
    void updateScale(bool force = false) {
        if (scaling_ || scalableLayouts_.empty())
            return;
        const double scale =
            std::clamp(std::min(width() / (left_->isHidden() ? 510.0 : 1120.0), height() / 730.0), .75, 2.0);
        if (!force && std::abs(scale - uiScale_) < .01)
            return;
        scaling_ = true;
        auto guard = qScopeGuard([this] { scaling_ = false; });
        uiScale_ = scale;
        auto px = [scale](int value) { return qRound(value * scale); };
        right_->setMaximumWidth(px(510));
        for (const auto &item : scalableLayouts_) {
            const auto m = item.margins;
            item.layout->setContentsMargins(px(m.left()), px(m.top()), px(m.right()), px(m.bottom()));
            if (item.spacing >= 0)
                item.layout->setSpacing(px(item.spacing));
        }
        for (auto *button : right_->findChildren<QPushButton *>()) {
            if (dynamic_cast<MoveButton *>(button))
                button->setMinimumSize(px(35), px(25));
            else if (button->objectName() == "resetButton") {
                const bool narrow = button->property("resetPieces").toInt() != 0;
                button->setMinimumWidth(px(narrow ? 25 : 62));
                if (narrow)
                    button->setMaximumWidth(px(27));
                button->setStyleSheet(QString("padding:%1px %2px;").arg(px(2)).arg(px(1)));
            }
        }
        if (langToggle_) {
            langToggle_->setFixedHeight(px(26));
            langToggle_->setIconSize(QSize(px(14), px(14)));
        }
        if (themeToggle_) {
            themeToggle_->setFixedSize(px(28), px(26));
            themeToggle_->setIconSize(QSize(px(17), px(17)));
        }
        if (cornerBar_ && cornerBar_->layout()) {
            cornerBar_->layout()->setContentsMargins(0, px(2), px(6), px(2));
            cornerBar_->layout()->setSpacing(px(4));
            cornerBar_->adjustSize();
        }
        changeAppearance(dark_);
        if (!left_->isHidden())
            split_->setSizes({std::max(1, width() - px(510)), px(510)});
        right_->updateGeometry();
    }
    void updateActionIcons() {
        const auto px = [this](int value) { return std::max(12, qRound(value * uiScale_)); };
        const QColor ink = palette().color(QPalette::ButtonText);
        const QColor accentInk = palette().color(QPalette::Link);
        const QColor mutedInk = dark_ ? QColor("#9cb0c6") : QColor("#53687e");
        if (solve_) {
            solve_->setIcon(uiIcon(UiIcon::Sparkles, accentInk, px(16)));
            solve_->setIconSize(QSize(px(16), px(16)));
        }
        if (generate_) {
            generate_->setIcon(uiIcon(UiIcon::Generate, accentInk, px(16)));
            generate_->setIconSize(QSize(px(16), px(16)));
        }
        if (applyButton_) {
            applyButton_->setIcon(uiIcon(UiIcon::Apply, ink, px(14)));
            applyButton_->setIconSize(QSize(px(14), px(14)));
        }
        if (inverseButton_) {
            inverseButton_->setIcon(uiIcon(UiIcon::Inverse, mutedInk, px(14)));
            inverseButton_->setIconSize(QSize(px(14), px(14)));
        }
        if (resetButtons_[0]) {
            resetButtons_[0]->setIcon(uiIcon(UiIcon::Eraser, mutedInk, px(14)));
            resetButtons_[3]->setIcon(uiIcon(UiIcon::Clean, mutedInk, px(14)));
            resetButtons_[6]->setIcon(uiIcon(UiIcon::Random, mutedInk, px(14)));
            for (int row : {0, 3, 6})
                resetButtons_[row]->setIconSize(QSize(px(14), px(14)));
        }
        if (langToggle_)
            langToggle_->setIcon(uiIcon(UiIcon::Globe, mutedInk, px(14)));
        if (presetZbls_) {
            presetZbls_->setIcon(uiIcon(UiIcon::Layers, mutedInk, px(13)));
            presetOll_->setIcon(uiIcon(UiIcon::Layers, mutedInk, px(13)));
            presetF2l_->setIcon(uiIcon(UiIcon::Layers, mutedInk, px(13)));
            presetOllcp_->setIcon(uiIcon(UiIcon::Layers, mutedInk, px(13)));
            presetXCross_->setIcon(uiIcon(UiIcon::Crosshair, accentInk, px(13)));
            for (auto *chip : {presetZbls_, presetOll_, presetF2l_, presetOllcp_, presetXCross_})
                chip->setIconSize(QSize(px(13), px(13)));
        }
    }
    void retranslateUi() {
        if (fileMenu_)
            fileMenu_->setTitle(trUi("文件(&F)", "File(&F)"));
        if (editMenu_)
            editMenu_->setTitle(trUi("编辑(&Y)", "Edit(&Y)"));
        if (runMenu_)
            runMenu_->setTitle(trUi("运行(&Z)", "Run(&Z)"));
        if (optionsMenu_)
            optionsMenu_->setTitle(trUi("选项(&O)", "Options(&O)"));
        if (helpMenu_)
            helpMenu_->setTitle(trUi("帮助(&H)", "Help(&H)"));
        if (viewsMenu_)
            viewsMenu_->setTitle(trUi("魔方显示", "Cube View"));
        if (perfMenu_)
            perfMenu_->setTitle(trUi("性能模式", "Performance"));
        for (const auto &entry : menuTexts_)
            entry.action->setText(trUi(entry.zh, entry.en));
        updateToggleLeftText();
        if (selectBox_)
            selectBox_->setTitle(trUi("选择", "Solve"));
        if (moveBox_)
            moveBox_->setTitle(trUi("转动", "Moves"));
        if (resetBox_)
            resetBox_->setTitle(trUi("重置", "Reset"));
        if (inputBox_)
            inputBox_->setTitle(trUi("输入公式", "Formula"));
        if (solve_)
            solve_->setText(trUi("求解当前情况", "Solve Cube"));
        if (generate_)
            generate_->setText(trUi("生成当前情况", "Generate Cube"));
        if (applyButton_)
            applyButton_->setText(trUi("应用", "Apply"));
        if (inverseButton_) {
            inverseButton_->setText(trUi("逆", "Inv"));
            inverseButton_->setToolTip(
                trUi("应用输入公式的逆公式（便于快速还原或设置 Setup）",
                     "Apply the inverse of the entered formula (handy for setup/undo)"));
        }
        if (formulaInput_) {
            formulaInput_->setPlaceholderText(trUi("R U R' U' 或 RUR'U'", "R U R' U' / RUR'U'"));
            const auto hint = shortcutHintText("apply_formula");
            formulaInput_->setToolTip(
                hint.isEmpty()
                    ? trUi("输入公式（支持无空格及 R'2，按 Enter 应用）",
                           "Enter formula (supports spaceless & R'2; Enter to apply)")
                    : trUi("输入公式（支持无空格及 R'2，按 Enter 或 %1 应用）",
                           "Enter formula (supports spaceless & R'2; Enter or %1 to apply)")
                          .arg(hint));
        }
        refreshShortcutHints();
        const std::array<QString, 3> resetNames = {trUi("空白", "Empty"), trUi("复原", "Clean"),
                                                   trUi("随机", "Random")};
        const QString cornerShort = trUi("角", "C");
        const QString edgeShort = trUi("棱", "E");
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                if (auto *btn = resetButtons_[r * 3 + c])
                    btn->setText(c == 0 ? resetNames[r] : c == 1 ? cornerShort : edgeShort);
        if (presetZbls_) {
            presetZbls_->setText("ZBLS");
            presetZbls_->setToolTip(trUi("预存 ZBLS 标记：底两层复原，顶层涂灰，仅顶层 4 棱标为 Ctrl 朝向 (@)",
                                         "ZBLS preset: F2L solved, LL gray, 4 U edges orientation-marked (@)"));
            presetOll_->setText("OLL");
            presetOll_->setToolTip(trUi("预存 OLL 标记：底两层复原，顶面 8 块标为 Ctrl 朝向 (@)，侧面涂灰",
                                        "OLL preset: F2L solved, all 8 U stickers orientation-marked (@)"));
            presetF2l_->setText("F2L");
            presetF2l_->setToolTip(trUi("预存 F2L 标记：底两层复原，顶层全部涂灰",
                                        "F2L preset: F2L solved, entire top layer masked"));
            presetOllcp_->setText("OLLCP/COLL");
            presetOllcp_->setToolTip(
                trUi("预存 OLLCP / COLL 标记：底两层与顶层四角复原，顶层 4 棱仅标 Ctrl 朝向 (@)",
                     "OLLCP / COLL preset: F2L & U corners solved, 4 U edges orientation-marked (@)"));
            presetXCross_->setText(trUi("Cross 快速求解", "Cross Solver"));
            presetXCross_->setToolTip(
                trUi("支持选择任意底色与任意 F2L 槽位（Cross / XC / XXC / XXXC）快速求解",
                     "Quick Cross / XC / XXC / XXXC solver with selectable bottom color and F2L slots"));
        }
        if (langToggle_) {
            langToggle_->setText(english_ ? "EN" : "中文");
            langToggle_->setToolTip(english_ ? "切换到中文界面" : "Switch to English UI");
            langToggle_->setAccessibleName(langToggle_->toolTip());
        }
        if (themeToggle_) {
            themeToggle_->setToolTip(
                dark_ ? trUi("切换到白天模式", "Switch to Light Mode")
                      : trUi("切换到夜晚模式", "Switch to Dark Mode"));
            themeToggle_->setAccessibleName(themeToggle_->toolTip());
        }
        for (auto *button : right_->findChildren<QPushButton *>())
            if (dynamic_cast<MoveButton *>(button))
                button->setToolTip(
                    trUi("左键：顺时针 90°；右键：逆时针 90°。半转可连按两次，或输入 2。",
                         "Left click: CW 90°; Right click: CCW 90°. Click twice or type 2 for half turns."));
        if (editorView_)
            editorView_->retranslateUi();
        for (auto *row : rows_)
            row->retranslateUi();
        if (formulaPanel_)
            formulaPanel_->retranslateUi();
        if (cornerBar_)
            cornerBar_->adjustSize();
    }
    void setLanguage(bool english) {
        english_ = english;
        setUiEnglish(english_);
        preferences_.setValue("Appearance/English", english_);
        retranslateUi();
    }
    void changeAppearance(bool dark) {
        dark_ = dark;
        applyAppTheme(*qApp, dark_, uiScale_);
        if (nightAction_)
            nightAction_->setChecked(dark_);
        editorView_->setDarkMode(dark_);
        editorView_->setColors(colors_);
        if (themeToggle_) {
            themeToggle_->setIcon(themeToggleIcon(dark_));
            themeToggle_->setToolTip(
                dark_ ? trUi("切换到白天模式", "Switch to Light Mode")
                      : trUi("切换到夜晚模式", "Switch to Dark Mode"));
            themeToggle_->setAccessibleName(themeToggle_->toolTip());
        }
        updateActionIcons();
        for (auto *widget : findChildren<QWidget *>())
            if (auto *canvas = dynamic_cast<CubeCanvas *>(widget)) {
                canvas->setDarkMode(dark_);
                canvas->setColors(colors_);
            }
        for (auto *row : rows_) {
            row->setAppearance(dark_, colors_);
            row->setUiScale(uiScale_);
        }
        if (formulaPanel_)
            formulaPanel_->refreshAppearance();
        preferences_.setValue("Appearance/Dark", dark_);
        refresh();
    }
    static QKeySequence alternateEnterSequence(const QKeySequence &seq) {
        if (seq.count() != 1)
            return {};
        const QKeyCombination combo = seq[0];
        if (combo.key() == Qt::Key_Return)
            return QKeySequence(QKeyCombination(combo.keyboardModifiers(), Qt::Key_Enter));
        if (combo.key() == Qt::Key_Enter)
            return QKeySequence(QKeyCombination(combo.keyboardModifiers(), Qt::Key_Return));
        return {};
    }
    QString shortcutHintText(const QString &id) const {
        for (const auto &binding : shortcuts_)
            if (binding.id == id && !binding.currentKey.isEmpty())
                return binding.currentKey.toString(QKeySequence::NativeText).replace("Return", "Enter");
        return {};
    }
    QString shortcutHintSuffix(const QString &id) const {
        const auto text = shortcutHintText(id);
        return text.isEmpty() ? QString{} : QString(" (%1)").arg(text);
    }
    void refreshShortcutHints() {
        if (applyButton_)
            applyButton_->setToolTip(trUi("应用输入公式", "Apply entered formula") +
                                     shortcutHintSuffix("apply_formula"));
        if (inverseButton_)
            inverseButton_->setToolTip(
                trUi("应用输入公式的逆公式（便于快速还原或设置 Setup）",
                     "Apply the inverse of the entered formula (handy for setup/undo)") +
                shortcutHintSuffix("apply_inverse"));
        if (solve_)
            solve_->setToolTip(trUi("求解当前魔方", "Solve current cube") + shortcutHintSuffix("solve_cube"));
        if (generate_)
            generate_->setToolTip(trUi("生成当前魔方", "Generate current cube") +
                                  shortcutHintSuffix("generate_cube"));
        if (presetXCross_)
            presetXCross_->setToolTip(
                trUi("支持选择任意底色与任意 F2L 槽位（Cross / XC / XXC / XXXC）快速求解",
                     "Quick Cross / XC / XXC / XXXC solver with selectable bottom color and F2L slots") +
                shortcutHintSuffix("xcross_solver"));
    }
    void applyShortcutKey(ShortcutBinding &binding, const QKeySequence &seq, bool persist) {
        binding.currentKey = seq;
        if (binding.primary)
            binding.primary->setKey(seq);
        if (binding.alt)
            binding.alt->setKey(alternateEnterSequence(seq));
        if (persist)
            preferences_.setValue("Shortcuts/" + binding.id, seq.toString(QKeySequence::PortableText));
    }
    void setupShortcuts() {
        auto addBinding = [this](const char *id, const char *zh, const char *en, QKeySequence defaultKey,
                                 std::function<void()> trigger) {
            ShortcutBinding binding;
            binding.id = QString::fromLatin1(id);
            binding.zh = zh;
            binding.en = en;
            binding.defaultKey = std::move(defaultKey);
            binding.trigger = std::move(trigger);
            const QString prefKey = "Shortcuts/" + binding.id;
            if (preferences_.contains(prefKey))
                binding.currentKey = QKeySequence::fromString(preferences_.value(prefKey).toString(),
                                                              QKeySequence::PortableText);
            else
                binding.currentKey = binding.defaultKey;
            binding.primary = new QShortcut(binding.currentKey, this);
            binding.primary->setContext(Qt::WindowShortcut);
            binding.alt = new QShortcut(alternateEnterSequence(binding.currentKey), this);
            binding.alt->setContext(Qt::WindowShortcut);
            const std::size_t index = shortcuts_.size();
            auto fire = [this, index] {
                if (index < shortcuts_.size() && shortcuts_[index].trigger)
                    shortcuts_[index].trigger();
            };
            connect(binding.primary, &QShortcut::activated, this, fire);
            connect(binding.alt, &QShortcut::activated, this, fire);
            shortcuts_.push_back(std::move(binding));
        };
        addBinding("apply_formula", "应用当前公式", "Apply Formula", QKeySequence(Qt::CTRL | Qt::Key_Return),
                   [this] {
                       try {
                           applyFormula(formulaInput_->text());
                       } catch (const std::exception &e) {
                           warn(friendlyError(e.what()));
                       }
                   });
        addBinding("apply_inverse", "应用当前公式的逆", "Apply Inverse Formula",
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return), [this] {
                       try {
                           applyInverseFormula(formulaInput_->text());
                       } catch (const std::exception &e) {
                           warn(friendlyError(e.what()));
                       }
                   });
        addBinding("solve_cube", "求解当前情况", "Solve Current Cube", QKeySequence(Qt::Key_F5),
                   [this] { chooseMode(false); });
        addBinding("generate_cube", "生成当前情况", "Generate Current Cube", QKeySequence(Qt::Key_F6),
                   [this] { chooseMode(true); });
        addBinding("xcross_solver", "Cross 快速求解", "Cross Quick Solver",
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_X), [this] { openXCrossDialog(); });
        addBinding("cycle_view", "切换魔方显示视图（经典 / 2D / 3D）", "Cycle Cube View (Classic / 2D / 3D)",
                   QKeySequence(Qt::Key_F2), [this] { setView((currentView_ + 1) % 3); });
        addBinding("toggle_dark", "切换白天 / 夜晚模式", "Toggle Dark / Light Mode",
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [this] { changeAppearance(!dark_); });
        addBinding("toggle_lang", "切换中 / 英文界面", "Toggle Chinese / English UI",
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), [this] { setLanguage(!english_); });
        addBinding("reset_clean", "重置为复原状态", "Reset to Solved", QKeySequence(Qt::CTRL | Qt::Key_R),
                   [this] { resetEditor(editor::Reset::Clean); });
        addBinding("reset_random", "随机打乱魔方", "Random Scramble",
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                   [this] { resetEditor(editor::Reset::Random); });
        addBinding("focus_formula", "聚焦公式输入框", "Focus Formula Input", QKeySequence(Qt::CTRL | Qt::Key_L),
                   [this] {
                       formulaInput_->setFocus(Qt::ShortcutFocusReason);
                       formulaInput_->selectAll();
                   });
    }
    void shortcutsDialog() {
        QDialog dialog(this);
        dialog.setObjectName("shortcutsDialog");
        dialog.setWindowTitle(trUi("快捷键设置", "Keyboard Shortcuts"));
        dialog.setMinimumWidth(qRound(470 * uiScale_));
        auto *layout = new QVBoxLayout(&dialog);
        auto *tip = new QLabel(
            trUi("点击右侧输入框并直接按下组合键即可修改（例如 Ctrl+Enter 应用公式）；支持主回车与小键盘回车。",
                 "Click any box and press a key combination to rebind (e.g. Ctrl+Enter to apply formula)."));
        tip->setWordWrap(true);
        layout->addWidget(tip);
        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        std::vector<QKeySequenceEdit *> edits;
        edits.reserve(shortcuts_.size());
        for (const auto &binding : shortcuts_) {
            auto *rowWidget = new QWidget;
            auto *rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(6);
            auto *edit = new QKeySequenceEdit(binding.currentKey);
            edit->setObjectName("shortcutEdit_" + binding.id);
            edit->setMaximumSequenceLength(1);
            edit->setMinimumWidth(qRound(150 * uiScale_));
            auto *clearBtn = new QPushButton(trUi("清除", "Clear"));
            clearBtn->setAutoDefault(false);
            clearBtn->setCursor(Qt::PointingHandCursor);
            connect(clearBtn, &QPushButton::clicked, edit, &QKeySequenceEdit::clear);
            rowLayout->addWidget(edit, 1);
            rowLayout->addWidget(clearBtn);
            form->addRow(trUi(binding.zh, binding.en), rowWidget);
            edits.push_back(edit);
        }
        layout->addLayout(form);
        auto *bottom = new QHBoxLayout;
        auto *resetBtn = new QPushButton(trUi("恢复默认", "Reset Defaults"));
        resetBtn->setAutoDefault(false);
        resetBtn->setCursor(Qt::PointingHandCursor);
        connect(resetBtn, &QPushButton::clicked, &dialog, [&] {
            for (std::size_t i = 0; i < shortcuts_.size(); ++i)
                edits[i]->setKeySequence(shortcuts_[i].defaultKey);
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        bottom->addWidget(resetBtn);
        bottom->addStretch();
        bottom->addWidget(buttons);
        layout->addLayout(bottom);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            for (std::size_t i = 0; i < edits.size(); ++i) {
                const auto a = edits[i]->keySequence();
                if (a.isEmpty())
                    continue;
                for (std::size_t j = i + 1; j < edits.size(); ++j) {
                    const auto b = edits[j]->keySequence();
                    if (!b.isEmpty() && (a == b || alternateEnterSequence(a) == b)) {
                        QMessageBox::warning(
                            &dialog, trUi("快捷键冲突", "Shortcut Conflict"),
                            trUi("“%1”与“%2”使用了相同的快捷键 %3，请修改后再保存。",
                                 "\"%1\" and \"%2\" share the same shortcut (%3). Please choose distinct keys.")
                                .arg(trUi(shortcuts_[i].zh, shortcuts_[i].en))
                                .arg(trUi(shortcuts_[j].zh, shortcuts_[j].en))
                                .arg(a.toString(QKeySequence::NativeText)));
                        return;
                    }
                }
            }
            dialog.accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            for (std::size_t i = 0; i < shortcuts_.size(); ++i)
                applyShortcutKey(shortcuts_[i], edits[i]->keySequence(), true);
            preferences_.sync();
            retranslateUi();
        }
    }
    void customColor() {
        const auto index = face_names.find(brush_);
        const auto chosen =
            QColorDialog::getColor(colors_[index], this, "自定义选中颜色", QColorDialog::DontUseNativeDialog);
        if (!chosen.isValid())
            return;
        colors_[index] = chosen;
        preferences_.setValue(QString("Colors/%1").arg(brush_), chosen.name());
        changeAppearance(dark_);
    }
    void centersDialog() {
        QDialog dialog(this);
        dialog.setWindowTitle("编辑中心方向");
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        std::array<QComboBox *, 6> boxes;
        for (int i = 0; i < 6; ++i) {
            boxes[i] = new QComboBox;
            boxes[i]->addItems({"不约束", "0°", "顺时针 90°", "180°", "顺时针 270°"});
            boxes[i]->setCurrentIndex(centers_[i] + 1);
            form->addRow(QString(face_names[i]), boxes[i]);
        }
        layout->addLayout(form);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            for (int i = 0; i < 6; ++i)
                centers_[i] = static_cast<std::int8_t>(boxes[i]->currentIndex() - 1);
            dirty_ = true;
            refresh();
        }
    }
    void chooseMode(bool generate) {
        try {
            replayTimer_.stop();
            const auto fixed = editor::check_all(raw_);
            refresh();
            if (!fixed.accepted) {
                warn("输入中的冲突已按原版规则修正。请检查魔方，再次点击求解或生成。");
                return;
            }
            const auto p = pattern();
            if (!ordinaryPattern(p)) {
                openFormula(p, generate);
                return;
            }
            ModeChoiceDialog choice(uiScale_, this);
            const auto method = choice.exec();
            if (method == 1) {
                auto *row = addOrdinary(p, generate);
                row->start(twoPhase_, optimal_);
            } else if (method == 2)
                openFormula(p, generate);
        } catch (const std::exception &e) {
            warn(friendlyError(e.what()));
        }
    }
    void selectRow(ResultRow *row, Qt::KeyboardModifiers mods) {
        auto first = std::find(rows_.begin(), rows_.end(), anchor_),
             last = std::find(rows_.begin(), rows_.end(), row);
        if (!anchor_) {
            row->setSelected(!row->selected());
            if (row->selected())
                anchor_ = row;
            return;
        }
        if (mods.testFlag(Qt::ShiftModifier) && first != rows_.end()) {
            if (!mods.testFlag(Qt::ControlModifier))
                for (auto *r : rows_)
                    r->setSelected(false);
            if (first > last)
                std::swap(first, last);
            for (auto it = first; it <= last; ++it)
                (*it)->setSelected(true);
            if (!mods.testFlag(Qt::ControlModifier))
                anchor_ = row;
        } else if (mods.testFlag(Qt::ControlModifier)) {
            row->setSelected(!row->selected());
            anchor_ = row->selected() ? row : nullptr;
        } else {
            for (auto *r : rows_)
                r->setSelected(r == row);
            anchor_ = row;
        }
    }
    void attachRow(ResultRow *row) {
        dirty_ = true;
        rows_.push_back(row);
        row->setNumber(static_cast<int>(rows_.size()));
        row->setAppearance(dark_, colors_);
        row->setViewMode(currentView_);
        row->setCamera(editorView_->canvas()->yaw(), editorView_->canvas()->pitch(), 1.0);
        row->setUiScale(uiScale_);
        row->onSelect = [this](ResultRow *row, Qt::KeyboardModifiers mods) { selectRow(row, mods); };
        row->onContext = [this](ResultRow *row, QPoint position) { rowMenu(row, position); };
        row->onClearSelection = [this] { clearSelection(); };
        row->onBackgroundContext = [this](QPoint position) { backgroundMenu(position); };
        row->onLoad = [this](PatternState p) { restore(p); };
        row->onReplay = [this](ResultData r) { startReplay(std::move(r)); };
        row->onRun = [this, row] { row->start(twoPhase_, optimal_); };
        connect(row->findChild<QLineEdit *>(), &QLineEdit::textEdited, this, [this] { dirty_ = true; });
        row->onBusy = [this] { updateBusy(); };
        row->onFinished = [this, row] {
            if (!closing_) {
                dirty_ = true;
                if (auto previous = autoLengths_.find(row); previous != autoLengths_.end()) {
                    if (auto result = row->result(true); result && result->turns < previous->second)
                        autoAttempted_.erase(row);
                    autoLengths_.erase(previous);
                }
                updateBusy();
            }
        };
        auto *list = ordinaryRows_;
        list->insertWidget(list->count() - 1, row);
    }
    void clearSelection() {
        for (auto *row : rows_)
            row->setSelected(false);
        anchor_ = nullptr;
    }
    void backgroundMenu(QPoint position) {
        QMenu menu(this);
        const bool selected = std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); });
        auto *remove = menu.addAction(trUi("删除选中魔方", "Delete Selected Cubes"));
        menu.addSeparator();
        auto *start = menu.addAction(trUi("开始搜索选中魔方…", "Start Search for Selected…"));
        auto *stop = menu.addAction(trUi("停止搜索选中魔方", "Stop Search for Selected"));
        for (auto *a : {remove, start, stop})
            a->setEnabled(selected);
        menu.addSeparator();
        auto *clear = menu.addAction(trUi("清空主窗口", "Clear Main Window"));
        clear->setEnabled(!rows_.empty());
        const auto chosen = menu.exec(position);
        if (chosen == remove)
            removeRows([](auto *r) { return r->selected(); });
        else if (chosen == start)
            startSelected();
        else if (chosen == stop)
            stopSelected();
        else if (chosen == clear)
            clearMainWindow();
    }
    void clearMainWindow() {
        if (dirty_ &&
            QMessageBox::question(
                this, trUi("清空主窗口", "Clear Main Window"),
                trUi("主窗口有尚未保存的内容，仍要清空吗？", "Unsaved cubes exist. Clear main window anyway?"),
                QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        removeRows([](auto *) { return true; });
        dirty_ = false;
    }
    void rowMenu(ResultRow *row, QPoint point) {
        QMenu menu(this);
        auto *inverse = menu.addAction(trUi("逆置换", "Invert Cube"));
        auto *multiply = menu.addAction(trUi("与编辑器魔方相乘", "Multiply with Editor Cube"));
        auto *multiplyInverse =
            menu.addAction(trUi("与编辑器魔方的逆相乘", "Multiply with Inverse of Editor Cube"));
        for (auto *a : {inverse, multiply, multiplyInverse})
            a->setEnabled(!row->isFormula() && ordinaryPattern(row->input()));
        menu.addSeparator();
        auto *solver = menu.addAction(trUi("复制求解公式", "Copy Solver Maneuver"));
        auto *generator = menu.addAction(trUi("复制生成公式", "Copy Generator Maneuver"));
        solver->setEnabled(!row->isFormula() && row->result(true).has_value());
        generator->setEnabled(solver->isEnabled());
        menu.addSeparator();
        auto *remove = menu.addAction(trUi("删除此魔方", "Delete This Cube"));
        menu.addSeparator();
        auto *load = menu.addAction(trUi("传送到贴纸编辑器", "Transfer to Editor"));
        menu.addSeparator();
        auto *copy = menu.addAction(trUi("复制当前公式", "Copy Displayed Formula"));
        auto *play = menu.addAction(trUi("回放公式", "Replay Formula"));
        copy->setEnabled(row->result().has_value());
        play->setEnabled(copy->isEnabled());
        const auto action = menu.exec(point);
        if (!action)
            return;
        try {
            if (action == load)
                restore(row->input());
            else if (action == play) {
                if (auto data = row->result())
                    startReplay(*data);
            } else if (action == remove)
                removeRows([row](ResultRow *r) { return r == row; });
            else if (action == copy || action == solver || action == generator) {
                if (auto data = row->result(action != copy)) {
                    auto moves = data->moves;
                    if (action != copy && data->generate != (action == generator))
                        moves = cube::inverse(moves);
                    auto text = qstr(format_moves(moves, data->formula));
                    if (action != copy)
                        text += QString(" (%1f%2)").arg(data->turns).arg(data->optimalityProven ? "*" : "");
                    QApplication::clipboard()->setText(text);
                }
            } else if (action == inverse || action == multiply || action == multiplyInverse) {
                dirty_ = true;
                if (action == inverse) {
                    row->invertInput();
                } else {
                    auto c = completeCube(row->input());
                    auto other = completeCube(pattern());
                    c.cubies =
                        compose(c.cubies, action == multiply ? other.cubies : cube::inverse(other.cubies));
                    row->replaceInput(pattern_from_cube(c));
                    if (anchor_ == row)
                        anchor_ = nullptr;
                }
            }
        } catch (const std::exception &e) {
            warn(friendlyError(e.what()));
        }
    }
    template <class Predicate> void removeRows(Predicate predicate) {
        autoAttempted_.clear();
        autoLengths_.clear();
        for (auto it = rows_.begin(); it != rows_.end();) {
            auto *row = *it;
            if (predicate(row)) {
                dirty_ = true;
                std::erase(generatorQueue_, row);
                if (anchor_ == row)
                    anchor_ = nullptr;
                row->onBusy = {};
                row->onFinished = {};
                it = rows_.erase(it);
                delete row;
            } else
                ++it;
        }
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->setNumber(static_cast<int>(i + 1));
        updateBusy();
    }
    void startSelected() {
        if (std::none_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); }))
            return;
        QDialog dialog(this);
        dialog.setWindowTitle("搜索选中魔方");
        auto *layout = new QVBoxLayout(&dialog);
        auto *algorithm = new QComboBox;
        algorithm->addItems({"二阶段求解", "最优求解"});
        algorithm->setCurrentIndex(1);
        layout->addWidget(algorithm);
        auto *threshold = new QComboBox;
        threshold->addItems({"当前公式超过 0 步（含尚未求解）", "当前公式超过 18 步", "当前公式超过 19 步",
                             "当前公式超过 20 步"});
        layout->addWidget(threshold);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted)
            return;
        if (std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected() && r->busy(); })) {
            warn("选中的魔方仍在搜索。请先停止这些搜索，再开始批量搜索。");
            return;
        }
        const int length = threshold->currentIndex() ? 17 + threshold->currentIndex() : 0;
        const auto eligible = [length](ResultRow *r) {
            const auto d = r->result(true);
            return r->selected() && !r->isFormula() && !r->busy() &&
                   (!d || (!d->optimalityProven && d->turns > length));
        };
        if (std::count_if(rows_.begin(), rows_.end(), eligible) > 500) {
            warn("一次最多启动 500 个选中魔方的搜索，请减少选择或使用自动运行。");
            return;
        }
        for (auto *row : rows_)
            if (eligible(row)) {
                row->setOptimal(algorithm->currentIndex() == 1);
                row->start(twoPhase_, optimal_);
            }
    }
    void stopSelected() {
        for (auto *row : rows_)
            if (row->selected()) {
                std::erase(generatorQueue_, row);
                row->stop();
            }
    }
    void autorun(bool optimal) {
        autoRun_ = !autoRun_ || autoOptimal_ != optimal;
        autoOptimal_ = optimal;
        autoAttempted_.clear();
        autoLengths_.clear();
        const auto selected = std::find_if(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); });
        autoIndex_ = selected == rows_.end() ? 0 : static_cast<std::size_t>(selected - rows_.begin());
        if (autoRun_) {
            autorunTimer_.start(100);
            autorunTick();
        } else
            autorunTimer_.stop();
    }
    void autorunTick() {
        if (!autoRun_ || rows_.empty())
            return;
        int running = 0;
        for (auto *row : rows_)
            if (row->busy())
                ++running;
        for (std::size_t scanned = 0; scanned < rows_.size() && running < maxThreads_; ++scanned) {
            auto *row = rows_[autoIndex_++ % rows_.size()];
            const auto result = row->result(true);
            if (!row->isFormula() && !row->busy() && !autoAttempted_.contains(row) &&
                (!result ||
                 (!result->optimalityProven && (autoOptimal_ || result->turns > twoPhase_.target_moves)))) {
                autoAttempted_.insert(row);
                autoLengths_[row] = result ? result->turns : 999;
                row->setOptimal(autoOptimal_);
                row->start(twoPhase_, optimal_);
                ++running;
            }
        }
    }
    void reorder(int kind) {
        if (!rows_.empty())
            dirty_ = true;
        std::stable_sort(rows_.begin(), rows_.end(), [kind](ResultRow *a, ResultRow *b) {
            const int names = a->patternName().localeAwareCompare(b->patternName());
            const auto ad = a->result(true), bd = b->result(true);
            const int al = ad ? ad->turns : 999, bl = bd ? bd->turns : 999;
            if (kind == 0)
                return names ? names < 0 : al < bl;
            if (kind == 1)
                return al == bl ? names < 0 : al < bl;
            const auto id = [](const PatternState &p) {
                const auto rank = [](const auto &permutation) {
                    std::int64_t value = 0;
                    for (int i = static_cast<int>(permutation.size()) - 1; i > 0; --i) {
                        int count = 0;
                        for (int j = i - 1; j >= 0; --j)
                            count += permutation[j] > permutation[i];
                        value = (value + count) * i;
                    }
                    return value;
                };
                int co = 0, eo = 0;
                for (int i = 0; i < 7; ++i)
                    co = 3 * co + p.co[i];
                for (int i = 0; i < 11; ++i)
                    eo = 2 * eo + p.eo[i];
                return std::pair{rank(p.ep) + 40320 * rank(p.cp), eo + 2048 * co};
            };
            const bool afull = ordinaryPattern(a->input()), bfull = ordinaryPattern(b->input());
            if (afull != bfull)
                return afull;
            return afull ? id(a->input()) < id(b->input()) : names < 0;
        });
        for (auto *row : rows_) {
            auto *layout = ordinaryRows_;
            layout->removeWidget(row);
            layout->insertWidget(layout->count() - 1, row);
        }
        for (std::size_t i = 0; i < rows_.size(); ++i)
            rows_[i]->setNumber(static_cast<int>(i + 1));
    }
    void addRandom() {
        bool ok = false;
        const int count = QInputDialog::getInt(this, "添加随机魔方", "数量", 10, 1, 100000, 1, &ok);
        if (!ok)
            return;
        for (int i = 0; i < count; ++i) {
            std::string raw = raw_;
            auto centers = centers_;
            editor::reset(raw, centers, editor::Reset::Random, editor::Pieces::All, std::random_device{}());
            auto p = parse_pattern_facelets(raw);
            p.center_ori = centers;
            if (ordinaryPattern(p))
                addOrdinary(p, true);
            else {
                auto *row = new ResultRow(p, true, true);
                attachRow(row);
                showLeft(1);
            }
        }
    }
    void legacyWcaScrambles() {
        // RubikMain.WCAscrambleClick: the historical five-generator shortcut.
        // The label describes the original feature, not current competition certification.
        colors_ = {QColor("#ffffff"), QColor("#ff0000"), QColor("#008000"), QColor("#ffff00"),
                   QColor("#ff8000"), QColor("#0000ff"), QColor("#808080")};
        twoPhase_.target_moves = 21;
        autoRun_ = false;
        autorunTimer_.stop();
        removeRows([](auto *) { return true; });
        for (int i = 0; i < 5; ++i) {
            resetEditor(editor::Reset::Random);
            auto p = parse_pattern_facelets(raw_);
            p.center_ori.fill(-1);
            auto *row = addOrdinary(p, true);
            generatorQueue_.push_back(row);
        }
        pumpGenerators();
        changeAppearance(dark_);
    }
    void solverOptions(bool optimal) {
        QDialog dialog(this);
        dialog.setWindowTitle(optimal ? "最优求解设置" : "二阶段算法设置");
        auto *layout = new QVBoxLayout(&dialog);
        auto *form = new QFormLayout;
        auto spin = [form](const QString &label, int value, int maximum) {
            auto *box = new QSpinBox;
            box->setRange(0, maximum);
            box->setValue(value);
            form->addRow(label, box);
            return box;
        };
        auto *depth = spin("最大步数", optimal ? optimal_.max_moves : twoPhase_.max_moves, 60);
        auto *target = optimal ? nullptr : spin("达到此步数自动停止", twoPhase_.target_moves, 60);
        auto *phase = optimal ? nullptr : spin("第一阶段深度上限", twoPhase_.max_phase1_moves, 30);
        auto *time = spin(
            "搜索秒数（0 不限）",
            static_cast<int>((optimal ? optimal_.time_limit : twoPhase_.time_limit).count() / 1000), 86400);
        auto *nodes = new QDoubleSpinBox;
        nodes->setDecimals(0);
        nodes->setRange(0, 1e12);
        nodes->setValue(static_cast<double>(optimal ? optimal_.node_limit : twoPhase_.node_limit));
        form->addRow("节点上限（0 不限）", nodes);
        layout->addLayout(form);
        QCheckBox *triple = nullptr;
        if (!optimal) {
            triple = new QCheckBox("使用三轴搜索（UD / RL / FB）");
            triple->setObjectName("useTripleSearch");
            triple->setChecked(twoPhase_.use_triple_search);
            layout->addWidget(triple);
        }
        if (optimal) {
            auto *note = new QLabel("使用原版三轴剪枝下界进行精确搜索。只有证明没有更短公式后才标记 "
                                    "*；深层搜索可能耗时较长，可随时停止。");
            note->setWordWrap(true);
            note->setMaximumWidth(420);
            layout->addWidget(note);
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            if (optimal) {
                optimal_.max_moves = depth->value();
                optimal_.time_limit = std::chrono::seconds(time->value());
                optimal_.node_limit = static_cast<std::uint64_t>(nodes->value());
            } else {
                twoPhase_.max_moves = depth->value();
                twoPhase_.target_moves = target->value();
                twoPhase_.max_phase1_moves = phase->value();
                twoPhase_.time_limit = std::chrono::seconds(time->value());
                twoPhase_.node_limit = static_cast<std::uint64_t>(nodes->value());
                twoPhase_.use_triple_search = triple->isChecked();
            }
        }
    }
    static QJsonObject patternJson(const PatternState &p) {
        auto array = [](const auto &values) {
            QJsonArray a;
            for (auto v : values)
                a.append(static_cast<int>(v));
            return a;
        };
        return {{"cp", array(p.cp)},
                {"co", array(p.co)},
                {"ep", array(p.ep)},
                {"eo", array(p.eo)},
                {"center_ori", array(p.center_ori)},
                {"frame", p.frame}};
    }
    static PatternState readPattern(const QJsonObject &json) {
        PatternState p;
        auto array = [&](const char *name, auto &values) {
            const auto a = json.value(name).toArray();
            if (a.size() != static_cast<int>(values.size()))
                throw std::invalid_argument("工程图案数组长度错误");
            for (int i = 0; i < a.size(); ++i) {
                if (!a[i].isDouble() || a[i].toDouble() != a[i].toInt() || a[i].toInt() < -1 ||
                    a[i].toInt() > 127)
                    throw std::invalid_argument("工程图案数字错误");
                values[i] = static_cast<std::int8_t>(a[i].toInt());
            }
        };
        array("cp", p.cp);
        array("co", p.co);
        array("ep", p.ep);
        array("eo", p.eo);
        array("center_ori", p.center_ori);
        const auto frame = json.value("frame");
        if (!frame.isDouble() || frame.toDouble() != frame.toInt() || frame.toInt() < 0 || frame.toInt() > 23)
            throw std::invalid_argument("工程朝向编号无效");
        p.frame = static_cast<std::uint8_t>(frame.toInt());
        const auto valid = validate_pattern(p);
        if (!valid)
            throw std::invalid_argument(valid.message);
        return p;
    }
    bool saveFile(bool legacy = false) {
        const auto path = QFileDialog::getSaveFileName(
            this, legacy ? "保存原版公式文本" : "保存工作区",
            currentFile_.isEmpty() ? baseDirectory() + "/data/untitled.cex" : currentFile_,
            legacy ? "原版公式文本 (*.txt)" : "Cube Explorer 工作区 (*.cex)");
        if (path.isEmpty())
            return false;
        try {
            savePath(path, legacy);
            currentFile_ = path;
            setWindowTitle(QFileInfo(path).fileName() + " - Cube Explorer Modern");
            return true;
        } catch (const std::exception &e) {
            warn(friendlyError(e.what()));
            return false;
        }
    }
    void saveDefaultSolver() {
        if (rows_.empty())
            return;
        auto data = rows_.back()->result(true);
        if (!data)
            return;
        if (data->formula && data->generate) {
            warn("公式生成与求解的模 U 目标不同，请先生成对应求解结果。");
            return;
        }
        auto moves = data->generate ? cube::inverse(data->moves) : data->moves;
        QDir().mkpath(baseDirectory() + "/data");
        QFile file(baseDirectory() + "/data/ctrlFsave.txt");
        if (file.open(QIODevice::WriteOnly))
            file.write(qstr(format_moves(moves)).toUtf8());
    }
    void loadFile() {
        const auto path = QFileDialog::getOpenFileName(this, "加载公式 / 工作区", baseDirectory() + "/data",
                                                       "Cube Explorer (*.cex *.txt);;所有文件 (*)");
        if (path.isEmpty())
            return;
        bool append = false;
        if (!rows_.empty()) {
            QMessageBox question(this);
            question.setWindowTitle("加载公式");
            question.setText("主窗口已有魔方，选择替换或追加。");
            auto *replace = question.addButton("替换", QMessageBox::AcceptRole);
            auto *add = question.addButton("追加", QMessageBox::ActionRole);
            question.addButton(QMessageBox::Cancel);
            question.exec();
            if (question.clickedButton() != replace && question.clickedButton() != add)
                return;
            append = question.clickedButton() == add;
        }
        try {
            loadPath(path, append);
            currentFile_ = path;
            setWindowTitle(QFileInfo(path).fileName() + " - Cube Explorer Modern");
        } catch (const std::exception &e) {
            warn(friendlyError(e.what()));
        }
    }
    QString html(bool selectedOnly = false) {
        QString text =
            "<!doctype html><meta charset='utf-8'><title>Cube Explorer</title><style>body{font:15px "
            "sans-serif}article{display:flex;gap:20px;border-bottom:1px solid "
            "#ccc;padding:12px}img{width:190px}code{white-space:pre-wrap}</style>";
        for (auto *row : rows_)
            if (!selectedOnly || row->selected()) {
                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                row->thumbnail()->grab().save(&buffer, "PNG");
                auto d = row->result(true);
                text += "<article><img src='data:image/png;base64," + QString::fromLatin1(bytes.toBase64()) +
                        "'><div><h3>" + row->patternName().toHtmlEscaped() + "</h3><p>" +
                        (row->generated() ? "Generator" : "Solver") + "</p><code>" +
                        (d ? formatDisplayMoves(d->moves, d->formula).toHtmlEscaped() : "尚未运行") +
                        "</code>" +
                        (d ? QString("<p>%1 步%2</p>").arg(d->turns).arg(d->optimalityProven ? " · 最优" : "")
                           : QString{}) +
                        "</div></article>";
            }
        return text;
    }
    void exportHtml() {
        const auto path = QFileDialog::getSaveFileName(this, "保存为 HTML",
                                                       baseDirectory() + "/data/cubes.html", "HTML (*.html)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(html().toUtf8());
        else
            warn("无法写入文件。");
    }
    void printRows(bool selectedOnly) {
        QPrintDialog dialog(&printer_, this);
        if (dialog.exec() == QDialog::Accepted) {
            QTextDocument doc;
            doc.setHtml(html(selectedOnly));
            doc.print(&printer_);
        }
    }
    void importState() {
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(
            this, "输入贴纸状态", "54 个贴纸按 URFDLB 排列。? 未知，小写忽略朝向，@ 仅朝向。", qstr(raw_),
            &ok);
        if (!ok)
            return;
        try {
            restore(parse_pattern_facelets(text.toStdString()));
        } catch (const std::exception &e) {
            warn(friendlyError(e.what()));
        }
    }
    void createMenus() {
        auto action = [this](QMenu *menu, const char *zh, const char *en, std::function<void()> fn,
                             const QKeySequence &key = QKeySequence{}) {
            auto *a = menu->addAction(trUi(zh, en));
            if (!key.isEmpty())
                a->setShortcut(key);
            connect(a, &QAction::triggered, this, std::move(fn));
            menuTexts_.push_back({a, zh, en});
            return a;
        };
        fileMenu_ = menuBar()->addMenu(trUi("文件(&F)", "File(&F)"));
        action(fileMenu_, "加载公式", "Load Maneuvers", [this] { loadFile(); });
        auto *saveText = action(fileMenu_, "保存公式文本", "Save Maneuvers", [this] { saveFile(true); });
        auto *saveHtml = action(fileMenu_, "保存为 HTML", "Save as HTML", [this] { exportHtml(); });
        fileMenu_->addSeparator();
        action(fileMenu_, "打印设置", "Printer Setup", [this] {
            QPrintDialog dialog(&printer_, this);
            dialog.setWindowTitle(trUi("打印设置", "Printer Setup"));
            dialog.exec();
        });
        auto *printAll = action(fileMenu_, "打印全部魔方", "Print All Cubes", [this] { printRows(false); });
        auto *printSelected =
            action(fileMenu_, "打印选中魔方", "Print Selected Cubes", [this] { printRows(true); });
        fileMenu_->addSeparator();
        action(fileMenu_, "保存工作区", "Save Workspace", [this] { saveFile(); });
        fileMenu_->addSeparator();
        action(fileMenu_, "退出", "Exit", [this] { close(); }, QKeySequence(Qt::ALT | Qt::Key_X));
        connect(fileMenu_, &QMenu::aboutToShow, this, [this, saveText, saveHtml, printAll, printSelected] {
            for (auto *a : {saveText, saveHtml, printAll})
                a->setEnabled(!rows_.empty());
            printSelected->setEnabled(
                std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); }));
        });
        editMenu_ = menuBar()->addMenu(trUi("编辑(&Y)", "Edit(&Y)"));
        auto *deleteSelected = action(editMenu_, "删除选中魔方", "Delete Selected Cubes",
                                      [this] { removeRows([](ResultRow *r) { return r->selected(); }); });
        editMenu_->addSeparator();
        action(editMenu_, "添加随机魔方", "Add Random Cubes", [this] { addRandom(); });
        auto *wca = action(editMenu_, "生成 WCA 打乱（原版）", "Generate WCA Scrambles (Legacy)",
                           [this] { legacyWcaScrambles(); });
        wca->setToolTip(trUi("沿用原版：切换标准配色，清空列表，生成五个随机魔方的生成公式，目标为 21 步。",
                             "Legacy WCA shortcut: standard colors, clear list, 5 random generators (target 21)."));
        editMenu_->addSeparator();
        action(editMenu_, "按图案名称排序", "Sort by Pattern Name", [this] { reorder(0); });
        action(editMenu_, "按公式长度排序", "Sort by Maneuver Length", [this] { reorder(1); });
        action(editMenu_, "按魔方 ID 排序", "Sort by Cube ID", [this] { reorder(2); });
        editMenu_->addSeparator();
        action(editMenu_, "清空主窗口", "Clear Main Window", [this] { clearMainWindow(); });
        action(editMenu_, "输入贴纸状态", "Input Facelet String", [this] { importState(); });
        connect(editMenu_, &QMenu::aboutToShow, this, [this, deleteSelected] {
            deleteSelected->setEnabled(
                std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); }));
        });
        runMenu_ = menuBar()->addMenu(trUi("运行(&Z)", "Run(&Z)"));
        auto *startSelectedAction =
            action(runMenu_, "开始搜索选中魔方", "Start Search for Selected", [this] { startSelected(); });
        auto *stopSelectedAction =
            action(runMenu_, "停止搜索选中魔方", "Stop Search for Selected", [this] { stopSelected(); });
        runMenu_->addSeparator();
        action(runMenu_, "设置自动运行线程数", "Set Autorun Threads", [this] {
            bool ok = false;
            const auto n = QInputDialog::getInt(this, trUi("自动运行线程数", "Autorun Threads"),
                                                trUi("同时运行的搜索数量", "Concurrent searches"),
                                                maxThreads_, 1, 16, 1, &ok);
            if (ok)
                maxThreads_ = n;
        });
        auto *autoTwo =
            action(runMenu_, "开始二阶段自动运行", "Start Two-Phase Autorun", [this] { autorun(false); });
        auto *autoOptimal =
            action(runMenu_, "开始最优自动运行", "Start Optimal Autorun", [this] { autorun(true); });
        runMenu_->addSeparator();
        auto *xcAction =
            action(runMenu_, "Cross 快速求解", "Cross Quick Solver", [this] { openXCrossDialog(); });
        xcAction->setObjectName("xcrossMenuAction");
        connect(runMenu_, &QMenu::aboutToShow, this,
                [this, startSelectedAction, stopSelectedAction, autoTwo, autoOptimal] {
                    const bool selected =
                        std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); });
                    startSelectedAction->setEnabled(selected);
                    stopSelectedAction->setEnabled(selected);
                    autoTwo->setText(autoRun_ && !autoOptimal_
                                         ? trUi("停止二阶段自动运行", "Stop Two-Phase Autorun")
                                         : trUi("开始二阶段自动运行", "Start Two-Phase Autorun"));
                    autoOptimal->setText(autoRun_ && autoOptimal_
                                             ? trUi("停止最优自动运行", "Stop Optimal Autorun")
                                             : trUi("开始最优自动运行", "Start Optimal Autorun"));
                    autoTwo->setEnabled(!autoRun_ || !autoOptimal_);
                    autoOptimal->setEnabled(!autoRun_ || autoOptimal_);
                });
        optionsMenu_ = menuBar()->addMenu(trUi("选项(&O)", "Options(&O)"));
        action(optionsMenu_, "二阶段算法", "Two-Phase Algorithm", [this] { solverOptions(false); });
        action(optionsMenu_, "最优求解器", "Optimal Solver", [this] { solverOptions(true); });
        perfMenu_ = optionsMenu_->addMenu(trUi("性能模式", "Performance"));
        auto *perfGroup = new QActionGroup(this);
        const std::array<std::pair<const char *, const char *>, 3> perfNames = {
            {{"静音 / 省电（约 50% 核心）", "Quiet (~50% cores)"},
             {"均衡（默认，留核防卡顿）", "Balanced (Default)"},
             {"全速（满核并发）", "Full Speed (All cores)"}}};
        for (int i = 0; i < 3; ++i) {
            perfActions_[i] = action(perfMenu_, perfNames[i].first, perfNames[i].second, [this, i] {
                set_performance_profile(static_cast<PerformanceProfile>(i));
                preferences_.setValue("Performance/Profile", i);
                if (perfActions_[i])
                    perfActions_[i]->setChecked(true);
            });
            perfActions_[i]->setCheckable(true);
            perfGroup->addAction(perfActions_[i]);
        }
        const int savedPerf = std::clamp(preferences_.value("Performance/Profile", 1).toInt(), 0, 2);
        set_performance_profile(static_cast<PerformanceProfile>(savedPerf));
        perfActions_[savedPerf]->setChecked(true);
        optionsMenu_->addSeparator();
        auto *centers = optionsMenu_->addAction(trUi("使用中心方向约束", "Use Center Orientations"));
        menuTexts_.push_back({centers, "使用中心方向约束", "Use Center Orientations"});
        centers->setCheckable(true);
        connect(centers, &QAction::toggled, this, [this](bool yes) {
            if (yes)
                centers_ = rememberedCenters_;
            else {
                rememberedCenters_ = centers_;
                centers_.fill(-1);
            }
            dirty_ = true;
            refresh();
        });
        auto *editCenters =
            action(optionsMenu_, "编辑中心方向", "Edit Center Orientations", [this] { centersDialog(); });
        connect(optionsMenu_, &QMenu::aboutToShow, this, [this, centers, editCenters] {
            const bool enabled = std::any_of(centers_.begin(), centers_.end(), [](int n) { return n >= 0; });
            const QSignalBlocker blocked(centers);
            centers->setChecked(enabled);
            editCenters->setEnabled(enabled);
            updateToggleLeftText();
        });
        optionsMenu_->addSeparator();
        nightAction_ = action(optionsMenu_, "夜晚模式", "Dark Mode", [this] { changeAppearance(!dark_); });
        nightAction_->setCheckable(true);
        viewsMenu_ = optionsMenu_->addMenu(trUi("魔方显示", "Cube View"));
        auto *viewGroup = new QActionGroup(this);
        const std::array<std::pair<const char *, const char *>, 3> viewNames = {
            {{"经典展开", "Classic"}, {"2D 展开", "2D Net"}, {"3D 魔方", "3D Cube"}}};
        for (int i = 0; i < 3; ++i) {
            viewActions_[i] =
                action(viewsMenu_, viewNames[i].first, viewNames[i].second, [this, i] { setView(i); });
            viewActions_[i]->setCheckable(true);
            viewGroup->addAction(viewActions_[i]);
        }
        viewActions_[0]->setChecked(true);
        action(optionsMenu_, "自定义选中颜色", "Customize Active Color", [this] { customColor(); });
        auto *shortcutsAction =
            action(optionsMenu_, "快捷键设置", "Keyboard Shortcuts", [this] { shortcutsDialog(); });
        shortcutsAction->setObjectName("shortcutsMenuAction");
        toggleLeftAction_ = optionsMenu_->addAction(trUi("展开主窗口", "Expand Main Window"));
        toggleLeftAction_->setObjectName("toggleMainWindowAction");
        connect(toggleLeftAction_, &QAction::triggered, this, [this] {
            if (!left_ || left_->isHidden())
                showLeft();
            else
                hideLeft();
        });
        action(optionsMenu_, "MCP 接口配置", "MCP Configuration", [this] {
            const auto path = baseDirectory() + "/mcp-config.json";
            if (!QFileInfo::exists(path) || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
                warn(trUi("无法打开 MCP 配置文件：\n", "Unable to open MCP config:\n") + path);
        });
        action(optionsMenu_, "搜索统计", "Statistics", [this] { showStatistics(); });
        helpMenu_ = menuBar()->addMenu(trUi("帮助(&H)", "Help(&H)"));
        action(
            helpMenu_, "使用说明", "Help Contents",
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(helpFilePath())); },
            QKeySequence::HelpContents);
        action(helpMenu_, "关于", "About", [this] {
            QMessageBox::about(
                this, "Cube Explorer - Modern version",
                trUi("Cube Explorer - Modern version\n"
                     "基于 Herbert Kociemba 原始 Cube Explorer\n"
                     "C++20 / Qt · GPL-3.0\n"
                     "制作：云起Arrebol",
                     "Cube Explorer - Modern version\n"
                     "Based on Herbert Kociemba's original Cube Explorer\n"
                     "C++20 / Qt · GPL-3.0\n"
                     "Created by: 云起Arrebol"));
        });
        cornerBar_ = new QWidget;
        auto *cornerLayout = new QHBoxLayout(cornerBar_);
        cornerLayout->setContentsMargins(0, 2, 6, 2);
        cornerLayout->setSpacing(4);
        langToggle_ = new QToolButton(cornerBar_);
        langToggle_->setObjectName("langToggle");
        langToggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        langToggle_->setAutoRaise(true);
        langToggle_->setCursor(Qt::PointingHandCursor);
        connect(langToggle_, &QToolButton::clicked, this, [this] { setLanguage(!english_); });
        themeToggle_ = new QToolButton(cornerBar_);
        themeToggle_->setObjectName("themeToggle");
        themeToggle_->setAutoRaise(true);
        themeToggle_->setCursor(Qt::PointingHandCursor);
        connect(themeToggle_, &QToolButton::clicked, this, [this] { changeAppearance(!dark_); });
        cornerLayout->addWidget(langToggle_);
        cornerLayout->addWidget(themeToggle_);
        menuBar()->setCornerWidget(cornerBar_, Qt::TopRightCorner);
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        if (!scalePending_ && !scaling_) {
            scalePending_ = true;
            QTimer::singleShot(16, this, [this] {
                scalePending_ = false;
                updateScale();
            });
        }
    }
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::KeyPress && QApplication::activeWindow() == this) {
            auto *key = static_cast<QKeyEvent *>(event);
            if ((loading_ && key->key() == Qt::Key_Escape) ||
                (key->key() == Qt::Key_Delete &&
                 std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); })) ||
                (key->key() == Qt::Key_F && key->modifiers().testFlag(Qt::ControlModifier))) {
                keyPressEvent(key);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress &&
            (object == ordinaryScroll_->viewport() || object == ordinaryScroll_->widget())) {
            if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
                clearSelection();
            } else if (static_cast<QMouseEvent *>(event)->button() == Qt::RightButton) {
                backgroundMenu(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
                return true;
            }
        }
        return QMainWindow::eventFilter(object, event);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if (loading_ && e->key() == Qt::Key_Escape) {
            cancelLoad_ = true;
            e->accept();
        } else if (e->key() == Qt::Key_Delete &&
                   std::any_of(rows_.begin(), rows_.end(), [](auto *r) { return r->selected(); })) {
            removeRows([](auto *r) { return r->selected(); });
            e->accept();
        } else if (e->key() == Qt::Key_F && e->modifiers().testFlag(Qt::ControlModifier)) {
            saveDefaultSolver();
            e->accept();
        } else
            QMainWindow::keyPressEvent(e);
    }
    void closeEvent(QCloseEvent *e) override {
        if (loading_) {
            cancelLoad_ = true;
            e->ignore();
            return;
        }
        if (!closing_) {
            const auto answer =
                QMessageBox::question(this, "退出 Cube Explorer",
                                      dirty_ ? "是否保存主窗口内容后退出？" : "是否退出 Cube Explorer？",
                                      dirty_ ? QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel
                                             : QMessageBox::Yes | QMessageBox::No);
            if ((dirty_ && answer == QMessageBox::Cancel) || (!dirty_ && answer != QMessageBox::Yes) ||
                (dirty_ && answer == QMessageBox::Yes && !saveFile())) {
                e->ignore();
                return;
            }
        }
        closing_ = true;
        autoRun_ = false;
        autorunTimer_.stop();
        replayTimer_.stop();
        for (auto *r : rows_)
            r->stop();
        if (formulaPanel_)
            formulaPanel_->stopSearch();
        preferences_.sync();
        QMainWindow::closeEvent(e);
    }

  public:
    explicit MainWindow() : preferences_(settingsFile(), QSettings::IniFormat) {
        if (QApplication::instance()->property("testMode").toBool())
            preferences_.clear();
        centers_.fill(-1);
        const std::array<QColor, 6> oldDefaults = {QColor("#ffff00"), QColor("#008000"), QColor("#ff0000"),
                                                   QColor("#ffffff"), QColor("#0000ff"), QColor("#ff8000")};
        for (int i = 0; i < 6; ++i) {
            const auto key = QString("Colors/%1").arg(face_names[i]);
            const QColor saved(preferences_.value(key).toString());
            if (saved.isValid() && saved != oldDefaults[i])
                colors_[i] = saved;
        }
        dark_ = preferences_.value("Appearance/Dark", false).toBool();
        english_ = preferences_.value("Appearance/English", false).toBool();
        setUiEnglish(english_);
        qApp->installEventFilter(this);
        setWindowTitle("untitled - Cube Explorer Modern");
        setMinimumSize(380, 530);
        resize(510, 730);
        createMenus();
        split_ = new QSplitter;
        split_->setChildrenCollapsible(false);
        setCentralWidget(split_);
        ordinaryScroll_ = new QScrollArea;
        ordinaryScroll_->setObjectName("mainResults");
        ordinaryScroll_->setWidgetResizable(true);
        ordinaryScroll_->setMinimumWidth(150);
        auto *ordinaryBody = new QWidget;
        ordinaryBody->setObjectName("mainResultsBody");
        ordinaryRows_ = new QVBoxLayout(ordinaryBody);
        ordinaryRows_->setContentsMargins(0, 0, 0, 0);
        ordinaryRows_->setSpacing(0);
        ordinaryRows_->addStretch();
        ordinaryScroll_->setWidget(ordinaryBody);
        left_ = ordinaryScroll_;
        split_->addWidget(left_);

        right_ = new QWidget;
        right_->setObjectName("stickerEditor");
        right_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        right_->setMaximumWidth(510);
        auto *layout = new QVBoxLayout(right_);
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        layout->setContentsMargins(6, 6, 6, 8);
        layout->setSpacing(6);
        editorView_ = new OriginalEditorView;
        editorView_->setAutoFix(preferences_.value("Editor/AutoFix", true).toBool());
        editorView_->onAutoFix = [this](bool value) { preferences_.setValue("Editor/AutoFix", value); };
        editorView_->onSticker = [this](int index, Qt::MouseButton button, Qt::KeyboardModifiers mods) {
            editSticker(index, button, mods);
        };
        editorView_->onViewChanged = [this](int view) {
            currentView_ = std::clamp(view, 0, 2);
            if (currentView_ >= 0 && currentView_ < static_cast<int>(viewActions_.size()))
                viewActions_[currentView_]->setChecked(true);
            preferences_.setValue("Appearance/View", currentView_);
            const double yaw = editorView_->canvas()->yaw();
            const double pitch = editorView_->canvas()->pitch();
            for (auto *row : rows_) {
                row->setViewMode(currentView_);
                row->setCamera(yaw, pitch, 1.0);
            }
        };
        editorView_->canvas()->onCameraChanged = [this](double yaw, double pitch, double) {
            for (auto *row : rows_)
                row->setCamera(yaw, pitch, 1.0);
        };
        layout->addWidget(editorView_);

        // Compact Agent-style quick speedcubing presets & XC/XXC shortcut strip
        auto *presetRow = new QHBoxLayout;
        presetRow->setContentsMargins(2, 0, 2, 0);
        presetRow->setSpacing(5);
        auto makeChip = [](const char *objectName, bool accent = false) {
            auto *chip = new QToolButton;
            chip->setObjectName(objectName);
            chip->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            chip->setProperty("presetChip", true);
            if (accent)
                chip->setProperty("presetAccent", true);
            chip->setCursor(Qt::PointingHandCursor);
            return chip;
        };
        presetZbls_ = makeChip("presetZbls");
        presetOll_ = makeChip("presetOll");
        presetF2l_ = makeChip("presetF2l");
        presetOllcp_ = makeChip("presetOllcp");
        presetXCross_ = makeChip("presetXCross", true);
        connect(presetZbls_, &QToolButton::clicked, this, [this] { applyZblsPreset(); });
        connect(presetOll_, &QToolButton::clicked, this, [this] { applyOllPreset(); });
        connect(presetF2l_, &QToolButton::clicked, this, [this] { applyF2lPreset(); });
        connect(presetOllcp_, &QToolButton::clicked, this, [this] { applyOllcpPreset(); });
        connect(presetXCross_, &QToolButton::clicked, this, [this] { openXCrossDialog(); });
        presetRow->addWidget(presetZbls_);
        presetRow->addWidget(presetOll_);
        presetRow->addWidget(presetF2l_);
        presetRow->addWidget(presetOllcp_);
        presetRow->addStretch();
        presetRow->addWidget(presetXCross_);
        layout->addLayout(presetRow);

        auto *controls = new QHBoxLayout;
        controls->setSpacing(5);
        selectBox_ = new QGroupBox("选择");
        auto *selectLayout = new QVBoxLayout(selectBox_);
        selectLayout->setContentsMargins(6, 15, 6, 6);
        selectLayout->setSpacing(6);
        solve_ = new QPushButton("求解当前情况");
        generate_ = new QPushButton("生成当前情况");
        solve_->setObjectName("solveCurrent");
        generate_->setObjectName("generateCurrent");
        for (auto *b : {solve_, generate_}) {
            b->setCursor(Qt::PointingHandCursor);
            b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            selectLayout->addWidget(b);
        }
        connect(solve_, &QPushButton::clicked, this, [this] { chooseMode(false); });
        connect(generate_, &QPushButton::clicked, this, [this] { chooseMode(true); });
        controls->addWidget(selectBox_, 31);
        moveBox_ = new QGroupBox("转动");
        auto *moveLayout = new QGridLayout(moveBox_);
        moveLayout->setContentsMargins(6, 15, 6, 6);
        moveLayout->setSpacing(3);
        constexpr std::string_view moves = "RUFLDBrufldbMESxyz";
        for (int i = 0; i < 18; ++i) {
            auto *button = new MoveButton(moves[i]);
            button->setCursor(Qt::PointingHandCursor);
            button->onMove = [this](Move m) { applyMove(m); };
            moveLayout->addWidget(button, i / 3, i % 3);
        }
        controls->addWidget(moveBox_, 36);
        resetBox_ = new QGroupBox("重置");
        auto *resetLayout = new QGridLayout(resetBox_);
        resetLayout->setContentsMargins(6, 15, 6, 6);
        resetLayout->setSpacing(4);
        const std::array<QString, 3> resetNames = {"空白", "复原", "随机"};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) {
                auto *b = new QPushButton(col == 0 ? resetNames[row] : col == 1 ? "角" : "棱");
                b->setProperty("resetKind", row);
                b->setProperty("resetPieces", col);
                b->setObjectName("resetButton");
                b->setCursor(Qt::PointingHandCursor);
                b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                b->setMinimumWidth(col ? 25 : 62);
                if (col)
                    b->setMaximumWidth(27);
                b->setStyleSheet("padding:2px 1px;");
                resetButtons_[row * 3 + col] = b;
                resetLayout->addWidget(b, row, col);
                connect(b, &QPushButton::clicked, this, [this, row, col] {
                    resetEditor(static_cast<editor::Reset>(row), static_cast<editor::Pieces>(col));
                });
            }
        resetLayout->setColumnStretch(0, 3);
        controls->addWidget(resetBox_, 33);
        layout->addLayout(controls);
        inputBox_ = new QGroupBox("输入公式");
        inputBox_->setObjectName("formulaInputGroup");
        auto *inputLayout = new QHBoxLayout(inputBox_);
        inputLayout->setContentsMargins(7, 15, 7, 7);
        inputLayout->setSpacing(5);
        formulaInput_ = new QLineEdit;
        formulaInput_->setObjectName("formulaInput");
        inverseButton_ = new QPushButton("逆");
        inverseButton_->setObjectName("inverseApplyButton");
        inverseButton_->setCursor(Qt::PointingHandCursor);
        applyButton_ = new QPushButton("应用");
        applyButton_->setObjectName("applyFormulaButton");
        applyButton_->setCursor(Qt::PointingHandCursor);
        inputLayout->addWidget(formulaInput_, 1);
        inputLayout->addWidget(inverseButton_);
        inputLayout->addWidget(applyButton_);
        layout->addWidget(inputBox_);
        auto applyText = [this] {
            try {
                applyFormula(formulaInput_->text());
            } catch (const std::exception &e) {
                warn(friendlyError(e.what()));
            }
        };
        auto applyInverseText = [this] {
            try {
                applyInverseFormula(formulaInput_->text());
            } catch (const std::exception &e) {
                warn(friendlyError(e.what()));
            }
        };
        connect(applyButton_, &QPushButton::clicked, this, applyText);
        connect(inverseButton_, &QPushButton::clicked, this, applyInverseText);
        connect(formulaInput_, &QLineEdit::returnPressed, this, applyText);
        for (auto *childLayout : right_->findChildren<QLayout *>())
            if (!childLayout->parentWidget() || !editorView_->isAncestorOf(childLayout->parentWidget()))
                scalableLayouts_.push_back({childLayout, childLayout->contentsMargins(), childLayout->spacing()});
        split_->addWidget(right_);
        left_->hide();
        statusBar()->hide();
        connect(&replayTimer_, &QTimer::timeout, this, [this] { replayStep(); });
        connect(&autorunTimer_, &QTimer::timeout, this, [this] { autorunTick(); });
        setupShortcuts();
        const int savedView = std::clamp(preferences_.value("Appearance/View", 0).toInt(), 0, 2);
        setView(savedView);
        retranslateUi();
        changeAppearance(dark_);
        updateScale(true);
        refresh();
        updateBusy();
    }
    ~MainWindow() override {
        closing_ = true;
        for (auto *row : rows_) {
            row->onBusy = {};
            row->onFinished = {};
            row->stop();
        }
        if (formulaPanel_) {
            formulaPanel_->onBusy = {};
            formulaPanel_->stopSearch();
            delete formulaPanel_;
        }
        for (auto *row : rows_)
            delete row;
        rows_.clear();
    }
    PatternState pattern() const {
        auto p = parse_pattern_facelets(raw_);
        p.center_ori = centers_;
        const auto valid = validate_pattern(p);
        if (!valid)
            throw std::invalid_argument(valid.message);
        return p;
    }
    void restore(const PatternState &p) {
        replayTimer_.stop();
        raw_ = format_pattern_facelets(p);
        centers_ = p.center_ori;
        for (int i = 0; i < 6; ++i)
            rememberedCenters_[i] = std::max<int>(0, centers_[i]);
        dirty_ = true;
        refresh();
    }
    void editSticker(int index, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
        replayTimer_.stop();
        if (index < 0 || index >= 54)
            return;
        if (index % 9 == 4) {
            if (button == Qt::LeftButton) {
                const auto picked = static_cast<char>(std::toupper(static_cast<unsigned char>(raw_[index])));
                if (face_names.find(picked) == std::string_view::npos)
                    return;
                brush_ = picked;
                refresh();
            } else if (std::any_of(centers_.begin(), centers_.end(), [](int n) { return n >= 0; }))
                centersDialog();
            return;
        }
        if (button == Qt::LeftButton) {
            raw_[index] = modifiers.testFlag(Qt::ShiftModifier)
                              ? static_cast<char>(std::tolower(static_cast<unsigned char>(brush_)))
                          : modifiers.testFlag(Qt::ControlModifier) ? '@'
                                                                    : brush_;
            if (editorView_->autoFix()) {
                const auto corrected = editor::check_facelet(raw_, index, false);
                if (!corrected.corrections.empty()) {
                    QStringList messages;
                    for (auto c : corrected.corrections) {
                        if (c == editor::Correction::CornerTwist)
                            messages << "角块总扭转已修正。";
                        if (c == editor::Correction::EdgeFlip)
                            messages << "棱块总翻转已修正。";
                        if (c == editor::Correction::PermutationParity)
                            messages << "角棱置换奇偶不一致，已修正。";
                    }
                    refresh();
                    warn(messages.join("\n"));
                }
            }
        } else
            raw_[index] = '?';
        dirty_ = true;
        refresh();
    }
    void applyMove(Move move) {
        replayTimer_.stop();
        const std::array<Move, 1> moves = {move};
        applyEditorMoves(moves);
        dirty_ = true;
        refresh();
    }
    void applyFormula(const QString &text) {
        replayTimer_.stop();
        const auto moves = editor::parse_legacy_moves(text.toStdString());
        applyEditorMoves(moves);
        dirty_ = true;
        refresh();
    }
    void applyInverseFormula(const QString &text) {
        replayTimer_.stop();
        const auto moves = cube::inverse(editor::parse_legacy_moves(text.toStdString()));
        applyEditorMoves(moves);
        dirty_ = true;
        refresh();
    }
    // Builds a clean solved base preserving the current center-color orientation.
    std::string solvedBaseKeepingFrame() const {
        std::string s(54, '?');
        for (int face = 0; face < 6; ++face) {
            const char center = static_cast<char>(std::toupper(static_cast<unsigned char>(raw_[face * 9 + 4])));
            const char fill = face_names.find(center) == std::string_view::npos ? face_names[face] : center;
            for (int tile = 0; tile < 9; ++tile)
                s[face * 9 + tile] = fill;
        }
        return s;
    }
    void applyZblsPreset() {
        replayTimer_.stop();
        raw_ = solvedBaseKeepingFrame();
        // Mask top-layer corners and edge side stickers to '?', mark 4 U edges with Ctrl '@'
        for (int idx : {0, 2, 6, 8, 9, 10, 11, 18, 19, 20, 36, 37, 38, 45, 46, 47})
            raw_[idx] = '?';
        for (int idx : {1, 3, 5, 7})
            raw_[idx] = '@';
        dirty_ = true;
        refresh();
    }
    void applyOllPreset() {
        replayTimer_.stop();
        raw_ = solvedBaseKeepingFrame();
        // Mask top-layer sides to '?', mark all 8 U-face non-center stickers with Ctrl '@'
        for (int idx : {9, 10, 11, 18, 19, 20, 36, 37, 38, 45, 46, 47})
            raw_[idx] = '?';
        for (int idx : {0, 1, 2, 3, 5, 6, 7, 8})
            raw_[idx] = '@';
        dirty_ = true;
        refresh();
    }
    void applyF2lPreset() {
        replayTimer_.stop();
        raw_ = solvedBaseKeepingFrame();
        for (int idx : {0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 18, 19, 20, 36, 37, 38, 45, 46, 47})
            raw_[idx] = '?';
        dirty_ = true;
        refresh();
    }
    void applyOllcpPreset() {
        replayTimer_.stop();
        raw_ = solvedBaseKeepingFrame();
        // F2L + all 4 U-layer corners remain full color; 4 U-layer edges keep '@' orientation only.
        for (int idx : {10, 19, 37, 46})
            raw_[idx] = '?';
        for (int idx : {1, 3, 5, 7})
            raw_[idx] = '@';
        dirty_ = true;
        refresh();
    }
    void openXCrossDialog() {
        replayTimer_.stop();
        CubeState baseCube;
        try {
            const auto p = pattern();
            if (ordinaryPattern(p))
                baseCube = completeCube(p);
        } catch (const std::exception &) {
            // If current editor is incomplete/partial, fall back to solved cube so user can type a scramble.
        }
        XCrossDialog dialog(baseCube, colors_, uiScale_, this);
        if (dialog.exec() != QDialog::Accepted || !dialog.built())
            return;
        restore(dialog.built()->pattern);
        if (dialog.openSolver()) {
            auto *win = openFormula(dialog.built()->pattern, false);
            if (win) {
                win->setModuloU(false);
                win->setPreRotationMode(dialog.preRotationMode());
            }
        }
    }
    void resetEditor(editor::Reset reset, editor::Pieces pieces = editor::Pieces::All) {
        replayTimer_.stop();
        const auto seed = std::random_device{}();
        auto shadow = raw_;
        editor::reset(shadow, rememberedCenters_, reset, pieces, seed);
        editor::reset(raw_, centers_, reset, pieces, seed);
        dirty_ = true;
        refresh();
    }
    ResultRow *addOrdinary(PatternState input, bool generate) {
        showLeft(0);
        auto *row = new ResultRow(std::move(input), generate);
        attachRow(row);
        return row;
    }
    ResultRow *addFormula(ResultData data) {
        showLeft();
        auto *row = new ResultRow(data.target, data.generate, true);
        attachRow(row);
        row->setFormulaResult(std::move(data));
        return row;
    }
    FormulaWindow *openFormula(PatternState input, bool generate) {
        if (formulaPanel_ && formulaPanel_->busy()) {
            formulaPanel_->show();
            formulaPanel_->raise();
            formulaPanel_->activateWindow();
            return formulaPanel_;
        }
        if (formulaPanel_)
            delete formulaPanel_;
        formulaPanel_ = new FormulaWindow(
            std::move(input), generate, [this](ResultData data) { addFormula(std::move(data)); }, this);
        formulaPanel_->onBusy = [this](bool) { updateBusy(); };
        formulaPanel_->show();
        formulaPanel_->raise();
        formulaPanel_->activateWindow();
        return formulaPanel_;
    }
    void startReplay(ResultData data) {
        replayTimer_.stop();
        replay_ = std::move(data);
        replayPosition_ = 0;
        restore(replay_->start);
        replayTimer_.start(400);
        replayStep();
    }
    void replayStep() {
        if (!replay_)
            return;
        if (replayPosition_ >= replay_->moves.size()) {
            replayTimer_.stop();
            statusBar()->showMessage(QString("回放完成：%1 步").arg(replay_->turns));
            return;
        }
        const auto move = replay_->moves[replayPosition_++];
        const std::array<Move, 1> moves = {move};
        applyEditorMoves(moves);
        refresh();
        statusBar()->showMessage(QString("%1 / %2 · %3")
                                     .arg(replayPosition_)
                                     .arg(replay_->moves.size())
                                     .arg(qstr(format_move(move))));
    }
    void savePath(const QString &path, bool legacy = false) {
        QByteArray bytes;
        if (legacy) {
            for (auto *row : rows_) {
                if (row->isFormula())
                    throw std::invalid_argument("公式模式需要保存工作区，以保留部分约束和模 U 方向。");
                if (auto d = row->result(true)) {
                    const auto generator = d->generate ? d->moves : cube::inverse(d->moves);
                    bytes += (qstr(format_moves(generator)) + QString(" (%1f%2)   //%3\r\n")
                                                                  .arg(d->turns)
                                                                  .arg(d->optimalityProven ? "*" : "")
                                                                  .arg(row->patternName()))
                                 .toUtf8();
                } else {
                    const auto stickers = format_pattern_facelets(row->input());
                    QString line;
                    for (int i : editor::legacy_facelet_order)
                        line += stickers[i] == '?' ? QChar('N') : QChar(stickers[i]);
                    bytes += (line + "   //" + row->patternName() + "\r\n").toUtf8();
                }
            }
        } else {
            QJsonArray records;
            for (auto *row : rows_) {
                QJsonObject r{{"pattern", patternJson(row->input())},
                              {"name", row->patternName()},
                              {"generate", row->generated()},
                              {"formula_mode", row->isFormula()}};
                if (auto d = row->result(true)) {
                    r["moves"] = qstr(format_moves(d->moves));
                    r["turns"] = d->turns;
                    r["rotations"] = d->rotations;
                    r["optimality_proven"] = d->optimalityProven;
                    r["start"] = patternJson(d->start);
                    r["terminal"] = patternJson(d->terminal);
                }
                records.append(r);
            }
            QJsonArray centers, remembered;
            for (auto x : centers_)
                centers.append(static_cast<int>(x));
            for (auto x : rememberedCenters_)
                remembered.append(static_cast<int>(x));
            bytes = QJsonDocument(QJsonObject{{"format", "CubeExplorerModern"},
                                              {"version", 3},
                                              {"editor", qstr(raw_)},
                                              {"centers", centers},
                                              {"remembered_centers", remembered},
                                              {"records", records}})
                        .toJson();
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("无法安全保存文件，原文件未被替换");
        dirty_ = false;
    }
    void loadPath(const QString &path, bool append = false) {
        if (loading_)
            throw std::runtime_error("正在加载文件");
        loading_ = true;
        cancelLoad_ = false;
        auto resetLoading = qScopeGuard([this] { loading_ = false; });
        int checked = 0;
        const auto pollLoad = [this, &checked] {
            if (++checked % 64 == 0)
                QApplication::processEvents();
            if (cancelLoad_)
                throw std::runtime_error("已取消加载；原窗口内容保留。");
        };
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("无法打开文件");
        const auto bytes = file.readAll();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        struct Entry {
            PatternState p;
            QString name;
            bool generate = true, formula = false;
            std::optional<ResultData> result;
        };
        std::vector<Entry> pending;
        std::optional<std::string> savedEditor;
        auto savedCenters = centers_;
        auto savedRemembered = rememberedCenters_;
        if (error.error == QJsonParseError::NoError && document.isObject()) {
            const auto object = document.object();
            if (object.value("format") != "CubeExplorerModern")
                throw std::invalid_argument("不是 Cube Explorer 工作区");
            const auto editor = object.value("editor").toString().toStdString();
            if (editor.size() != 54 || editor.find_first_not_of("URFDLBurfdlb?@.") != std::string::npos)
                throw std::invalid_argument("工作区编辑贴纸无效");
            savedEditor = editor;
            const auto centers = object.value("centers").toArray();
            if (centers.size() != 6)
                throw std::invalid_argument("工作区中心数组无效");
            for (int i = 0; i < 6; ++i) {
                if (!centers[i].isDouble() || centers[i].toDouble() != centers[i].toInt() ||
                    centers[i].toInt() < -1 || centers[i].toInt() > 3)
                    throw std::invalid_argument("工作区中心朝向无效");
                savedCenters[i] = static_cast<std::int8_t>(centers[i].toInt());
                savedRemembered[i] = static_cast<std::int8_t>(std::max(0, centers[i].toInt()));
            }
            if (object.contains("remembered_centers")) {
                const auto remembered = object.value("remembered_centers").toArray();
                if (remembered.size() != 6)
                    throw std::invalid_argument("工作区保存的中心数组无效");
                for (int i = 0; i < 6; ++i) {
                    if (!remembered[i].isDouble() || remembered[i].toDouble() != remembered[i].toInt() ||
                        remembered[i].toInt() < -1 || remembered[i].toInt() > 3)
                        throw std::invalid_argument("工作区保存的中心朝向无效");
                    savedRemembered[i] = static_cast<std::int8_t>(remembered[i].toInt());
                }
            }
            auto centerCheck = *savedEditor;
            editor::apply_moves_raw(centerCheck, savedCenters, {});
            for (const auto value : object.value("records").toArray()) {
                pollLoad();
                const auto r = value.toObject();
                Entry e;
                e.p = readPattern(r.value("pattern").toObject());
                e.name = r.value("name").toString();
                e.generate = r.value("generate").toBool();
                e.formula = r.value("formula_mode").toBool();
                if (r.contains("moves")) {
                    ResultData d;
                    d.target = e.p;
                    d.moves = parse_moves(r.value("moves").toString().toStdString());
                    d.turns = r.value("turns").toInt();
                    d.rotations = r.value("rotations").toInt();
                    d.formula = e.formula;
                    d.generate = e.generate;
                    d.optimalityProven = false;
                    d.start = readPattern(r.value("start").toObject());
                    d.terminal = readPattern(r.value("terminal").toObject());
                    if (turn_count(d.moves) != d.turns || rotation_count(d.moves) != d.rotations ||
                        apply_pattern(d.start, d.moves) != d.terminal)
                        throw std::invalid_argument("文件中的公式、终态或计步不一致");
                    if (d.formula) {
                        PatternRequest request;
                        request.pattern = e.p;
                        request.direction = e.generate ? PatternDirection::Generate : PatternDirection::Solve;
                        request.allowed_moves = all_moves();
                        request.max_turns = 40;
                        request.reject_alternating_triples = false;
                        request.remove_u_symmetric_prefix = false;
                        request.modulo_u = true;
                        PatternSolution solution;
                        solution.moves = d.moves;
                        solution.turn_cost = d.turns;
                        solution.rotations = d.rotations;
                        solution.terminal = d.terminal;
                        bool verified = false;
                        for (int u = 0; u < 4; ++u) {
                            solution.residual_u = u;
                            if (verify_pattern_solution(request, solution)) {
                                verified = true;
                                break;
                            }
                        }
                        if (!verified)
                            throw std::invalid_argument("文件公式未满足图案与求解方向的目标");
                        PatternState expectedStart = e.p;
                        if (e.generate) {
                            CubeState start;
                            start.frame = e.p.frame;
                            expectedStart = pattern_from_cube(start);
                            for (int i = 0; i < 6; ++i)
                                if (e.p.center_ori[i] >= 0)
                                    expectedStart.center_ori[i] = 0;
                        }
                        if (d.start != expectedStart)
                            throw std::invalid_argument("文件公式起点与方向不一致");
                    } else {
                        auto input = completeCube(e.p);
                        auto solveMoves = e.generate ? cube::inverse(d.moves) : d.moves;
                        auto end = cube::apply(input, solveMoves);
                        if (!is_solved(end) || !verify_sequence(input, solveMoves, end))
                            throw std::invalid_argument("文件中的普通公式不能精确复原目标");
                    }
                    e.result = d;
                }
                pending.push_back(std::move(e));
            }
        } else {
            for (auto line : QString::fromUtf8(bytes).split('\n')) {
                pollLoad();
                const auto comment = line.indexOf("//");
                QString name;
                if (comment >= 0) {
                    name = line.mid(comment + 2).trimmed();
                    line = line.left(comment);
                }
                line.remove(QRegularExpression("\\([0-9]+[fq]?\\*?\\)"));
                line = line.trimmed();
                if (line.isEmpty())
                    continue;
                Entry e;
                e.name = name.left(30);
                if (auto raw = editor::parse_legacy_facelets(line.toStdString())) {
                    e.p = parse_pattern_facelets(*raw);
                    e.formula = !ordinaryPattern(e.p);
                    pending.push_back(std::move(e));
                    continue;
                }
                auto moves = editor::parse_legacy_moves(line.toStdString());
                CubeState start;
                const auto target = cube::apply(start, moves);
                e.p = pattern_from_cube(target);
                ResultData d;
                d.target = e.p;
                d.start = pattern_from_cube(start);
                d.terminal = e.p;
                d.moves = moves;
                d.turns = turn_count(moves);
                d.rotations = rotation_count(moves);
                d.formula = false;
                d.generate = true;
                e.result = d;
                pending.push_back(std::move(e));
            }
        }
        if (!append)
            removeRows([](ResultRow *) { return true; });
        for (auto &e : pending) {
            auto *row = new ResultRow(e.p, e.generate, e.formula);
            attachRow(row);
            row->setPatternName(e.name);
            if (e.result)
                row->restoreResult(*e.result);
        }
        if (savedEditor && !append) {
            raw_ = *savedEditor;
            centers_ = savedCenters;
            rememberedCenters_ = savedRemembered;
            refresh();
        }
        if (!pending.empty())
            showLeft(pending.back().formula ? 1 : 0);
        if (!append)
            dirty_ = false;
    }
    // The UI harness calls the same handlers and real buttons as interactive use.
    const std::string &rawFacelets() const {
        return raw_;
    }
    const std::array<std::int8_t, 6> &centerOrientations() const {
        return centers_;
    }
    char brush() const {
        return brush_;
    }
    bool leftVisible() const {
        return !left_->isHidden();
    }
    void collapseResults() {
        hideLeft();
    }
    bool dark() const {
        return dark_;
    }
    void setDark(bool value) {
        changeAppearance(value);
    }
    bool english() const {
        return english_;
    }
    void setEnglish(bool value) {
        setLanguage(value);
    }
    void setAutoFix(bool value) {
        editorView_->setAutoFix(value);
    }
    void setView(int index) {
        currentView_ = std::clamp(index, 0, 2);
        editorView_->setView(currentView_);
        viewActions_.at(currentView_)->setChecked(true);
        preferences_.setValue("Appearance/View", currentView_);
        const double yaw = editorView_->canvas()->yaw();
        const double pitch = editorView_->canvas()->pitch();
        for (auto *row : rows_) {
            row->setViewMode(currentView_);
            row->setCamera(yaw, pitch, 1.0);
        }
    }
    int currentView() const {
        return currentView_;
    }
    QString helpFilePath() const {
        const QString rel = english_ ? "/help/index.en.html" : "/help/index.html";
        const QString installed = baseDirectory() + rel;
        if (QFileInfo::exists(installed))
            return installed;
        const QString source = QString::fromUtf8(CUBE_PROJECT_ROOT) + rel;
        if (QFileInfo::exists(source))
            return source;
        const QString fallback = baseDirectory() + "/help/index.html";
        return QFileInfo::exists(fallback) ? fallback : QString::fromUtf8(CUBE_PROJECT_ROOT) + "/help/index.html";
    }
    QKeySequence shortcutFor(const QString &id) const {
        for (const auto &binding : shortcuts_)
            if (binding.id == id)
                return binding.currentKey;
        return {};
    }
    void setShortcutFor(const QString &id, const QKeySequence &seq) {
        for (auto &binding : shortcuts_)
            if (binding.id == id) {
                applyShortcutKey(binding, seq, true);
                retranslateUi();
                return;
            }
    }
    const std::vector<ResultRow *> &rows() const {
        return rows_;
    }
    CubeCanvas *canvas() const {
        return editorView_->canvas();
    }
};
