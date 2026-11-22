#include "../src/app/formula_window.hpp"
#include "../src/app/app_theme.hpp"
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void waitIdle(FormulaWindow &window) {
    QElapsedTimer timer;
    timer.start();
    while (window.busy() && timer.elapsed() < 10000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    require(!window.busy(), "Search did not actually stop within timeout");
    QApplication::processEvents();
}
QPushButton *button(FormulaWindow &window, const char *name) {
    auto *result = window.findChild<QPushButton *>(name);
    require(result, "Missing original-style action button");
    return result;
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
    for (const auto *name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "segoeuib.ttf", "consola.ttf"})
        QFontDatabase::addApplicationFont(fonts + name);
    applyAppTheme(app, false);
    try {
        std::vector<ResultData> transferred;
        FormulaWindow window(pattern_from_cube(CubeState{}), false,
                             [&](ResultData data) { transferred.push_back(std::move(data)); });
        window.show();
        QApplication::processEvents();
        auto *output = window.findChild<QPlainTextEdit *>("formulaOutput");
        auto *advanced = window.findChild<QWidget *>("formulaAdvancedSettings");
        auto *start = button(window, "formulaStart");
        auto *clear = button(window, "formulaClear");
        auto *add = button(window, "formulaAdd");
        require(window.isWindow(), "Formula mode must be a separate window");
        require(window.windowFlags().testFlag(Qt::WindowMinimizeButtonHint),
                "Formula window must expose a native minimize button");
        window.showMinimized();
        QApplication::processEvents();
        require(window.isMinimized() && !window.busy(), "Minimizing must work without starting a search");
        window.showNormal();
        QApplication::processEvents();
        require(!window.isMinimized(), "Formula window must restore from minimization");
        require(!window.busy() && window.startText() == "开始搜索",
                "Window opened with search already running");
        const auto defaults = window.request();
        require(defaults.max_turns == -1 && defaults.max_results == 0 && defaults.max_nodes == 0 &&
                    defaults.time_limit.count() == 0 && !defaults.collect_solutions,
                "UI defaults must be unlimited and use streaming core results without duplicate storage");
        require(output && output->toPlainText().isEmpty(), "New output must initially be empty");
        require(window.findChildren<QProgressBar *>().isEmpty(),
                "Original-style formula window has no activity strip");
        require(advanced && advanced->isHidden(), "Advanced settings must initially be collapsed");
        require(window.size() == QSize(550, 670),
                "Popup dimensions must follow the supplied original-style layout");
        require(output->height() > window.height() * 0.5, "Text output lost its dominant original position");
        auto *slices = window.findChild<QCheckBox *>("formulaCategory_0");
        auto *wideCategory = window.findChild<QCheckBox *>("formulaCategory_1");
        auto *rotations = window.findChild<QCheckBox *>("formulaCategory_2");
        require(slices->text() == "允许中层转动" && wideCategory->text() == "允许双层转动" &&
                    rotations->text() == "允许整体旋转",
                "Full original category labels must be retained");
        const auto slicePosition = slices->mapTo(&window, QPoint{});
        const auto widePosition = wideCategory->mapTo(&window, QPoint{});
        const auto rotationPosition = rotations->mapTo(&window, QPoint{});
        require(slicePosition.x() == widePosition.x() && widePosition.x() == rotationPosition.x() &&
                    slicePosition.y() < widePosition.y() && widePosition.y() < rotationPosition.y(),
                "Category master controls must use three vertical lines");
        require(start->width() == qRound(240 * window.uiScale()) && clear->width() == start->width() &&
                    add->width() == start->width(),
                "Original right action column must retain its 240-pixel width");
        require(start->mapTo(&window, QPoint{}).x() == clear->mapTo(&window, QPoint{}).x() &&
                    clear->mapTo(&window, QPoint{}).x() == add->mapTo(&window, QPoint{}).x(),
                "Three action buttons must share a right column");
        require(start->mapTo(&window, QPoint{}).y() < clear->mapTo(&window, QPoint{}).y() &&
                    clear->mapTo(&window, QPoint{}).y() < add->mapTo(&window, QPoint{}).y(),
                "Original button order changed");
        for (char c : std::string("URFDLB"))
            require(window.findChild<QCheckBox *>("formulaMove_" + QString(c))->isEnabled(),
                    "Outer layer controls unavailable");
        for (char c : std::string("urfdlbMESxyz"))
            require(!window.findChild<QCheckBox *>("formulaMove_" + QString(c))->isEnabled() &&
                        !window.findChild<QCheckBox *>("formulaMove_" + QString(c))->isChecked(),
                    "Optional moves must be disabled and initially unchecked");
        for (auto *category : {slices, wideCategory, rotations})
            category->setChecked(true);
        for (char c : std::string("urfdlbMESxyz"))
            require(window.findChild<QCheckBox *>("formulaMove_" + QString(c))->isEnabled() &&
                        !window.findChild<QCheckBox *>("formulaMove_" + QString(c))->isChecked(),
                    "Enabling a category must not automatically include its moves");
        require(window.request().allowed_moves.size() == 18,
                "Empty optional groups do not change real search moves");
        for (auto *category : {slices, wideCategory, rotations})
            category->setChecked(false);

        window.smokeConfigure(pattern_from_cube(CubeState{}), 0);
        start->click();
        require(window.busy() && window.startText() == "停止搜索", "Start did not change real search state");
        require(!clear->isEnabled(), "Clear must be disabled while searching");
        waitIdle(window);
        require(window.resultCount() == 1 && transferred.empty(),
                "Search results were not buffered exclusively in formula window");
        require(window.lastReport() && window.lastReport()->results == 1 &&
                    window.lastReport()->solutions.empty(),
                "Core report must count results without collecting a duplicate result vector");
        require(output->toPlainText().contains("计算开始") &&
                    output->toPlainText().contains("正在搜索深度 0") &&
                    output->toPlainText().contains("(0f*)") && output->toPlainText().contains("计算完成"),
                "Chinese append-only messages, depth groups and proven zero-step mark must be present");
        require(!output->toPlainText().isEmpty() && add->isEnabled(),
                "Verified result was not shown or addable");
        require(window.startText() == "开始搜索", "Finished search cannot be restarted");
        add->click();
        require(transferred.size() == 1 && window.resultCount() == 1,
                "Add must copy the buffer without clearing it");
        add->click();
        require(transferred.size() == 2, "Repeated Add must copy all results again, as in original");
        require(transferred.front().moves.empty() && transferred.front().turns == 0,
                "Zero-step result was fabricated or miscounted");
        require(transferred.front().optimalityProven, "UI must preserve the core optimality proof flag");
        clear->click();
        require(window.resultCount() == 0 && output->toPlainText().isEmpty() && add->isEnabled(),
                "Clear did not remove the corresponding immutable buffer");

        // Only the enabled wide-turn category may participate in this real solve.
        const auto wideInput = pattern_from_cube(cube::apply(CubeState{}, parse_moves("r")));
        window.smokeConfigure(wideInput, 1);
        window.findChild<QCheckBox *>("formulaCategory_1")->setChecked(true);
        for (char c : std::string(move_names))
            window.findChild<QCheckBox *>("formulaMove_" + QString(c))->setChecked(c == 'r');
        window.findChild<QComboBox *>("formulaHeuristic")->setCurrentIndex(2);
        start->click();
        waitIdle(window);
        require(window.resultCount() == 1, "Selected wide turn did not reach real formula search");
        add->click();
        const auto &wide = transferred.back();
        require(wide.moves == parse_moves("r'") && wide.turns == 1 && wide.rotations == 0,
                "Wide solution actions or algorithm counts changed in UI");
        require(apply_pattern(wide.start, wide.moves) == wide.terminal,
                "Buffered formula terminal does not replay");
        require(output->toPlainText().contains("r' (1f*)") && wide.optimalityProven,
                "The first proven solution depth receives the core proof marker");
        require(output->toPlainText().startsWith("不使用加速表。\n计算开始"),
                "No-heuristic preamble must match the original accelerator/start wording in Chinese");
        const auto priorOutput = output->toPlainText();
        window.findChild<QSpinBox *>("formulaMinimum")->setValue(1);
        start->click();
        waitIdle(window);
        require(window.resultCount() == 2 && output->toPlainText().startsWith(priorOutput),
                "Restart appends a new search without overwriting previous messages or formulas");
        require(output->toPlainText().contains("r' (1f)"),
                "Skipping an unproved shorter depth must not display a star");
        add->click();
        require(!transferred.back().optimalityProven,
                "Unproven solution retains false proof flag when transferred");
        require(!output->toPlainText().contains("旋转") && !output->toPlainText().contains("8角"),
                "Output must not display rotation counts or claim the original 8-corner table");
        require(window.grab().save("logs/formula-window-v5.png"), "Formula-window screenshot failed");
        applyAppTheme(app, true);
        QApplication::processEvents();
        require(window.grab().save("logs/formula-window-v5-night.png"),
                "Night formula-window screenshot failed");
        applyAppTheme(app, false);
        auto *settings = window.findChild<QToolButton *>("formulaSettingsToggle");
        settings->setChecked(true);
        QApplication::processEvents();
        require(window.width() == 550 && !advanced->isHidden(),
                "Expanding settings changed the original popup width");
        require(window.grab().save("logs/formula-window-v5-settings.png"),
                "Expanded settings screenshot failed");
        settings->setChecked(false);
        QApplication::processEvents();
        for (bool dark : {false, true})
            for (double scale : {.75, 1.0, 1.5, 2.0}) {
                applyAppTheme(app, dark, 1.0); // Main-window scaling must not override this popup's scale.
                window.resize(qRound(610 * scale), qRound(740 * scale));
                QApplication::processEvents();
                require(std::abs(window.uiScale() - scale) < .003,
                        "Popup must derive one consistent window scale");
                require(start->size() == QSize(qRound(240 * scale), qRound(50 * scale)) &&
                            clear->size() == start->size() && add->size() == start->size(),
                        "Polished action buttons must retain proportional actual sizes");
                require(output->font().pixelSize() == qRound(15 * scale),
                        "Popup text must scale once in logical pixels");
                require(output->font().families().contains("Consolas") &&
                            output->font().families().contains("Microsoft YaHei"),
                        "Popup output must use Consolas and Microsoft YaHei family");
                require(window.rect().contains(add->mapTo(&window, add->rect().bottomRight())),
                        "Scaled add action must remain fully accessible");
                require(window.grab().save(QString("logs/formula-window-scale-%1-%2.png")
                                               .arg(qRound(scale * 100))
                                               .arg(dark ? "night" : "day")),
                        "Scaled popup screenshot failed");
                settings->setChecked(true);
                QApplication::processEvents();
                require(window.width() == qRound(610 * scale) &&
                            window.rect().contains(advanced->mapTo(&window, advanced->rect().bottomRight())),
                        "Advanced settings must remain accessible without changing scaled popup width");
                settings->setChecked(false);
                QApplication::processEvents();
            }
        applyAppTheme(app, false);
        window.resize(610, 740);
        QApplication::processEvents();
        clear->click();

        // Exercise the UI's default unbounded request until a real later depth,
        // then cancel from the displayed depth event. No hidden finite budget.
        window.smokeConfigure(pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U F D L B"))), -1);
        window.findChild<QComboBox *>("formulaHeuristic")->setCurrentIndex(2);
        const auto cancelAtDepth = QObject::connect(output, &QPlainTextEdit::textChanged, &window, [&] {
            if (window.busy() && output->toPlainText().contains("正在搜索深度 3"))
                window.stopSearch();
        });
        start->click();
        waitIdle(window);
        QObject::disconnect(cancelAtDepth);
        require(window.request().max_turns == -1 && window.lastReport()->stop == PatternStop::Cancelled &&
                    window.lastReport()->completed_depth >= 2,
                "Unlimited UI search must actually enumerate increasing depths until cancellation");
        require(output->toPlainText().contains("正在搜索深度 2") &&
                    output->toPlainText().contains("计算停止"),
                "Unbounded search must preserve depth messages and append its actual stop status");
        clear->click();

        int finished = 0;
        window.onFinished = [&] { ++finished; };
        window.smokeConfigure(pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U F D L B"))), 20);
        start->click();
        window.stopSearch();
        require(window.startText() == "停止中…", "Cancel did not display pending cancellation");
        waitIdle(window);
        require(finished == 1 && window.startText() == "开始搜索",
                "Cancelled worker was not joined and reset");
        require(window.lastReport()->stop == PatternStop::Cancelled &&
                    output->toPlainText().contains("计算停止"),
                "Actual core cancellation must append the original-style stop message");
        window.smokeConfigure(pattern_from_cube(CubeState{}), 0);
        start->click();
        waitIdle(window);
        require(finished == 2 && window.resultCount() == 1, "Restart after cancellation failed");
        window.smokeConfigure(wideInput, 20);
        start->click();
        window.close();
        waitIdle(window);
        require(!window.isVisible() && window.resultCount() == 0,
                "Close did not wait for cancel and clear original text buffer");
        QElapsedTimer destruction;
        destruction.start();
        {
            FormulaWindow active(wideInput, false, {});
            active.startSearch();
            QThread::msleep(20); // Core now waits on a queued progress/result acknowledgement.
        }
        require(destruction.elapsed() < 1000,
                "Destructor must cancel acknowledgement wait without blocking the GUI join");
        {
            std::vector<ResultData> sortedOut;
            FormulaWindow fine(pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U R'"))), false,
                               [&](ResultData d) { sortedOut.push_back(std::move(d)); });
            // Ban F2 (symbol 2 = 'F', power 1 = '2') and verify request excludes only F2.
            fine.setMovePowerEnabled(2, 1, false);
            auto *fBox = fine.findChild<QCheckBox *>("formulaMove_F");
            require(fBox && fBox->text() == "F*", "Partially banned move must display * decoration");
            const auto req1 = fine.request();
            bool hasF = false, hasF2 = false, hasFp = false;
            for (auto m : req1.allowed_moves) {
                if (m.symbol == 'F' && m.power == 1) hasF = true;
                if (m.symbol == 'F' && m.power == 2) hasF2 = true;
                if (m.symbol == 'F' && m.power == 3) hasFp = true;
            }
            require(hasF && !hasF2 && hasFp, "Per-move power ban must exclude F2 while keeping F and F'");
            // Real search with F2 banned must NOT synthesize F F or F' F'!
            for (char c : std::string("URFDLB"))
                fine.findChild<QCheckBox *>("formulaMove_" + QString(c))->setChecked(c == 'F');
            fine.smokeConfigure(pattern_from_cube(cube::apply(CubeState{}, parse_moves("F2"))), 2);
            fine.startSearch();
            waitIdle(fine);
            require(fine.resultCount() == 0, "Banning F2 in FormulaWindow still emitted F F / F' F'");
            for (char c : std::string("URFDLB"))
                fine.findChild<QCheckBox *>("formulaMove_" + QString(c))->setChecked(true);
            fine.setMovePowerEnabled(2, 1, true);
            // Max rotations spinbox must be disabled when rotations are unchecked, enabled when checked.
            auto *rotCat = fine.findChild<QCheckBox *>("formulaCategory_2");
            auto *maxRot = fine.findChild<QSpinBox *>("formulaMaxRotations");
            require(maxRot && !maxRot->isEnabled(), "Max rotations must be disabled when rotations are off");
            require(fine.request().max_rotations == -1, "Max rotations must be -1 when rotations are off");
            rotCat->setChecked(true);
            require(maxRot->isEnabled(), "Max rotations must enable when rotations are checked");
            fine.setMaxRotations(1);
            require(fine.request().max_rotations == 1, "Max rotations must propagate to PatternRequest");
            rotCat->setChecked(false);
            // MCC scoring & lossless re-sort on actual results.
            fine.smokeConfigure(pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U R'"))), 4);
            fine.startSearch();
            waitIdle(fine);
            require(fine.resultCount() >= 1, "Fine window test search produced no results");
            fine.setMccSortMode(2); // MCC -> Moves
            fine.addResults();
            require(sortedOut.size() == static_cast<std::size_t>(fine.resultCount()),
                    "Add results count mismatch after MCC sort");
            for (std::size_t i = 0; i < sortedOut.size(); ++i) {
                require(sortedOut[i].mcc >= 0.0, "Formula result missing computed MCC score");
                if (i > 0)
                    require(sortedOut[i - 1].mcc <= sortedOut[i].mcc + 1e-9,
                            "MCC -> Moves sort did not order results by ascending MCC");
            }
            // Pre-rotation bracket formatting in FormulaWindow output:
            fine.clearResults();
            fine.setMccSortMode(0);
            fine.setModuloU(false);
            fine.setPreRotationMode(PatternPreRotation::YOnly);
            for (char c : std::string("URFDLB"))
                fine.findChild<QCheckBox *>("formulaMove_" + QString(c))->setChecked(c == 'R' || c == 'U' || c == 'L');
            fine.smokeConfigure(pattern_from_cube(cube::apply(CubeState{}, parse_moves("F' U F"))), 3);
            fine.startSearch();
            waitIdle(fine);
            auto *fineOut = fine.findChild<QPlainTextEdit *>("formulaOutput");
            require(fineOut && fineOut->toPlainText().contains("[y'] R' U' R") &&
                        fineOut->toPlainText().contains("[y] L' U' L"),
                    "FormulaWindow output did not bracket leading pre-rotations");
            // Double-clicking a formula line in FormulaWindow copies its formula text to clipboard:
            fine.show();
            QApplication::processEvents();
            const int pos = fineOut->toPlainText().indexOf("[y'] R' U' R");
            require(pos >= 0, "Formula text missing in output");
            QTextCursor tc = fineOut->textCursor();
            tc.setPosition(pos + 2);
            fineOut->setTextCursor(tc);
            const QRect cursorRect = fineOut->cursorRect(tc);
            const QPoint clickPt = cursorRect.center();
            QMouseEvent dblClick(QEvent::MouseButtonDblClick, QPointF(clickPt),
                                 fineOut->viewport()->mapToGlobal(clickPt), Qt::LeftButton, Qt::LeftButton,
                                 Qt::NoModifier);
            QApplication::sendEvent(fineOut->viewport(), &dblClick);
            require(QGuiApplication::clipboard()->text() == "[y'] R' U' R",
                    "Double-clicking formula line in FormulaWindow did not copy formula to clipboard");
            auto *toast = fine.findChild<QLabel *>("formulaCopyToast");
            require(toast && toast->isVisible() && toast->text().contains("已复制到剪贴板"),
                    "Double-clicking formula line did not show floating copy toast");
        }
        std::cout << "formula_window_tests: " << checks << " checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "formula_window_tests failed: " << error.what() << '\n';
        return 1;
    }
}
