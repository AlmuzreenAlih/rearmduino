#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("myide"));

    // Optional first argument: a sketch folder or .ino file.
    const QStringList args = QApplication::arguments();
    MainWindow window(args.size() > 1 ? args.at(1) : QString());
    window.show();
    return app.exec();
}
