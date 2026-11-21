#include "main_window.hpp"
#include "ui_smoke.hpp"
#include "ui_localization.hpp"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    ChineseUiTranslator translator;
    app.installTranslator(&translator);
    if (QGuiApplication::platformName() == "offscreen") {
        const auto fonts = qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/";
        for (const auto *name : {"msyh.ttc", "msyhbd.ttc", "segoeui.ttf", "segoeuib.ttf", "segoeuisl.ttf",
                                 "consola.ttf", "consolab.ttf", "calibri.ttf", "calibrib.ttf"})
            QFontDatabase::addApplicationFont(fonts + name);
    }
    QApplication::setApplicationName("CubeExplorerModern");
    QApplication::setApplicationVersion(CUBE_GUI_VERSION);
    QApplication::setWindowIcon(QIcon(":/rubik.ico"));
    QApplication::setOrganizationName("CubeExplorerModern");
    QApplication::setStyle("Fusion");
    QString screenshot, smokeLog;
    bool smoke = false;
    for (const auto &arg : app.arguments()) {
        if (arg == "--smoke-test")
            smoke = true;
        else if (arg.startsWith("--screenshot="))
            screenshot = arg.mid(13);
        else if (arg.startsWith("--smoke-log="))
            smokeLog = arg.mid(12);
    }
    app.setProperty("testMode", smoke);
    MainWindow window;
    window.show();
    if (smoke)
        QTimer::singleShot(0, &window, [&] { runUiSmoke(window, app, screenshot, smokeLog); });
    else if (!screenshot.isEmpty())
        QTimer::singleShot(400, &window, [&] {
            QDir().mkpath(QFileInfo(screenshot).absolutePath());
            app.exit(window.grab().save(screenshot) ? 0 : 3);
        });
    return app.exec();
}
