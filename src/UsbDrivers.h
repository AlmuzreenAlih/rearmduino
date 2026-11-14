#pragma once

#include <QList>
#include <QString>

#include <functional>

class QObject;

// USB driver check/installation for boards (Windows; no-ops elsewhere).
// Only boards that are plugged in WITHOUT any driver are offered an install, so
// a driver that is already installed (possibly newer) is never replaced.
namespace UsbDrivers {

enum class Kind {
    Arduino,   // genuine boards (VID 2341/2A03) and FTDI: drivers/arduino
    Ch340      // clones with a WCH CH340/CH341: drivers/CH340
};

struct Device {
    QString name;         // as Windows shows it
    QString hardwareId;   // e.g. USB\VID_1A86&PID_7523
    Kind kind;
    bool hasDriver;
};

bool supported();                  // Windows only
QList<Device> boards();            // connected Arduino-type USB devices
QString kindName(Kind kind);       // "CH340", "Arduino"
QString installedCh340Version();   // version of the installed CH340 driver, empty if none
QString bundledDir(Kind kind);     // folder with this driver's files, empty if not bundled

// Installs the bundled driver (asks for administrator rights via UAC), then calls
// done(ok, message) on the context's thread.
void install(Kind kind, QObject *context, std::function<void(bool ok, const QString &message)> done);

// Restarts the USB device behind a serial port, like unplugging it and plugging it
// back in. For a port left "busy"/stuck after the board's USB chip was upset
// (motor or relay noise). Windows: pnputil /restart-device, administrator rights
// via UAC. Linux: USB reset, allowed by linux/99-myide-usb-reset.rules (else the
// message says how to install it). Calls done(ok, message) on the context's thread.
bool canRestartPorts();   // Windows and Linux
void restartPort(const QString &portName, QObject *context, std::function<void(bool ok, const QString &message)> done);

}
