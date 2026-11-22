#include "result_row.hpp"
#include "app_theme.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool ok, const char *message) {
    ++checks;
    if (!ok)
        throw std::runtime_error(message);
}
void click(QWidget &widget, QPointF point, Qt::MouseButton button, Qt::KeyboardModifiers modifiers = {},
           QEvent::Type type = QEvent::MouseButtonPress) {
    QMouseEvent event(type, point, widget.mapToGlobal(point), button, button, modifiers);
    QApplication::sendEvent(&widget, &event);
}
ResultData stored(const PatternState &input, std::string_view formula, bool proof) {
    ResultData d;
    d.target = d.start = input;
    d.moves = parse_moves(formula);
    d.turns = turn_count(d.moves);
    d.rotations = rotation_count(d.moves);
    d.formula = false;
    d.optimalityProven = proof;
    d.terminal = pattern_from_cube(cube::apply(completeCube(input), d.moves));
    return d;
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    if (QGuiApplication::platformName() == "offscreen") {
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto *name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "segoeuib.ttf", "consola.ttf"})
            QFontDatabase::addApplicationFont(fonts + name);
    }
    applyAppTheme(application, false);
    try {
        const auto original = pattern_from_cube(cube::apply(CubeState{}, parse_moves("R")));
        ResultRow row(original, true);
        row.setNumber(7);
        row.setPatternName("keep name");
        row.resize(640, 165);
        row.show();
        application.processEvents();
        int selected = 0, context = 0, cleared = 0, background = 0, loaded = 0;
        Qt::KeyboardModifiers selectionModifiers;
        row.onSelect = [&](ResultRow *, Qt::KeyboardModifiers modifiers) {
            ++selected;
            selectionModifiers = modifiers;
        };
        row.onContext = [&](ResultRow *, QPoint) { ++context; };
        row.onClearSelection = [&] { ++cleared; };
        row.onBackgroundContext = [&](QPoint) { ++background; };
        row.onLoad = [&](PatternState) { ++loaded; };
        const auto cubeCenter = row.thumbnail()->mapTo(&row, row.thumbnail()->rect().center());
        click(row, cubeCenter, Qt::LeftButton, Qt::ControlModifier | Qt::ShiftModifier);
        check(selected == 1 && selectionModifiers == (Qt::ControlModifier | Qt::ShiftModifier),
              "thumbnail keeps original modifier selection");
        click(row, cubeCenter, Qt::RightButton);
        check(context == 1 && selected == 1, "thumbnail right click opens menu without selecting");
        click(row, {300, 55}, Qt::LeftButton);
        check(cleared == 1 && selected == 1, "formula area clears selection");
        click(row, {300, 55}, Qt::RightButton);
        check(background == 1 && context == 1, "formula area uses bulk context menu");
        click(row, cubeCenter, Qt::LeftButton, {}, QEvent::MouseButtonDblClick);
        check(loaded == 0 && selected == 2, "double click does not load editor");
        check(row.hasFocus(), "thumbnail click takes focus from name edit");

        row.restoreResult(stored(original, "U U' R'", false));
        row.restoreResult(stored(original, "R'", true));
        row.setSelected(true);
        row.setOptimal(false);
        check(row.wantsOptimal(), "proved optimal result cannot be unchecked in original UI");
        row.invertInput();
        check(completeCube(row.input()).cubies == cube::inverse(completeCube(original).cubies),
              "inverse changes the clicked row input");
        check(row.selected() && row.wantsOptimal() && row.generated(),
              "inverse preserves selection, optimal checkbox, direction");
        check(row.patternName() == "keep name" && row.number() == 7, "inverse preserves row identity");
        auto best = row.result();
        check(best && best->optimalityProven && best->turns == 1 && format_moves(best->moves) == "R'",
              "inverse preserves proof and reverses generation formula");
        auto *optimal = row.findChild<QCheckBox *>();
        {
            QSignalBlocker blocked(optimal);
            optimal->setChecked(false);
        }
        const auto ordinary = row.result();
        check(ordinary && ordinary->turns == 3 && format_moves(ordinary->moves) == "U U' R'",
              "inverse independently preserves ordinary result");
        row.setOptimal(true);
        auto *formula = row.findChild<QLabel *>("formulaText");
        check(formula && formula->text().contains("(1f*)"),
              "original f metric and proven marker are visible");
        check(row.thumbnail()->geometry() == QRect(8, 10, 168, 122),
              "thumbnail remains proportional in the compact result row");
        check(row.findChild<QToolButton *>("resultDirection")->geometry() == QRect(186, 10, 50, 28),
              "direction title aligns with the name caption");
        check(formula->pos() == QPoint(240, 14), "formula follows the direction caption on the same line");
        check(row.findChild<QLineEdit *>()->geometry() == QRect(240, 64, 210, 30),
              "name edit follows the compact label column");
        check(optimal->geometry() == QRect(466, 64, 80, 30), "optimal control shares the compact name row");
        check(row.findChild<QToolButton *>("resultRun")->geometry() == QRect(552, 62, 30, 30),
              "run arrow follows the optimal control");
        auto *copyBtn = row.findChild<QToolButton *>("resultCopy");
        check(copyBtn && copyBtn->isVisible(), "copy button must be visible when result row has a formula");
        copyBtn->click();
        check(QGuiApplication::clipboard()->text() == "R'",
              "clicking copy button must copy current formula text to clipboard");
        row.resize(1000, row.height());
        application.processEvents();
        check(row.findChild<QLineEdit *>()->pos() == QPoint(240, 64) && formula->x() == 240 &&
                  row.findChild<QToolButton *>("resultDirection")->x() == 186,
              "wider result panel does not move source anchors");
        row.replaceInput(original);
        application.processEvents();
        check(!row.result(true) && !row.selected() && !row.wantsOptimal() && !row.busy() && !copyBtn->isVisible(),
              "multiplication replacement clears searches, results and controls");
        check(row.patternName() == "keep name" && row.number() == 7 && row.generated(),
              "replacement preserves identity and direction");

        row.setViewMode(1);
        application.processEvents();
        check(row.thumbnail()->view() == CubeCanvas::View::Net2D,
              "result row thumbnail must support 2D net view");
        click(row, cubeCenter, Qt::LeftButton);
        check(selected == 3, "2D net thumbnail keeps click selection");
        row.setViewMode(2);
        application.processEvents();
        check(row.thumbnail()->view() == CubeCanvas::View::Cube3D,
              "result row thumbnail must support 3D cube view");
        click(row, cubeCenter, Qt::LeftButton);
        check(selected == 4, "3D cube thumbnail keeps click selection");
        row.setViewMode(0);
        check(row.thumbnail()->view() == CubeCanvas::View::Classic,
              "result row thumbnail returns cleanly to Classic view");

        ResultRow formulaRow(original, false, true);
        ResultData formulaData;
        formulaData.formula = true;
        formulaData.moves = parse_moves("x R");
        formulaData.turns = 1;
        formulaData.rotations = 1;
        formulaRow.setFormulaResult(formulaData);
        const auto formulaText = formulaRow.findChild<QLabel *>("formulaText")->text();
        auto *formulaCopyBtn = formulaRow.findChild<QToolButton *>("resultCopy");
        check(!formulaCopyBtn || !formulaCopyBtn->isVisible(),
              "Method 2 formula rows in main window should not display the Method 1 copy button");
        check(formulaText.contains("(1f)") && !formulaText.contains("旋转") &&
                  formulaRow.result()->rotations == 1 && formulaRow.result()->turns == 1,
              "rotation display is hidden without changing internal rotation and turn counts");

        ResultRow longRow(original, false);
        longRow.resize(770, 165);
        longRow.setUiScale(1.0);
        std::string longSolution;
        for (int i = 0; i < 60; ++i)
            longSolution += "R R' ";
        longSolution += "R'";
        longRow.restoreResult(stored(original, longSolution, false));
        longRow.show();
        application.processEvents();
        auto *longFormula = longRow.findChild<QLabel *>("formulaText");
        auto *longName = longRow.findChild<QLineEdit *>();
        check(longRow.height() > 140 && longName->y() > 64 &&
                  longFormula->geometry().bottom() < longName->y(),
              "long formula expands row and moves lower controls without overlap");
        longRow.restoreResult(stored(original, "R'", false));
        check(longRow.height() == 140 && longName->y() == 64,
              "shorter formula restores original default anchors");

        for (bool dark : {false, true})
            for (double scale : {.75, 1.0, 1.5, 2.0}) {
                applyAppTheme(application, dark, scale);
                longRow.setUiScale(scale);
                longRow.setAppearance(dark, longRow.thumbnail()->colors());
                longRow.resize(qRound(770 * scale), longRow.height());
                longRow.setPatternName("测试名称 · Cube example");
                longRow.setNumber(1);
                application.processEvents();
                const auto px = [scale](int value) { return qRound(value * scale); };
                check(longRow.thumbnail()->geometry() == QRect(px(8), px(10), px(168), px(122)),
                      "Thumbnail geometry scales once in logical pixels");
                check(longName->geometry() == QRect(px(240), px(64), px(210), px(30)) &&
                          longRow.height() == px(140),
                      "Name geometry and row height scale proportionally");
                check(longName->font().pixelSize() == px(15) && longFormula->font().pixelSize() == px(15),
                      "Font scales once with controls");
                QLabel *nameLabel = nullptr;
                for (auto *label : longRow.findChildren<QLabel *>())
                    if (label->text() == "名称：")
                        nameLabel = label;
                check(nameLabel && nameLabel->alignment().testFlag(Qt::AlignRight) &&
                          longName->x() - nameLabel->geometry().right() <= px(6),
                      "Name caption is adjacent to the input at every scale");
                if (argc > 1)
                    check(longRow.grab().save(QString::fromLocal8Bit(argv[1]) + "/result-row-scale-" +
                                              QString::number(qRound(scale * 100)) +
                                              (dark ? "-night.png" : "-day.png")),
                          "Scaled result row screenshot save");
            }
        applyAppTheme(application, false);

        // Test all frames: the inverse formula must still solve physical input.
        for (int frame = 0; frame < 24; ++frame) {
            CubeState solved;
            solved.frame = static_cast<std::uint8_t>(frame);
            const auto scramble = parse_moves("R U F2 L' D B");
            const auto p = pattern_from_cube(cube::apply(solved, scramble));
            ResultRow framed(p, false);
            framed.restoreResult(stored(p, format_moves(cube::inverse(scramble)), false));
            framed.invertInput();
            const auto data = framed.result();
            check(data && is_solved(cube::apply(completeCube(framed.input()), data->moves)),
                  "inverse formula verified across all 24 physical frames");
        }

        const auto hard = pattern_from_cube(
            cube::apply(CubeState{}, parse_moves("R U F2 D' L B2 U' R2 F D2 B' L2 U2 F' R D B U L' F2")));
        row.replaceInput(hard);
        row.setOptimal(true);
        OptimalOptions limits;
        limits.time_limit = std::chrono::seconds(20);
        row.start(TwoPhaseOptions{}, limits);
        QThread::msleep(30);
        check(row.busy(), "search started before replacing input");
        row.replaceInput(original);
        application.processEvents();
        check(!row.busy() && !row.result(true) &&
                  format_pattern_facelets(row.input()) == format_pattern_facelets(original),
              "cancel join and generation guard reject queued results for old input");
        std::cout << checks << " result row interaction checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
