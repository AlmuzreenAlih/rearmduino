#pragma once

#include <QString>

#include <functional>

class QObject;

// Machine activation (Windows Release builds only; CMake defines MYIDE_ACTIVATION for them).
// The app runs on a PC only after the developer approves that PC on the server. Approval comes
// back as a license signed by the server, saved with the settings and verified here offline
// (signature + this PC's machine hash), so a normal start needs no network. Copying an approved
// folder to another PC gives a different machine hash, so that PC has to be approved too.
// Server side: webserviceapp2/projects/rearm (routes/ide.js, routes/README.md).
// This stops casual copying only; it is not meant to resist cracking.
namespace Activation {

// SHA-256 (64 hex chars) of this PC's MachineGuid and system-drive volume serial.
QString machineHash();

// "K7P2-9XQM": the first 40 bits of the machine hash in Crockford base32. The server computes
// the same code, so it can be shown before (or without) reaching the server.
QString requestCode();

// True if a saved license is signed by the server and is for this PC. Local and fast.
bool hasValidLicense();

// Saves a license from the server. False (nothing saved) if it isn't valid for this PC.
bool saveLicense(const QString &license);
void clearLicense();

// Records this start locally (time and count); reported to the server at the next check, so
// "last used" is known even for starts made offline.
void noteStart();

enum class Status {
    Approved,      // the server sent a license
    NotApproved,   // pending, denied or revoked (the server answers the same for all three)
    Unreachable,   // no connection
    ServerError,   // reached something, but not a usable answer
};

struct Result {
    Status status = Status::Unreachable;
    QString license;   // only when Approved
};

// Asks the server about this PC on a background thread (WinHTTP); never blocks the UI.
// `done` runs on the UI thread, unless `context` has been destroyed or the app is quitting.
void check(QObject *context, std::function<void(const Result &)> done);

// <app folder>/copy.json, written by the portable builder: {"copyId": "<uuid>", "copyName": "..."}.
QString copyFileName();

}
