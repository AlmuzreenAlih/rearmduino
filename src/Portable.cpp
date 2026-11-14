#include "Portable.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace Portable {

QString markerFileName()
{
    return QStringLiteral("portable.txt");
}

QString appDir()
{
    return QCoreApplication::applicationDirPath();
}

bool isPortable()
{
    static const bool portable = QFileInfo::exists(QDir(appDir()).filePath(markerFileName()));
    return portable;
}

void setupSettings()
{
    if (!isPortable())
        return;
    // QSettings() then means <app>/settings/myide/myide.ini.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(appDir()).filePath(QStringLiteral("settings")));
}

QString bundledCli()
{
    if (!isPortable())
        return {};
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("arduino-cli.exe");
#else
    const QString name = QStringLiteral("arduino-cli");
#endif
    const QString path = QDir(appDir()).filePath(QStringLiteral("arduino-cli/") + name);
    return QFileInfo(path).isExecutable() ? path : QString();
}

QProcessEnvironment cliEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!isPortable())
        return env;
    const QDir arduino(QDir(appDir()).filePath(QStringLiteral("arduino")));
    env.insert(QStringLiteral("ARDUINO_DIRECTORIES_DATA"), QDir::toNativeSeparators(arduino.filePath(QStringLiteral("data"))));
    env.insert(QStringLiteral("ARDUINO_DIRECTORIES_USER"), QDir::toNativeSeparators(arduino.filePath(QStringLiteral("user"))));
    env.insert(QStringLiteral("ARDUINO_DIRECTORIES_DOWNLOADS"),
               QDir::toNativeSeparators(arduino.filePath(QStringLiteral("downloads"))));
    env.insert(QStringLiteral("ARDUINO_UPDATER_ENABLE_NOTIFICATION"), QStringLiteral("false"));
    return env;
}

QString driversDir()
{
    return QDir(appDir()).filePath(QStringLiteral("drivers"));
}

bool bundlesEsp8266()
{
    return isPortable() && QFileInfo(QDir(appDir()).filePath(QStringLiteral("arduino/data/packages/esp8266/hardware/esp8266"))).isDir();
}

QString pcLibrariesDir()
{
    if (!isPortable())
        return {};
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(QStringLiteral("Arduino/libraries"));
    return QFileInfo(dir).isDir() ? dir : QString();
}

}
