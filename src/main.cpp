#include "MainWindow.h"
#include "Portable.h"

#include <QApplication>
#include <QFont>
#include <QIcon>

int main(int argc, char *argv[])
{
    // High-DPI: must be set before QApplication exists. PassThrough keeps
    // fractional Windows scaling (125%, 150%) instead of rounding to 1x/2x.
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("myide"));   // QSettings location
    QApplication::setApplicationName(QStringLiteral("myide"));
    Portable::setupSettings();   // portable copy: settings in its own folder, not the registry
    // Same look on Windows and the Pi, independent of the Pi's qt5ct settings.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // UI text at least 10pt (Windows defaults to 9pt).
    QFont uiFont = QApplication::font();
    if (uiFont.pointSizeF() > 0 && uiFont.pointSizeF() < 10)
        uiFont.setPointSizeF(10);
    QApplication::setFont(uiFont);

    // REARMduino logo: window/taskbar icon (the .exe file icon comes from rearmduino.rc).
    QIcon appIcon;
    for (int size : {16, 32, 48, 64, 128, 256})
        appIcon.addFile(QStringLiteral(":/app/rearmduino-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(appIcon);

    // Optional first argument: a sketch folder or .ino file.
    const QStringList args = QApplication::arguments();
    MainWindow window(args.size() > 1 ? args.at(1) : QString());
    window.show();
    return app.exec();
}
