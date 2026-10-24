// spire — progress billing for contractors: a schedule of values, billed
// period by period, and the pay application (G702/G703) as a PDF.

#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "cli.h"
#include "portalfilepicker.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands and help draw no window, so they must not need a display,
    // even where the desktop exports QT_QPA_PLATFORM=wayland. Bare spire
    // prints help, as a command line tool should; launchers say spire open.
    const bool command = argc == 1 || isCliCommand(argv[1]) || QByteArray(argv[1]).startsWith("-");
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);
    app.setApplicationName("spire");
    app.setOrganizationName("spire");
    app.setApplicationVersion("0.1.0");

    // Matches the window to spire.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("spire");
    app.setWindowIcon(QIcon::fromTheme("spire"));

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes. Pay applications
    // written from the command line use the desktop's language too.
    Translator translator;
    app.installTranslator(&translator);

    if (command) {
        const QString first = app.arguments().value(1);
        if (first == "--version" || first == "-v") {
            fprintf(stdout, "spire %s\n", qPrintable(app.applicationVersion()));
            return 0;
        }
        if (first.startsWith("-"))
            return runCli({app.arguments().first(), "help"});
        return runCli(app.arguments());
    }

    QStringList arguments = app.arguments();
    if (arguments.value(1) == "open")
        arguments.removeAt(1);

    QQuickStyle::setStyle("Material");

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    // A project named on the command line, or else the last one worked on.
    if (arguments.size() > 1)
        backend.openProjectAt(QFileInfo(arguments.at(1)).absoluteFilePath());
    else
        backend.reopenLastProject();

    return app.exec();
}
