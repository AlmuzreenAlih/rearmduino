#include "MainWindow.h"

#include <QApplication>
#include <QFont>

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
    // Same look on Windows and the Pi, independent of the Pi's qt5ct settings.
    QApplication::setStyle(QStringLiteral("Fusion"));

    // UI text at least 10pt (Windows defaults to 9pt).
    QFont uiFont = QApplication::font();
    if (uiFont.pointSizeF() > 0 && uiFont.pointSizeF() < 10)
        uiFont.setPointSizeF(10);
    QApplication::setFont(uiFont);

    // Optional first argument: a sketch folder or .ino file.
    const QStringList args = QApplication::arguments();
    MainWindow window(args.size() > 1 ? args.at(1) : QString());
    window.show();
    return app.exec();
}
