#include "Activation.h"
#include "ActivationKey.h"
#include "Portable.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QSysInfo>

#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

namespace Activation {

namespace {

const QString kLicenseKey = QStringLiteral("activation/license");
const QString kLastUsedKey = QStringLiteral("activation/lastUsedAt");
const QString kRunCountKey = QStringLiteral("activation/runCount");

QByteArray fromBase64Url(const QString &text)
{
    return QByteArray::fromBase64(text.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

// ECDSA P-256 / SHA-256 with Windows CNG. signature: raw r||s, 64 bytes.
bool signatureValid(const QByteArray &payload, const QByteArray &signature)
{
    if (signature.size() != 64)
        return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)))
        return false;
    struct {
        BCRYPT_ECCKEY_BLOB header;
        UCHAR xy[64];
    } blob;
    blob.header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    blob.header.cbKey = 32;
    std::memcpy(blob.xy, ActivationKey::kPublicKeyXY, sizeof(blob.xy));

    bool valid = false;
    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                           reinterpret_cast<PUCHAR>(&blob), sizeof(blob), 0))) {
        QByteArray hash = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
        QByteArray sig = signature;
        valid = BCRYPT_SUCCESS(BCryptVerifySignature(key, nullptr, reinterpret_cast<PUCHAR>(hash.data()),
                                                     ULONG(hash.size()), reinterpret_cast<PUCHAR>(sig.data()),
                                                     ULONG(sig.size()), 0));
        BCryptDestroyKey(key);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return valid;
}

// "<payload>.<signature>", both base64url; the signature covers the payload bytes as received.
bool licenseValid(const QString &license)
{
    const QStringList parts = license.split(QLatin1Char('.'));
    if (parts.size() != 2)
        return false;
    const QByteArray payload = fromBase64Url(parts.at(0));
    if (payload.isEmpty() || !signatureValid(payload, fromBase64Url(parts.at(1))))
        return false;
    const QJsonObject json = QJsonDocument::fromJson(payload).object();
    return json.value(QStringLiteral("v")).toInt() == 1
           && json.value(QStringLiteral("machine")).toString() == machineHash();
}

struct HttpReply {
    bool reached = false;   // got an HTTP response
    int status = 0;
    QByteArray body;
};

// Blocking; call on a worker thread only.
HttpReply postJson(const std::wstring &url, const QByteArray &body)
{
    HttpReply reply;
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    URL_COMPONENTS parts = {};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = DWORD(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = DWORD(std::size(path));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
        return reply;

    const std::wstring agent = L"REARMduino/" + QStringLiteral(MYIDE_VERSION).toStdWString();
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        return reply;
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 12000);   // resolve, connect, send, receive (ms)
    HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
    HINTERNET request = connection
        ? WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
        : nullptr;
    if (request
        && WinHttpSendRequest(request, L"Content-Type: application/json\r\n", DWORD(-1),
                              const_cast<char *>(body.constData()), DWORD(body.size()), DWORD(body.size()), 0)
        && WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            reply.reached = true;
            reply.status = int(status);
            char buffer[4096];
            DWORD read = 0;
            // The answer is a few hundred bytes; stop at 64 KB whatever the server sends.
            while (reply.body.size() < 64 * 1024 && WinHttpReadData(request, buffer, sizeof(buffer), &read) && read > 0)
                reply.body.append(buffer, int(read));
        }
    }
    if (request)
        WinHttpCloseHandle(request);
    if (connection)
        WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return reply;
}

Result toResult(const HttpReply &reply)
{
    Result result;
    if (!reply.reached)
        return result;   // Unreachable
    result.status = Status::ServerError;
    if (reply.status != 200)
        return result;
    const QJsonObject json = QJsonDocument::fromJson(reply.body).object();
    const QString status = json.value(QStringLiteral("status")).toString();
    const QString license = json.value(QStringLiteral("license")).toString();
    if (status == QLatin1String("approved") && !license.isEmpty()) {
        result.status = Status::Approved;
        result.license = license;
    } else if (status == QLatin1String("pending")) {
        result.status = Status::NotApproved;
    }
    return result;
}

