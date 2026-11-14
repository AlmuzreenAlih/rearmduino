#include "UsbDrivers.h"
#include "Portable.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>

#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>
#include <shellapi.h>
#endif

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace UsbDrivers {

namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("UsbDrivers", text);
}

bool hasFiles(const QString &dir, const QStringList &patterns)
{
    QDirIterator it(dir, patterns, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

#ifdef Q_OS_WIN
// Runs a program with administrator rights (UAC prompt), then calls
// done(started, exitCode, error) on the context's thread when it ends.
void runElevated(const QString &program, const QString &params, QObject *context,
                 std::function<void(bool started, unsigned long code, const QString &error)> done)
{
    const std::wstring wprogram = QDir::toNativeSeparators(program).toStdWString();
    const std::wstring wparams = params.toStdWString();
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";   // UAC prompt
    sei.lpFile = wprogram.c_str();
    sei.lpParameters = wparams.empty() ? nullptr : wparams.c_str();
    sei.nShow = params.isEmpty() ? SW_SHOWNORMAL : SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        const DWORD err = GetLastError();
        done(false, 0, err == ERROR_CANCELLED ? tr("Cancelled (administrator permission was not given).")
                                              : tr("Could not start %1 (error %2).").arg(QFileInfo(program).fileName()).arg(err));
        return;
    }
    auto *notifier = new QWinEventNotifier(sei.hProcess, context);
    QObject::connect(notifier, &QWinEventNotifier::activated, context, [notifier, done](HANDLE h) {
        notifier->setEnabled(false);   // before the handle is closed
        notifier->deleteLater();
        DWORD code = 0;
        GetExitCodeProcess(h, &code);
        CloseHandle(h);
        done(true, code, QString());
    });
}
#endif
}

QString kindName(Kind kind)
{
    return kind == Kind::Ch340 ? QStringLiteral("CH340") : QStringLiteral("Arduino");
}

QString bundledDir(Kind kind)
{
    const QString dir = QDir(Portable::driversDir()).filePath(kind == Kind::Ch340 ? QStringLiteral("CH340")
                                                                                   : QStringLiteral("arduino"));
    return hasFiles(dir, {QStringLiteral("*.inf"), QStringLiteral("*.exe")}) ? dir : QString();
}

#ifdef Q_OS_WIN

bool supported()
{
    return true;
}

QList<Device> boards()
{
    QList<Device> result;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return result;
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        wchar_t buf[1024] = {};
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr,
                                               reinterpret_cast<BYTE *>(buf), sizeof(buf) - sizeof(wchar_t), nullptr))
            continue;
        const QString hwid = QString::fromWCharArray(buf).toUpper();   // first of the multi-string
        Kind kind;
        if (hwid.contains(QLatin1String("VID_1A86&PID_7523")) || hwid.contains(QLatin1String("VID_1A86&PID_5523"))
            || hwid.contains(QLatin1String("VID_4348&PID_5523")))
            kind = Kind::Ch340;
        else if (hwid.contains(QLatin1String("VID_2341&")) || hwid.contains(QLatin1String("VID_2A03&"))
                 || hwid.contains(QLatin1String("VID_0403&PID_6001")))
            kind = Kind::Arduino;
        else
            continue;

        QString name;
        for (DWORD prop : {DWORD(SPDRP_FRIENDLYNAME), DWORD(SPDRP_DEVICEDESC)}) {
            wchar_t nameBuf[512] = {};
            if (SetupDiGetDeviceRegistryPropertyW(set, &info, prop, nullptr, reinterpret_cast<BYTE *>(nameBuf),
                                                  sizeof(nameBuf) - sizeof(wchar_t), nullptr)) {
                name = QString::fromWCharArray(nameBuf);
                break;
            }
        }
        ULONG status = 0, problem = 0;
        const bool hasProblem = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS
                                && (status & DN_HAS_PROBLEM);
        const bool noDriver = hasProblem && (problem == CM_PROB_FAILED_INSTALL || problem == CM_PROB_NOT_CONFIGURED);
        result.append({name, hwid.section(QLatin1Char('&'), 0, 1), kind, !noDriver});
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

