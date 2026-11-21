#include "../src/app/original_editor_view.hpp"
#include "../src/app/app_theme.hpp"
#include "../src/app/ui_i18n.hpp"
#include <QApplication>
#include <QFontDatabase>
#include <QMouseEvent>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
QPointF middle(const QPolygonF &p) {
    return (p[0] + p[1] + p[2] + p[3]) / 4;
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (QGuiApplication::platformName() == "offscreen") {
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto *name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "segoeuib.ttf", "consola.ttf"})
            QFontDatabase::addApplicationFont(fonts + name);
    }
    try {
        applyAppTheme(app, false);
        OriginalEditorView editor;
        editor.resize(editor.sizeHint());
        editor.show();
        QApplication::processEvents();
        auto *swatch = editor.findChild<QLabel *>("brushSwatch");
        auto *caption = editor.findChild<QLabel *>("brushCaption");
        auto *autoFix = editor.findChild<QCheckBox *>("autoFix");
        auto *viewBar = editor.findChild<QFrame *>("viewSegment");
        auto *resetCam = editor.findChild<QToolButton *>("resetCameraButton");
        require(swatch && caption && autoFix && viewBar && resetCam, "Editor embedded controls must be present");

        bool rejectedSideBySide = false;
        try {
            editor.setView(3);
        } catch (const std::invalid_argument &) {
            rejectedSideBySide = true;
        }
        require(rejectedSideBySide, "Removed 2D+3D view mode must be rejected");

        for (int width : {375, 500, 750, 1000}) {
            editor.resize(width, editor.heightForWidth(width));
            QApplication::processEvents();
            const double scale = width / 332.0;
            require(editor.height() == editor.heightForWidth(width), "Editor height must follow width");
            require(swatch->geometry() == QRect(qRound(283 * scale), qRound(190 * scale), qRound(28 * scale),
                                                qRound(28 * scale)),
                    "Compact brush swatch must keep proportional square aspect");
            require(autoFix->y() == qRound(212 * scale), "AutoFix must remain aligned at bottom-left");
            require(autoFix->font().pixelSize() == qRound(14 * width / 500.0),
                    "AutoFix font must scale with cube and embedded controls");
            const auto leftTile = editor.canvas()->stickerPolygon(36);
            require(leftTile[0].x() > 4.0, "Classic cube must retain breathing room from the left edge");
            for (int index = 0; index < 54; ++index) {
                auto polygon = editor.canvas()->stickerPolygon(index);
                const auto point = middle(polygon);
                require(editor.canvas()->stickerAt(point) == index,
                        "Original facelet indices must remain editable");
                polygon.translate(editor.canvas()->pos());
                require(!polygon.intersects(QPolygonF(QRectF(swatch->geometry()))) &&
                            !polygon.intersects(QPolygonF(QRectF(caption->geometry()))) &&
                            !polygon.intersects(QPolygonF(QRectF(autoFix->geometry()))) &&
                            !polygon.intersects(QPolygonF(QRectF(viewBar->geometry()))),
                        "Embedded controls must never cover a sticker");
            }
        }
        int seen = -1;
        Qt::MouseButton seenButton = Qt::NoButton;
        Qt::KeyboardModifiers seenModifiers;
        editor.onSticker = [&](int index, Qt::MouseButton button, Qt::KeyboardModifiers modifiers) {
            seen = index;
            seenButton = button;
            seenModifiers = modifiers;
        };
        const auto point = middle(editor.canvas()->stickerPolygon(24));
        QMouseEvent press(QEvent::MouseButtonPress, point, editor.canvas()->mapToGlobal(point), Qt::RightButton,
                          Qt::RightButton, Qt::ControlModifier | Qt::ShiftModifier);
        QApplication::sendEvent(editor.canvas(), &press);
        require(seen == 24 && seenButton == Qt::RightButton &&
                    seenModifiers == (Qt::ControlModifier | Qt::ShiftModifier),
                "Wrapper must preserve right button, Ctrl and Shift independently");
        int changes = 0;
        editor.onAutoFix = [&](bool) { ++changes; };
        editor.setAutoFix(true);
        require(editor.autoFix() && changes == 0,
                "Programmatic auto-fix initialization must not emit a user action");
        autoFix->click();
        require(!editor.autoFix() && changes == 1, "Auto-fix clicks must forward exactly one change");

        // Verify bilingual switching on embedded labels
        setUiEnglish(true);
        editor.retranslateUi();
        require(autoFix->text() == "Auto-Fix" && caption->text() == "Brush", "English labels did not apply");
        setUiEnglish(false);
        editor.retranslateUi();
        require(autoFix->text() == "自动修正" && caption->text() == "画笔", "Chinese labels did not restore");

        const std::array<QColor, 7> colors = {QColor(254, 254, 0), QColor(0, 216, 0),   QColor(238, 0, 0),
                                              QColor(255, 255, 255), QColor(0, 0, 242), QColor(255, 161, 0),
                                              QColor(128, 128, 128)};
        editor.setColors(colors);
        editor.setBrush('U');
        editor.setAutoFix(true);
        editor.resize(500, editor.heightForWidth(500));
        auto pattern = cube::pattern_from_cube(cube::apply(cube::CubeState{}, cube::parse_moves("R U F2 r' U")));
        editor.setFacelets(cube::format_pattern_facelets(pattern), pattern.center_ori);
        for (bool dark : {false, true}) {
            applyAppTheme(app, dark);
            editor.setDarkMode(dark);
            for (int mode = 0; mode < 3; ++mode) {
                editor.setView(mode);
                QApplication::processEvents();
                require(editor.view() == mode, "Editor view selection must be retained");
                require(resetCam->isVisible() == (mode == 2), "Camera reset button is 3D-only");
                for (int width : {375, 750, 1000, 500}) {
                    editor.resize(width, editor.heightForWidth(width));
                    QApplication::processEvents();
                    const auto scale = width / 332.0;
                    require(swatch->geometry() == QRect(qRound(283 * scale), qRound(190 * scale),
                                                        qRound(28 * scale), qRound(28 * scale)),
                            "All views must share the compact brush position and size");
                    require(autoFix->y() == qRound(212 * scale), "AutoFix moved when changing view");
                    const auto sample = swatch->grab().toImage();
                    require(sample.pixelColor(sample.rect().center()) == colors[0],
                            "Brush changed with view or theme");
                    for (int i = 0; i < 54; ++i) {
                        auto polygon = editor.canvas()->stickerPolygon(i);
                        if (polygon.isEmpty())
                            continue;
                        require(editor.canvas()->stickerAt(middle(polygon)) == i,
                                "Visible sticker is not selectable");
                        polygon.translate(editor.canvas()->pos());
                        require(!polygon.intersects(QPolygonF(QRectF(swatch->geometry()))) &&
                                    !polygon.intersects(QPolygonF(QRectF(caption->geometry()))) &&
                                    !polygon.intersects(QPolygonF(QRectF(autoFix->geometry()))) &&
                                    !polygon.intersects(QPolygonF(QRectF(viewBar->geometry()))),
                                "Embedded control overlaps a visible sticker");
                    }
                }
                if (argc > 1) {
                    const QString path = QString::fromLocal8Bit(argv[1]) + "/original-editor-" +
                                         QString::number(mode) + (dark ? "-night.png" : "-day.png");
                    require(editor.grab().save(path), "Original editor screenshot save failed");
                }
            }
        }
        editor.setView(0);
        auto marked = cube::format_facelets(cube::solved_facelets());
        marked[0] = 'u';
        marked[9] = 'r';
        marked[18] = 'f';
        marked[27] = 'd';
        marked[36] = 'l';
        marked[45] = 'b';
        marked[2] = '@';
        marked[11] = '@';
        marked[20] = '@';
        std::array<std::int8_t, 6> noCenters;
        noCenters.fill(-1);
        editor.setFacelets(marked, noCenters);
        for (bool dark : {false, true})
            for (int width : {375, 500, 750, 1000}) {
                applyAppTheme(app, dark, width / 500.0);
                editor.setDarkMode(dark);
                editor.resize(width, editor.heightForWidth(width));
                QApplication::processEvents();
                require(editor.height() == editor.heightForWidth(width),
                        "Marked editor keeps aspect across scales");
                if (argc > 1)
                    require(editor.grab().save(QString::fromLocal8Bit(argv[1]) + "/editor-markers-scale-" +
                                               QString::number(width / 5) + (dark ? "-night.png" : "-day.png")),
                            "Scaled marker screenshot save failed");
            }
        std::cout << "Original editor: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