// Lets a worker thread hand its result to the UI thread safely while the app may be quitting:
// alive is cleared (under the mutex) before QCoreApplication goes away. Never destroyed, because
// a worker can still be running, blocked in WinHTTP, when the process exits.
struct Shared {
    std::mutex mutex;
    bool alive = true;
};

Shared &shared()
{
    static Shared *instance = [] {
        qAddPostRoutine([] {
            std::lock_guard<std::mutex> lock(shared().mutex);
            shared().alive = false;
        });
        return new Shared;
    }();
    return *instance;
}

QJsonObject copyInfo()
{
    QFile file(QDir(Portable::appDir()).filePath(copyFileName()));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

QString copyFileName()
{
    return QStringLiteral("copy.json");
}

QString machineHash()
{
    static const QString hash = [] {
        wchar_t guid[128] = {};
        DWORD size = sizeof(guid) - sizeof(wchar_t);
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, guid, &size);
        // Volume serial of the drive Windows is on (normally C:).
        wchar_t root[MAX_PATH] = L"C:\\";
        if (GetWindowsDirectoryW(root, MAX_PATH) >= 3)
            root[3] = L'\0';
        DWORD serial = 0;
        GetVolumeInformationW(root, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
        const QByteArray id = QString::fromWCharArray(guid).trimmed().toLower().toUtf8() + '|'
                              + QByteArray::number(qulonglong(serial), 16);
        return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha256).toHex());
    }();
    return hash;
}

QString requestCode()
{
    static const char alphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";   // Crockford base32
    const quint64 bits = machineHash().left(10).toULongLong(nullptr, 16);
    QString code;
    for (int shift = 35; shift >= 0; shift -= 5)
        code += QLatin1Char(alphabet[(bits >> shift) & 31]);
    return code.left(4) + QLatin1Char('-') + code.mid(4);
}

bool hasValidLicense()
{
    const QString license = QSettings().value(kLicenseKey).toString();
    return !license.isEmpty() && licenseValid(license);
}

bool saveLicense(const QString &license)
{
    if (!licenseValid(license))
        return false;
    QSettings().setValue(kLicenseKey, license);
    return true;
}

void clearLicense()
{
    QSettings settings;
    settings.remove(kLicenseKey);
    settings.sync();
}

void noteStart()
{
    QSettings settings;
    settings.setValue(kLastUsedKey, QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.setValue(kRunCountKey, settings.value(kRunCountKey).toInt() + 1);
}

void check(QObject *context, std::function<void(const Result &)> done)
{
    QSettings settings;
    QJsonObject body;
    body.insert(QStringLiteral("machine"), machineHash());
    const QJsonObject copy = copyInfo();
    if (!copy.value(QStringLiteral("copyId")).toString().isEmpty())
        body.insert(QStringLiteral("copyId"), copy.value(QStringLiteral("copyId")).toString());
    QString copyName = copy.value(QStringLiteral("copyName")).toString();
    if (copyName.isEmpty())
        copyName = Portable::isPortable() ? QStringLiteral("portable (no copy.json)") : QStringLiteral("dev");
    body.insert(QStringLiteral("copyName"), copyName);
    body.insert(QStringLiteral("pcName"), QSysInfo::machineHostName());
    body.insert(QStringLiteral("winUser"), qEnvironmentVariable("USERNAME"));
    body.insert(QStringLiteral("appVersion"), QStringLiteral(MYIDE_VERSION));
    const QString lastUsed = settings.value(kLastUsedKey).toString();
    if (!lastUsed.isEmpty())
        body.insert(QStringLiteral("lastUsedAt"), lastUsed);
    body.insert(QStringLiteral("runCount"), settings.value(kRunCountKey).toInt());

    const QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    const std::wstring url = QStringLiteral(MYIDE_ACTIVATION_URL "/api/ide/check").toStdWString();
    const QPointer<QObject> guard(context);
    Shared &state = shared();   // created here, on the UI thread, while the app exists
    std::thread([&state, url, json, guard, done] {
        const Result result = toResult(postJson(url, json));
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!state.alive)
            return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, done, result] {
            if (guard)
                done(result);
        }, Qt::QueuedConnection);
    }).detach();
}

}