QString installedCh340Version()
{
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    for (const QString &file : {QStringLiteral("CH341S64.SYS"), QStringLiteral("CH341SER.SYS")}) {
        const QString path = QDir(QString::fromWCharArray(sysDir)).filePath(QStringLiteral("drivers/") + file);
        if (!QFileInfo::exists(path))
            continue;
        const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
        DWORD handle = 0;
        const DWORD size = GetFileVersionInfoSizeW(wpath.c_str(), &handle);
        QByteArray data(int(size), 0);
        VS_FIXEDFILEINFO *fixed = nullptr;
        UINT len = 0;
        if (size && GetFileVersionInfoW(wpath.c_str(), 0, size, data.data())
            && VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&fixed), &len) && fixed)
            return QStringLiteral("%1.%2").arg(HIWORD(fixed->dwFileVersionMS)).arg(LOWORD(fixed->dwFileVersionMS));
        return tr("installed");
    }
    return {};
}

void install(Kind kind, QObject *context, std::function<void(bool, const QString &)> done)
{
    const QString dir = bundledDir(kind);
    if (dir.isEmpty()) {
        done(false, tr("No %1 driver files are bundled (expected in %2).")
                        .arg(kindName(kind), QDir::toNativeSeparators(QDir(Portable::driversDir()).filePath(
                                                 kind == Kind::Ch340 ? QStringLiteral("CH340") : QStringLiteral("arduino")))));
        return;
    }
    // INF files: add them to the driver store and install on matching devices
    // (pnputil, built into Windows 10/11). Otherwise run the vendor's installer.
    QString program, params;
    if (hasFiles(dir, {QStringLiteral("*.inf")})) {
        wchar_t sysDir[MAX_PATH] = {};
        GetSystemDirectoryW(sysDir, MAX_PATH);
        program = QDir(QString::fromWCharArray(sysDir)).filePath(QStringLiteral("pnputil.exe"));
        params = QStringLiteral("/add-driver \"%1\" /subdirs /install")
                     .arg(QDir::toNativeSeparators(QDir(dir).filePath(QStringLiteral("*.inf"))));
    } else {
        QDirIterator it(dir, {QStringLiteral("*.exe")}, QDir::Files, QDirIterator::Subdirectories);
        program = it.next();
    }

    runElevated(program, params, context, [done, kind](bool started, unsigned long code, const QString &error) {
        if (!started)
            done(false, error);
        // pnputil: 0 OK, 3010 OK but reboot needed, 259 nothing new to install.
        else if (code == 0)
            done(true, tr("%1 driver installed. Unplug the board and plug it in again.").arg(kindName(kind)));
        else if (code == 3010)
            done(true, tr("%1 driver installed; Windows needs a restart to finish.").arg(kindName(kind)));
        else if (code == 259)
            done(true, tr("%1 driver: Windows already had this driver or a newer one; nothing changed.").arg(kindName(kind)));
        else
            done(false, tr("%1 driver installer finished with code %2.").arg(kindName(kind)).arg(code));
    });
}

