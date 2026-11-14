#pragma once

#include <QProcessEnvironment>
#include <QString>

// Portable mode: the app folder carries its own arduino-cli, AVR core, libraries
// and drivers (made by Tools > Create Portable Version). It is portable when
// <app folder>/portable.txt exists. Layout:
//   REARMduino.exe, Qt DLLs, portable.txt
//   copy.json                 this copy's ID and name, for activation (Activation.h)
//   settings/                 QSettings as an INI file (nothing in the registry); holds the license
//   arduino-cli/arduino-cli.exe
//   arduino/data              ARDUINO_DIRECTORIES_DATA (cores, tools)
//   arduino/user/libraries    ARDUINO_DIRECTORIES_USER (bundled libraries)
//   arduino/downloads         ARDUINO_DIRECTORIES_DOWNLOADS
//   drivers/arduino, drivers/CH340
namespace Portable {

QString markerFileName();   // "portable.txt"
bool isPortable();
QString appDir();

// Call right after QApplication is created, before any QSettings is used.
void setupSettings();

// <app>/arduino-cli/arduino-cli(.exe) if portable and present, else empty.
QString bundledCli();

// Environment for arduino-cli: in portable mode, points its folders into the app folder.
QProcessEnvironment cliEnvironment();

QString driversDir();   // <app>/drivers

// The AVR compiler builds paths ~135 characters deep inside the folder, and
// Windows tools fail past 260 ("No such file or directory"): keep the folder's
// own path at most this long.
constexpr int kMaxFolderPath = 100;

}
