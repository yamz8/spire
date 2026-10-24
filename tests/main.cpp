#include <QGuiApplication>
#include <QQuickStyle>
#include <QStandardPaths>

int runSpireTests(int argc, char **argv);
int runWindowTests(int argc, char **argv);

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName("spire");
    app.setApplicationName("spire");
    // Settings (the last project, the last folder) go to a test location,
    // not the user's own.
    QStandardPaths::setTestModeEnabled(true);
    QQuickStyle::setStyle("Material");
    int failed = runSpireTests(argc, argv);
    failed |= runWindowTests(argc, argv);
    return failed;
}