// The device instance ID behind a COM port, e.g. USB\VID_2341&PID_0042\85735313...
// For a composite device (native USB boards: the port is interface "&MI_00"), the
// parent, so the whole USB device restarts.
QString portDeviceId(const QString &portName)
{
    QString result;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (set == INVALID_HANDLE_VALUE)
        return result;
    SP_DEVINFO_DATA info;
    info.cbSize = sizeof(info);
    for (DWORD i = 0; result.isEmpty() && SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE)
            continue;
        wchar_t name[64] = {};
        DWORD size = sizeof(name) - sizeof(wchar_t);
        DWORD type = 0;
        const bool ok = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE *>(name), &size) == ERROR_SUCCESS
                        && type == REG_SZ;
        RegCloseKey(key);
        if (!ok || QString::fromWCharArray(name).compare(portName, Qt::CaseInsensitive) != 0)
            continue;
        DEVINST inst = info.DevInst;
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(inst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            continue;
        DEVINST parent = 0;
        if (QString::fromWCharArray(id).contains(QLatin1String("&MI_"), Qt::CaseInsensitive)
            && CM_Get_Parent(&parent, inst, 0) == CR_SUCCESS
            && CM_Get_Device_IDW(parent, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
            continue;
        result = QString::fromWCharArray(id);
    }
    SetupDiDestroyDeviceInfoList(set);
    return result;
}

void restartPort(const QString &portName, QObject *context, std::function<void(bool, const QString &)> done)
{
    const QString id = portDeviceId(portName);
    if (id.isEmpty()) {
        done(false, tr("%1 is not in Windows' device list (unplugged?).").arg(portName));
        return;
    }
    wchar_t sysDir[MAX_PATH] = {};
    GetSystemDirectoryW(sysDir, MAX_PATH);
    const QString pnputil = QDir(QString::fromWCharArray(sysDir)).filePath(QStringLiteral("pnputil.exe"));
    // /restart-device: Windows 10 version 2004 and later.
    runElevated(pnputil, QStringLiteral("/restart-device \"%1\"").arg(id), context,
                [done, portName](bool started, unsigned long code, const QString &error) {
        if (!started)
            done(false, error);
        else if (code == 0)
            done(true, tr("Restarted the USB device of %1.").arg(portName));
        else if (code == 3010)
            done(false, tr("%1: Windows needs a restart to finish restarting the device. Unplug and plug the board in instead.").arg(portName));
        else
            done(false, tr("Could not restart the USB device of %1 (pnputil code %2). Unplug the board and plug it in again.")
                            .arg(portName).arg(code));
    });
}

#else   // not Windows: boards use the kernel's drivers

bool supported() { return false; }
QList<Device> boards() { return {}; }
QString installedCh340Version() { return {}; }
void install(Kind, QObject *, std::function<void(bool, const QString &)> done)
{
    done(false, tr("Driver installation is only needed on Windows."));
}

#ifdef Q_OS_LINUX
// USB reset (USBDEVFS_RESET) of the device behind /dev/ttyACM0 or /dev/ttyUSB0:
// the kernel re-enumerates it, as when it is plugged in. Needs write access to
// its /dev/bus/usb/BBB/DDD file: linux/99-myide-usb-reset.rules grants that to
// the dialout group.
void restartPort(const QString &portName, QObject *, std::function<void(bool, const QString &)> done)
{
    const QString tty = QFileInfo(portName).fileName();   // ttyACM0
    // /sys/class/tty/ttyACM0/device -> .../1-1.2/1-1.2:1.0 (interface); walk up
    // to the USB device directory (the one with busnum/devnum).
    QDir dir(QFileInfo(QStringLiteral("/sys/class/tty/%1/device").arg(tty)).canonicalFilePath());
    QString busnum, devnum;
    for (int depth = 0; depth < 4 && !dir.isRoot(); ++depth) {
        QFile bus(dir.filePath(QStringLiteral("busnum"))), dev(dir.filePath(QStringLiteral("devnum")));
        if (bus.open(QIODevice::ReadOnly) && dev.open(QIODevice::ReadOnly)) {
            busnum = QString::fromLatin1(bus.readAll()).trimmed();
            devnum = QString::fromLatin1(dev.readAll()).trimmed();
            break;
        }
        dir.cdUp();
    }
    if (busnum.isEmpty() || devnum.isEmpty()) {
        done(false, tr("%1 is not a USB device the IDE can reset (unplugged?). Unplug the board and plug it in again.")
                        .arg(portName));
        return;
    }
    const QString node = QStringLiteral("/dev/bus/usb/%1/%2").arg(busnum.toInt(), 3, 10, QLatin1Char('0'))
                                                              .arg(devnum.toInt(), 3, 10, QLatin1Char('0'));
    const int fd = ::open(QFile::encodeName(node).constData(), O_WRONLY);
    if (fd < 0) {
        const int err = errno;
        if (err == EACCES || err == EPERM)
            done(false, tr("No permission to reset %1 (%2). Install the IDE's USB rule once:\n"
                           "  sudo cp linux/99-myide-usb-reset.rules /etc/udev/rules.d/\n"
                           "  sudo udevadm control --reload-rules && sudo udevadm trigger --subsystem-match=usb\n"
                           "For now, unplug the board and plug it in again.").arg(portName, node));
        else
            done(false, tr("Cannot open %1: %2").arg(node, QString::fromLocal8Bit(strerror(err))));
        return;
    }
    const int rc = ::ioctl(fd, USBDEVFS_RESET, 0);
    const int err = errno;
    ::close(fd);
    if (rc < 0)
        done(false, tr("USB reset of %1 failed: %2. Unplug the board and plug it in again.")
                        .arg(portName, QString::fromLocal8Bit(strerror(err))));
    else
        done(true, tr("Reset the USB device of %1.").arg(portName));
}
#else
void restartPort(const QString &portName, QObject *, std::function<void(bool, const QString &)> done)
{
    done(false, tr("Unplug the board on %1 and plug it in again.").arg(portName));
}
#endif

#endif

bool canRestartPorts()
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    return true;
#else
    return false;
#endif
}

}
