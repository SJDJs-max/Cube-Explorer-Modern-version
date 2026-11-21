#pragma once
#include "main_window.hpp"

inline void runUiSmoke(MainWindow &window, QApplication &app, const QString &screenshot, const QString &log) {
    auto checks = std::make_shared<int>(0);
    auto stage = std::make_shared<int>(0);
    auto attempts = std::make_shared<int>(0);
    auto fail = [&app, log](QString message) {
        if (!log.isEmpty()) {
            QFile file(log);
            if (file.open(QIODevice::WriteOnly))
                file.write(("FAIL: " + message + "\n").toUtf8());
        }
        app.exit(2);
    };
    auto check = [checks](bool condition, const char *message) {
        ++*checks;
        if (!condition)
            throw std::runtime_error(message);
    };
    auto shot = [&window, screenshot](const QString &suffix, QWidget *target = nullptr) {
        if (screenshot.isEmpty())
            return;
        QEventLoop settle;
        QTimer::singleShot(35, &settle, &QEventLoop::quit);
        settle.exec();
        const auto file = QFileInfo(screenshot);
        QDir().mkpath(file.absolutePath());
        if (!(target ? target : &window)
                 ->grab()
                 .save(file.absolutePath() + "/" + file.completeBaseName() + suffix + ".png"))
            throw std::runtime_error("Screenshot write failed");
    };
    auto click = [](QWidget *widget, Qt::MouseButton button,
                    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPointF local = widget->rect().center();
        const QPointF global = widget->mapToGlobal(local.toPoint());
        QMouseEvent down(QEvent::MouseButtonPress, local, global, button, button, modifiers);
        QApplication::sendEvent(widget, &down);
        QMouseEvent up(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, modifiers);
        QApplication::sendEvent(widget, &up);
    };
    try {
        check(!window.leftVisible(), "Left pane must start hidden");
        check(window.findChildren<QTabWidget *>().empty(), "Sticker editor must have no editor tabs");
        check(window.findChildren<QProgressBar *>().empty(),
              "Main window must have no decorative progress bar");
        check(window.findChild<QAction *>("lsllBatchAction") == nullptr, "LSLL action must be removed");
        check(window.findChild<QAction *>("xcrossMenuAction") != nullptr, "Cross quick solver action missing");
        const std::array<QColor, 7> defaults = {QColor(255, 255, 255), QColor(238, 0, 0),   QColor(0, 216, 0),
                                                QColor(254, 254, 0),   QColor(255, 161, 0), QColor(0, 0, 242),
                                                QColor(128, 128, 128)};
        check(window.canvas()->colors() == defaults, "User RGB palette defaults differ");
        check(!window.windowIcon().pixmap(32).isNull(), "Original cube icon must be embedded and usable");
        auto *theme = window.findChild<QToolButton *>("themeToggle");
        QAction *night = nullptr, *color = nullptr;
        for (auto *action : window.findChildren<QAction *>()) {
            if (action->text() == "夜晚模式")
                night = action;
            if (action->text() == "自定义选中颜色…")
                color = action;
        }
        check(theme && night && color, "Theme shortcut, menu or color option missing");
        theme->click();
        check(window.dark() && night->isChecked(), "Theme shortcut must update menu state");
        night->trigger();
        check(!window.dark() && !night->isChecked(), "Menu theme must update shortcut state");
        {
            ModeChoiceDialog choice(1, &window);
            choice.show();
            QCoreApplication::processEvents();
            const auto titles = choice.findChildren<QLabel *>("methodTitle");
            const auto descriptions = choice.findChildren<QLabel *>("methodDescription");
            check(titles.size() == 2 && descriptions.size() == 2,
                  "Mode cards need distinct titles and descriptions");
            check(titles[0]->font().bold() &&
                      titles[0]->font().pixelSize() > descriptions[0]->font().pixelSize(),
                  "Mode title must be bold and larger than explanatory text");
            check(titles[0]->height() >= titles[0]->fontMetrics().height() &&
                      descriptions[0]->height() >= descriptions[0]->fontMetrics().height() * 2,
                  "Mode card titles and two-line explanation must be fully visible");
            check(descriptions[0]->parentWidget()->rect().contains(descriptions[0]->geometry()),
                  "Mode explanation must remain inside the card");
            shot("-method-choice", &choice);
            choice.findChild<QPushButton *>("methodChoice2")->click();
            check(choice.result() == 2, "Method card click did not select formula mode");
        }
        bool translatedColor = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QColorDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return;
            QStringList labels;
            for (auto *label : dialog->findChildren<QLabel *>())
                labels << label->text();
            translatedColor =
                labels.contains("红色：") && labels.contains("绿色：") && labels.contains("蓝色：");
            shot("-color-picker", dialog);
            dialog->reject();
        });
        color->trigger();
        check(translatedColor, "Actual Options color dialog RGB labels must be Chinese");
        auto settle = [] {
            QEventLoop loop;
            QTimer::singleShot(80, &loop, &QEventLoop::quit);
            loop.exec();
        };
        for (const auto size : {QSize(390, 550), QSize(765, 1095), QSize(510, 730)}) {
            window.resize(size);
            settle();
            auto *input = window.findChild<QLineEdit *>("formulaInput");
            check(window.rect().contains(QRect(input->mapTo(&window, QPoint()), input->size())),
                  "Formula input clipped during proportional resizing");
            auto *group = window.findChild<QGroupBox *>("formulaInputGroup");
            check(window.height() - group->mapTo(&window, group->rect().bottomRight()).y() <= 18,
                  "Unused bottom strip below formula input must be removed");
            const auto polygon = window.canvas()->stickerPolygon(22).boundingRect();
            check(std::abs(polygon.width() - polygon.height()) < 1, "Sticker square stretched during resize");
            shot(QString("-scale-%1").arg(size.width()));
        }
        shot("-initial-day");
        window.setDark(true);
        shot("-initial-night");
        check(window.dark(), "Theme did not switch");
        window.setDark(false);
        const auto solved = pattern_from_cube(CubeState{});
        QAction *centerSwitch = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "使用中心方向约束")
                centerSwitch = action;
        check(centerSwitch != nullptr, "Original center-orientation option missing");
        const auto centerCube = cube::apply(CubeState{}, parse_moves("R U"));
        window.restore(pattern_from_cube(centerCube, true));
        QMetaObject::invokeMethod(centerSwitch->parent(), "aboutToShow", Qt::DirectConnection);
        check(centerSwitch->isChecked(), "Center menu state must follow actual editor constraints");
        centerSwitch->trigger();
        check(std::all_of(window.centerOrientations().begin(), window.centerOrientations().end(),
                          [](int n) { return n == -1; }),
              "Turning centers off must disable constraints");
        window.applyFormula("F");
        centerSwitch->trigger();
        check(
            window.centerOrientations() ==
                pattern_from_cube(cube::apply(centerCube, parse_moves("F")), true).center_ori,
            "Re-enabling center constraints must retain their real directions, including intervening turns");
        const auto remembered = window.centerOrientations();
        centerSwitch->trigger();
        const auto centerPath = QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v4-centers.cex";
        window.savePath(centerPath);
        window.restore(solved);
        window.loadPath(centerPath);
        centerSwitch->trigger();
        check(window.centerOrientations() == remembered,
              "Workspace must preserve disabled center directions");
        {
            QFile file(centerPath);
            check(file.open(QIODevice::ReadOnly), "Center workspace fixture open failed");
            auto data = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            data.remove("remembered_centers");
            data["editor"] = qstr(format_facelets(solved_facelets()));
            check(file.open(QIODevice::WriteOnly), "Legacy center fixture write failed");
            file.write(QJsonDocument(data).toJson());
            file.close();
            window.loadPath(centerPath);
            QMetaObject::invokeMethod(centerSwitch->parent(), "aboutToShow", Qt::DirectConnection);
            centerSwitch->trigger();
            check(std::all_of(window.centerOrientations().begin(), window.centerOrientations().end(),
                              [](int n) { return n == 0; }),
                  "Old workspace must not inherit another cube's hidden center directions");
        }
        window.restore(solved);
        window.setAutoFix(false);
        window.editSticker(13, Qt::LeftButton, Qt::NoModifier);
        check(window.brush() == 'R', "Center left click must pick color");
        window.editSticker(0, Qt::LeftButton, Qt::ControlModifier);
        check(window.rawFacelets()[0] == '@', "Ctrl paint must use orientation marker");
        window.editSticker(1, Qt::LeftButton, Qt::ShiftModifier | Qt::ControlModifier);
        check(window.rawFacelets()[1] == 'r', "Shift must take priority over Ctrl");
        const auto before = window.rawFacelets();
        window.editSticker(9, Qt::RightButton, Qt::NoModifier);
        int delta = 0;
        for (int i = 0; i < 54; ++i)
            delta += before[i] != window.rawFacelets()[i];
        check(delta == 1 && window.rawFacelets()[9] == '?', "Right click must clear one sticker only");
        window.editSticker(10, Qt::MiddleButton, Qt::NoModifier);
        check(window.rawFacelets()[10] == '?', "Middle click must clear one sticker");
        window.restore(solved);
        window.editSticker(4, Qt::LeftButton, Qt::NoModifier);
        window.setAutoFix(true);
        window.editSticker(0, Qt::LeftButton, Qt::ShiftModifier);
        const auto marked = window.pattern();
        check(std::any_of(marked.co.begin(), marked.co.end(), [](int x) { return x < 0; }),
              "Shift AutoFix did not propagate cubie mark");
        window.restore(solved);
        std::vector<MoveButton *> buttons;
        for (auto *b : window.findChildren<QPushButton *>())
            if (auto *move = dynamic_cast<MoveButton *>(b))
                buttons.push_back(move);
        check(buttons.size() == 18, "18 original-layout move buttons missing");
        for (auto *b : buttons) {
            const auto symbol = b->property("symbol").toString().at(0).toLatin1();
            window.restore(solved);
            click(b, Qt::LeftButton, Qt::ShiftModifier);
            const auto once = window.rawFacelets();
            std::string expected = format_facelets(solved_facelets());
            std::array<std::int8_t, 6> centers;
            centers.fill(-1);
            const std::array<Move, 1> move = {Move{symbol, 1}};
            editor::apply_moves_raw(expected, centers, move);
            check(once == expected, "Shift must not change turn-button power");
            click(b, Qt::RightButton);
            check(window.rawFacelets() == format_facelets(solved_facelets()),
                  "Right button must apply inverse");
        }
        check(window.findChildren<QPushButton *>("resetButton").size() == 9, "Nine reset buttons missing");
        window.resetEditor(editor::Reset::Empty, editor::Pieces::Corners);
        const auto emptyCorners = window.pattern();
        check(std::all_of(emptyCorners.cp.begin(), emptyCorners.cp.end(), [](int n) { return n < 0; }),
              "Corner empty reset failed");
        window.resetEditor(editor::Reset::Clean);
        window.setView(1);
        shot("-2d-day");
        window.setView(2);
        shot("-3d-day");
        window.setView(0);
        // Language toggle (Chinese <-> English)
        auto *langBtn = window.findChild<QToolButton *>("langToggle");
        check(langBtn != nullptr, "Language toggle button missing");
        langBtn->click();
        check(window.english() && window.findChild<QPushButton *>("solveCurrent")->text() == "Solve Cube",
              "English UI toggle did not update labels");
        shot("-initial-en-day");
        langBtn->click();
        check(!window.english() && window.findChild<QPushButton *>("solveCurrent")->text() == "求解当前情况",
              "Chinese UI toggle did not restore labels");
        // Formula input right-click context menu must be fully Chinese in Chinese mode
        {
            auto *input = window.findChild<QLineEdit *>("formulaInput");
            std::unique_ptr<QMenu> ctx(input->createStandardContextMenu());
            QStringList items;
            for (auto *a : ctx->actions())
                if (!a->isSeparator())
                    items << a->text();
            const auto joined = items.join("\n");
            check(joined.contains("撤销") && joined.contains("复制") && joined.contains("粘贴") &&
                      joined.contains("全选") && !joined.contains("Paste") && !joined.contains("Select All"),
                  "Formula input context menu was not fully localized to Chinese");
        }
        // Inverse formula button check
        window.applyFormula("R U F'");
        window.applyInverseFormula("R U F'");
        check(window.rawFacelets() == format_facelets(solved_facelets()),
              "Inverse formula button did not invert applied moves");
        // ZBLS preset check: LL gray '?', 4 U-edges '@' (Ctrl orientation), F2L solved
        auto *zblsBtn = window.findChild<QToolButton *>("presetZbls");
        check(zblsBtn != nullptr, "ZBLS preset chip missing");
        zblsBtn->click();
        const auto &zblsRaw = window.rawFacelets();
        check(zblsRaw[1] == '@' && zblsRaw[3] == '@' && zblsRaw[5] == '@' && zblsRaw[7] == '@' &&
                  zblsRaw[0] == '?' && zblsRaw[2] == '?' && zblsRaw[6] == '?' && zblsRaw[8] == '?' &&
                  zblsRaw[19] == '?' && zblsRaw[22] == 'F' && zblsRaw[25] == 'F' && zblsRaw[31] == 'D',
              "ZBLS preset stickers did not match specification");
        check(static_cast<bool>(validate_pattern(window.pattern())), "ZBLS preset must produce a valid pattern");
        shot("-zbls-day");
        // OLLCP / COLL preset check: F2L + U corners solved, 4 U edges '@' orientation only
        auto *ollcpBtn = window.findChild<QToolButton *>("presetOllcp");
        check(ollcpBtn != nullptr, "OLLCP/COLL preset chip missing");
        ollcpBtn->click();
        const auto &ollcpRaw = window.rawFacelets();
        check(ollcpRaw[0] == 'U' && ollcpRaw[2] == 'U' && ollcpRaw[6] == 'U' && ollcpRaw[8] == 'U' &&
                  ollcpRaw[18] == 'F' && ollcpRaw[20] == 'F' &&
                  ollcpRaw[1] == '@' && ollcpRaw[3] == '@' && ollcpRaw[5] == '@' && ollcpRaw[7] == '@' &&
                  ollcpRaw[10] == '?' && ollcpRaw[19] == '?' && ollcpRaw[37] == '?' && ollcpRaw[46] == '?',
              "OLLCP/COLL preset stickers did not match specification");
        check(static_cast<bool>(validate_pattern(window.pattern())),
              "OLLCP/COLL preset must produce a valid pattern");
        shot("-ollcp-day");
        // Cross / XC / XXC / XXXC quick solver builder & dialog verification across bottom colors
        {
            const auto scr = cube::apply(CubeState{}, parse_moves("R U2 F' L D' B2"));
            const auto whiteXC = buildXCrossPattern(scr, 0, 1); // White bottom + FR slot
            check(whiteXC.rotationText == "x2" && static_cast<bool>(validate_pattern(whiteXC.pattern)),
                  "White-bottom XCross pattern invalid");
            const auto greenXXXC = buildXCrossPattern(scr, 2, 1 | 2 | 4); // Green bottom + 3 slots (XXXC)
            check(greenXXXC.rotationText == "x'" && static_cast<bool>(validate_pattern(greenXXXC.pattern)),
                  "Green-bottom XXXCross pattern invalid");
            XCrossDialog xcDialog(scr, window.canvas()->colors(), 1.0, &window);
            xcDialog.show();
            QCoreApplication::processEvents();
            shot("-xcross-dialog", &xcDialog);
            xcDialog.close();
        }
        window.resetEditor(editor::Reset::Clean);
        const auto path = QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v2-roundtrip.cex";
        window.restore(pattern_from_cube(CubeState{}, true));
        window.editSticker(0, Qt::RightButton, Qt::NoModifier);
        const auto editor = window.rawFacelets();
        window.savePath(path);
        window.restore(solved);
        window.loadPath(path);
        check(window.rawFacelets() == editor, "Workspace discarded incomplete editor state");
        check(std::all_of(window.centerOrientations().begin(), window.centerOrientations().end(),
                          [](int x) { return x == 0; }),
              "Workspace discarded center constraints");
        {
            QFile file(path);
            check(file.open(QIODevice::ReadOnly), "Test fixture open failed");
            auto object = QJsonDocument::fromJson(file.readAll()).object();
            auto text = object["editor"].toString();
            text[4] = '?';
            object["editor"] = text;
            file.close();
            check(file.open(QIODevice::WriteOnly), "Test fixture open failed");
            file.write(QJsonDocument(object).toJson());
            file.close();
            bool rejected = false;
            try {
                window.loadPath(path);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected, "Invalid center color must not reach painting");
        }
        window.restore(solved);
        window.applyFormula("Rs R-1 L");
        check(window.pattern() == solved, "Legacy Rs / -1 input was not applied");
        const auto emptyWorkspace = QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v3-empty.cex";
        window.savePath(emptyWorkspace);
        window.collapseResults();
        check(window.rows().empty(), "Formula workflow fixture must have no main-window results");
        auto *panel = window.openFormula(solved, false);
        check(!window.leftVisible(), "Opening formula window must leave main results collapsed");
        check(panel->isWindow() && panel->isVisible(), "Formula search must open a separate visible window");
        check(!panel->busy() && panel->startText() == "开始搜索", "Opening formula mode started search");
        check(panel->resultCount() == 0, "New formula window must start with an empty buffer");
        shot("-formula-day", panel);
        window.setDark(true);
        shot("-formula-night", panel);
        window.setDark(false);
        panel->smokeConfigure(solved, 0);
        panel->startSearch();
        check(panel->busy() && panel->startText() == "停止搜索",
              "Starting formula search did not change real worker state");
        check(!window.leftVisible() && window.rows().empty(),
              "Starting formula search must not change main results");
        auto *timer = new QTimer(&window);
        timer->setInterval(25);
        QObject::connect(
            timer, &QTimer::timeout, &window,
            [&window, &app, log, check, fail, shot, timer, stage, attempts, checks, panel, emptyWorkspace] {
                try {
                    if (++*attempts > 2200)
                        throw std::runtime_error("UI smoke timeout");
                    if (*stage == 0 && !panel->busy()) {
                        check(panel->resultCount() == 1, "Formula zero-goal result missing");
                        check(
                            !window.leftVisible() && window.rows().empty(),
                            "Actual formula results must stay in popup without expanding or adding to main");
                        auto *add = panel->findChild<QPushButton *>("formulaAdd");
                        auto *clear = panel->findChild<QPushButton *>("formulaClear");
                        auto *start = panel->findChild<QPushButton *>("formulaStart");
                        check(add && clear && start, "Original formula action buttons missing");
                        for (auto *button : {start, clear, add}) {
                            const QRect area(button->mapTo(panel, QPoint(0, 0)), button->size());
                            check(panel->rect().contains(area),
                                  "Formula action button clipped outside popup");
                        }
                        add->click();
                        QCoreApplication::processEvents();
                        if (window.screen()->availableGeometry().contains(window.frameGeometry()))
                            check(
                                (window.frameGeometry().center() -
                                 window.screen()->availableGeometry().center())
                                        .manhattanLength() < 5,
                                "Expanding the main results should center the complete window on its screen");
                        check(window.leftVisible() && window.rows().size() == 1,
                              "First Add must expand main results and copy buffered formula");
                        auto first = window.rows().front()->result();
                        check(first && first->formula && first->moves.empty() && first->turns == 0,
                              "Add changed verified zero-step formula content or count");
                        check(panel->resultCount() == 1, "Add must retain original text and result buffer");
                        add->click();
                        check(window.rows().size() == 2, "Repeated Add must copy all formulas again");
                        clear->click();
                        check(panel->resultCount() == 0 && window.rows().size() == 2,
                              "Clear must clear only popup buffer, preserving added main rows");
                        window.collapseResults();
                        panel->smokeConfigure(
                            pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U F L B"))), 20);
                        panel->startSearch();
                        check(!clear->isEnabled() && add->isEnabled(),
                              "During search original Clear must be disabled and Add remain available");
                        panel->stopSearch();
                        check(panel->startText() == "停止中…",
                              "Stop request must reflect pending cancellation");
                        ++*stage;
                    } else if (*stage == 1 && !panel->busy()) {
                        check(panel->startText() == "开始搜索", "Stop did not restore button");
                        check(!window.leftVisible() && window.rows().size() == 2,
                              "Cancellation must not expand or alter main results");
                        panel->clearResults();
                        panel->smokeConfigure(pattern_from_cube(CubeState{}), 0);
                        panel->startSearch();
                        ++*stage;
                    } else if (*stage == 2 && !panel->busy()) {
                        check(panel->resultCount() == 1, "Formula restart failed");
                        check(!window.leftVisible() && window.rows().size() == 2,
                              "Restarted search must retain results exclusively in popup");
                        shot("-formula-results-day", panel);
                        panel->hide();
                        window.loadPath(emptyWorkspace);
                        window.collapseResults();
                        check(window.rows().empty(),
                              "Empty workspace did not reset result screenshot fixture");
                        auto *row = window.addOrdinary(
                            pattern_from_cube(cube::apply(CubeState{}, parse_moves("R U F'"))), false);
                        row->setPatternName("普通求解");
                        TwoPhaseOptions options;
                        row->start(options, {});
                        ++*stage;
                    } else if (*stage == 3 && !window.rows().back()->busy()) {
                        auto *row = window.rows().back();
                        auto data = row->result();
                        check(data.has_value(), "Ordinary solve missing");
                        check(is_solved(completeCube(data->terminal)), "Ordinary solve not exact");
                        check(!data->optimalityProven, "Two-phase result must not claim global optimality");
                        row->setOptimal(true);
                        OptimalOptions options;
                        options.time_limit = std::chrono::seconds(5);
                        row->start({}, options);
                        ++*stage;
                    } else if (*stage == 4 && !window.rows().back()->busy()) {
                        auto data = window.rows().back()->result();
                        check(data && data->optimalityProven && data->turns == 3,
                              "Real optimal checkbox not wired");
                        check(window.rows().size() == 1,
                              "Ordinary screenshot must show one compact original row");
                        check(window.rows().back()->height() < 220, "Original result row must be compact");
                        QAction *statistics = nullptr;
                        for (auto *action : window.findChildren<QAction *>())
                            if (action->text() == "搜索统计")
                                statistics = action;
                        check(statistics != nullptr, "Statistics menu missing");
                        bool histogram = false;
                        QTimer::singleShot(0, &window, [&] {
                            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                            if (!dialog)
                                return;
                            auto *text = dialog->findChild<QPlainTextEdit *>("searchStatistics");
                            histogram = text && text->toPlainText().contains("已最优求解的魔方：1") &&
                                        text->toPlainText().contains("3f*：1");
                            shot("-statistics", dialog);
                            dialog->reject();
                        });
                        statistics->trigger();
                        check(histogram, "Statistics must group actual algorithm results by step count and "
                                         "proven optimum");
                        window.restore(window.rows().back()->input());
                        window.resize(1280, 840);
                        shot("-ordinary-day");
                        window.setDark(true);
                        shot("-ordinary-night");
                        window.setDark(false);
                        const auto path = QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v3-results.cex";
                        window.savePath(path);
                        window.loadPath(path);
                        check(window.rows().size() == 1 && window.rows().front()->result().has_value(),
                              "Ordinary workspace roundtrip failed");
                        check(!window.rows().front()->result()->optimalityProven,
                              "Loaded file must not grant unproven optimality");
                        const auto ordinaryText =
                            QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v3-ordinary.txt";
                        window.savePath(ordinaryText, true);
                        window.loadPath(ordinaryText);
                        check(window.rows().size() == 1 && window.rows().front()->generated(),
                              "Legacy text must load in original Generator direction");
                        auto generated = window.rows().front()->result();
                        check(generated &&
                                  apply_pattern(generated->start, generated->moves) == generated->terminal,
                              "Legacy generator formula failed real replay");
                        panel->findChild<QPushButton *>("formulaAdd")->click();
                        check(window.rows().size() == 2 && window.rows().back()->isFormula(),
                              "Formula Add must share the ordinary main results list");
                        const auto count = window.rows().size();
                        window.savePath(path);
                        window.loadPath(path);
                        check(window.rows().size() == count && window.rows().back()->isFormula(),
                              "Mixed workspace roundtrip failed");
                        for (const auto *row : window.rows()) {
                            const auto value = row->result();
                            check(value && turn_count(value->moves) == value->turns &&
                                      rotation_count(value->moves) == value->rotations &&
                                      apply_pattern(value->start, value->moves) == value->terminal,
                                  "Workspace changed raw actions, counters or terminal state");
                        }
                        {
                            QFile file(path);
                            check(file.open(QIODevice::ReadOnly), "Result fixture open failed");
                            auto object = QJsonDocument::fromJson(file.readAll()).object();
                            auto records = object["records"].toArray();
                            auto record = records[0].toObject();
                            // Keep a physically self-consistent action/terminal pair, but lie about its
                            // target.
                            record["pattern"] = records[1].toObject()["pattern"];
                            records[0] = record;
                            object["records"] = records;
                            file.close();
                            check(file.open(QIODevice::WriteOnly), "Forged result fixture open failed");
                            file.write(QJsonDocument(object).toJson());
                            file.close();
                            bool invalidTargetRejected = false;
                            try {
                                window.loadPath(path);
                            } catch (const std::exception &) {
                                invalidTargetRejected = true;
                            }
                            check(invalidTargetRejected && window.rows().size() == count,
                                  "Invalid formula goal must be rejected before replacing existing results");
                        }
                        const auto legacy = QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v2-preserve.txt";
                        {
                            QFile file(legacy);
                            check(file.open(QIODevice::WriteOnly), "Test fixture open failed");
                            file.write("preserve-original");
                        }
                        panel->show();
                        panel->close();
                        check(!panel->isVisible() && panel->resultCount() == 0 &&
                                  window.rows().size() == count,
                              "Closing idle popup must clear its buffer without deleting transferred main "
                              "results");
                        bool rejected = false;
                        try {
                            window.savePath(legacy, true);
                        } catch (const std::exception &) {
                            rejected = true;
                        }
                        check(rejected, "Mixed formulas must not be flattened into legacy TXT");
                        {
                            QFile file(legacy);
                            check(file.open(QIODevice::ReadOnly), "Test fixture open failed");
                            check(file.readAll() == "preserve-original",
                                  "Rejected export truncated original file");
                        }
                        // Retain a real result while checking original Escape load cancellation.
                        window.loadPath(emptyWorkspace);
                        auto *row = window.addOrdinary(pattern_from_cube(CubeState{}), false);
                        row->start({}, {});
                        ++*stage;
                    } else if (*stage == 5 && !window.rows().empty() && !window.rows().back()->busy()) {
                        // Original Escape cancels loading without terminating unrelated searches.
                        const auto cancelPath =
                            QString::fromUtf8(CUBE_PROJECT_ROOT) + "/temp/ui-v3-load-cancel.txt";
                        {
                            QFile file(cancelPath);
                            check(file.open(QIODevice::WriteOnly), "Load cancellation fixture open failed");
                            for (int i = 0; i < 130; ++i)
                                file.write("R //cancel-test\n");
                        }
                        QTimer::singleShot(0, &window, [&window] {
                            QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                            QApplication::sendEvent(&window, &key);
                        });
                        bool cancelled = false;
                        try {
                            window.loadPath(cancelPath);
                        } catch (const std::exception &e) {
                            cancelled = QString::fromUtf8(e.what()).contains("取消");
                        }
                        check(cancelled && window.rows().size() == 1,
                              "Escape load cancellation must retain existing main rows");
                        QAction *scramble = nullptr;
                        for (auto *action : window.findChildren<QAction *>())
                            if (action->text() == "生成 WCA 打乱（原版）")
                                scramble = action;
                        check(scramble != nullptr, "Original five-scramble menu action missing");
                        scramble->trigger();
                        ++*stage;
                    } else if (*stage == 6 && window.rows().size() == 5 &&
                               std::all_of(window.rows().begin(), window.rows().end(),
                                           [](auto *r) { return !r->busy() && r->result().has_value(); })) {
                        for (auto *row : window.rows()) {
                            const auto data = row->result();
                            check(data->generate && !data->formula && data->turns <= 21 &&
                                      apply_pattern(data->start, data->moves) == data->terminal,
                                  "Original five-scramble action must contain actual verified generators");
                        }
                        // Verify 4 new UX upgrades: synced help language, synced result-row views,
                        // spaceless/R'2 formula parsing, and customizable shortcuts.
                        check(window.helpFilePath().endsWith("/help/index.html"),
                              "Chinese UI must resolve help/index.html");
                        window.setEnglish(true);
                        check(window.helpFilePath().endsWith("/help/index.en.html"),
                              "English UI must resolve help/index.en.html");
                        window.setEnglish(false);
                        window.setView(1);
                        check(!window.rows().empty() &&
                                  window.rows().front()->thumbnail()->view() == CubeCanvas::View::Net2D,
                              "Left result rows must follow 2D Net main view");
                        window.setView(2);
                        check(window.rows().front()->thumbnail()->view() == CubeCanvas::View::Cube3D,
                              "Left result rows must follow 3D Cube main view");
                        auto *addedIn3d = window.addOrdinary(pattern_from_cube(CubeState{}), false);
                        check(addedIn3d->thumbnail()->view() == CubeCanvas::View::Cube3D,
                              "Newly added result rows must inherit active view mode");
                        {
                            // Dragging the main 3D cube must rotate left-pane thumbnails in sync.
                            auto *mainCanvas = window.canvas();
                            const QPointF start = mainCanvas->rect().center();
                            const QPointF end = start + QPointF(60, -24);
                            QMouseEvent down(QEvent::MouseButtonPress, start, mainCanvas->mapToGlobal(start),
                                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(mainCanvas, &down);
                            QMouseEvent move(QEvent::MouseMove, end, mainCanvas->mapToGlobal(end),
                                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                            QApplication::sendEvent(mainCanvas, &move);
                            QMouseEvent up(QEvent::MouseButtonRelease, end, mainCanvas->mapToGlobal(end),
                                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                            QApplication::sendEvent(mainCanvas, &up);
                            check(std::abs(addedIn3d->thumbnail()->yaw() - mainCanvas->yaw()) < 1e-6 &&
                                      std::abs(addedIn3d->thumbnail()->pitch() - mainCanvas->pitch()) < 1e-6 &&
                                      std::abs(mainCanvas->yaw() - 30.0) > 1.0,
                                  "Left-pane 3D thumbnails must follow main 3D drag camera");
                            mainCanvas->resetCamera();
                            check(std::abs(addedIn3d->thumbnail()->yaw() - 30.0) < 1e-6 &&
                                      std::abs(addedIn3d->thumbnail()->pitch() - 35.0) < 1e-6,
                                  "Resetting main 3D camera must reset left-pane thumbnails too");
                        }
                        window.setView(0);
                        window.restore(pattern_from_cube(CubeState{}));
                        window.applyFormula("RUR'U'R'2");
                        check(window.rawFacelets() ==
                                  format_facelets(to_facelets(cube::apply(CubeState{}, parse_moves("R U R' U' R2")))),
                              "Main window formula input must parse spaceless and R'2 notation");
                        check(window.findChild<QAction *>("shortcutsMenuAction") != nullptr,
                              "Keyboard shortcuts menu action missing");
                        check(window.shortcutFor("apply_formula") == QKeySequence(Qt::CTRL | Qt::Key_Return),
                              "Default Ctrl+Enter shortcut for apply_formula missing");
                        window.setShortcutFor("apply_formula", QKeySequence(Qt::CTRL | Qt::Key_E));
                        check(window.shortcutFor("apply_formula") == QKeySequence(Qt::CTRL | Qt::Key_E),
                              "Custom shortcut update failed");
                        window.setShortcutFor("apply_formula", QKeySequence(Qt::CTRL | Qt::Key_Return));
                        timer->stop();
                        if (!log.isEmpty()) {
                            QFile file(log);
                            if (file.open(QIODevice::WriteOnly))
                                file.write(
                                    QString(
                                        "PASS: %1 UI checks; original mouse/modifier/reset behavior, "
                                        "independent formula popup and buffered explicit/repeated Add, "
                                        "idle/start/stop/restart/Clear, actual two-phase and proven optimal "
                                        "search, day/night/2D/3D screenshots, workspace and legacy "
                                        "roundtrip, "
                                        "invalid-goal rejection and atomic rejected export, Chinese color "
                                        "picker, "
                                        "theme shortcut, proportional resizing, Escape load cancellation and "
                                        "original five-scramble action.\n")
                                        .arg(*checks)
                                        .toUtf8());
                        }
                        app.exit(0);
                    }
                } catch (const std::exception &e) {
                    timer->stop();
                    fail(QString::fromUtf8(e.what()));
                }
            });
        timer->start();
    } catch (const std::exception &e) {
        fail(QString::fromUtf8(e.what()));
    }
}
