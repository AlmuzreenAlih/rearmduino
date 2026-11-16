# Project: REARMduino (lightweight Arduino IDE)

A fast-starting desktop Arduino IDE that runs on Windows and Raspberry Pi OS Bullseye.
Startup speed is the top priority: the main window must appear almost instantly, even on the Pi.

## Name and logo

- User-facing name: **REARMduino** (window title `<sketch> | REARMduino`, About, portable
  readme/folder name). The program file is `REARMduino.exe` / `REARMduino` (CMake `OUTPUT_NAME`).
- Internal name stays `myide`: CMake target and project, QSettings organization/application
  (registry `HKCU\Software\myide\myide`, or `settings/myide/myide.ini` in a portable copy), temp
  folders, `linux/99-myide-usb-reset.rules`. Renaming the QSettings names would reset users' settings.
- Logo: `resources/app/` (`rearmduino-512.png` is the master). The 16-256 px PNGs are in
  `resources.qrc` for the window icon (set in `main.cpp`); `rearmduino.ico` + `rearmduino.rc` give
  the Windows .exe icon (added in CMake only on WIN32).

## Portable version and USB drivers (Windows)

- Tools > Create Portable Version always bundles the Arduino drivers (from the AVR core) in
  `drivers/arduino`. The CH340 driver (clone boards) is bundled in `drivers/CH340` only if a
  folder/.zip/.exe is given in the builder.
- Boards: the builder lists every installed core; AVR is always bundled, the others (ESP32,
  ESP8266, ...) are ticked per copy and copied with the tools named in their `installed.json`.
  arduino-cli reads boards from one data folder only, so a copy can't use boards installed on the
  other PC; they have to be bundled.
- Libraries: a portable copy also compiles with the other PC's `Documents\Arduino\libraries`
  (`--libraries`, `Portable::pcLibrariesDir()`), besides the bundled ones.
- Folder path limits (Windows 260-character limit inside the compilers), measured: AVR and ESP32
  about 100 characters, **ESP8266 about 55** (core 3.1.2 compiled from 45, failed from 60). The
  builder and the app's startup log warn using these (`Portable.h`).
- Zip: made with Windows' `System32\tar.exe`. It crashes on file names outside the system code
  page (seen: Cyrillic-named board files in the ESP32 and UNO R4 openocd tools), so files with
  non-ASCII names are left out of the zip and listed in the builder's log; the folder keeps them.
  With all boards the folder is about 6.8 GB (zip about 2 GB); some ESP32 paths pass 260
  characters, so the zip should be extracted with WinRAR/7-Zip, not Explorer.
- Updating a copy after an app change: send only the new `REARMduino.exe` to replace in the
  folder; boards, libraries, settings and the license stay.
- Drivers are never installed silently: when a plugged-in board has no driver, the app offers the
  bundled one (UAC prompt). Without a bundled CH340 driver it only tells the user to install
  CH341SER.EXE from wch.cn (Windows Update often installs one when online).

## Machine activation (Windows Release builds)

- Every Windows **Release** build (portable or not, including the developer's own) runs only on a
  PC the developer approved. Debug builds and Linux/the Pi have no check. CMake defines
  `MYIDE_ACTIVATION` for Release only; the code is `src/Activation.*` and `src/ActivationWindow.*`.
- Start: the saved license (QSettings `activation/license`) is verified locally: ECDSA P-256
  signature (Windows CNG, public key in `src/ActivationKey.h`) + this PC's machine hash. No network.
  Valid: the main window opens as usual. Not valid: a small waiting window shows the request code
  and asks the server (and again every minute) until the PC is approved.
- 5 seconds after the main window appears, a background thread (WinHTTP) tells the server the copy
  was used and learns if the PC was revoked. Revoked: warn, Save/Discard, delete the license, close.
  Offline: nothing happens; an approved PC works offline forever.
- Server: `rearm.webserviceapp.online/api/ide/check`, in the repo
  `C:\SirAlih\Projects\htdocs\webserviceapp2` (`projects/rearm`; wire format in its
  `src/routes/README.md`). Approve or revoke by editing `ide_machines.status` in Adminer.
  `-DMYIDE_ACTIVATION_URL=http://127.0.0.1:<port>` builds a copy that talks to a local backend.
- The portable builder writes `copy.json` (random copy ID + the copy name typed in the builder).
  It must never copy `settings/` (that would copy a license).
- No new libraries for this: CNG (`bcrypt`) and WinHTTP are part of Windows.
- Threat model: casual copying only. Don't add obfuscation or anti-tamper code.

## Tech stack (do not change without asking)

- Language: C++17
- GUI: Qt 5.15 **Widgets** only. No Qt 6 APIs, no QML/Qt Quick (Bullseye only ships Qt 5.15).
- Code editor: QScintilla (Qt5 build)
- Serial monitor: QtSerialPort
- Build system: CMake + Ninja
- Backend: `arduino-cli`, called as a subprocess via `QProcess`. Never reimplement compiling,
  uploading, board detection, or library/core management.

## Platforms and versions

### Windows (main development machine)
- Windows, Visual Studio 2022 (MSVC, "Desktop development with C++" workload)
- Qt 5.15.2 at `C:\Qt\5.15.2\msvc2019_64` (installed with aqtinstall; MSVC 2019 build works with VS 2022)
- QScintilla built from source with qmake and installed into the Qt folder above:
  - libs: `C:\Qt\5.15.2\msvc2019_64\lib\qscintilla2_qt5.lib` (release) and `qscintilla2_qt5d.lib` (debug)
  - headers: `C:\Qt\5.15.2\msvc2019_64\include\Qsci\`
  - DLLs in `C:\Qt\5.15.2\msvc2019_64\bin\`
- arduino-cli **1.4.1** (Commit e39419312, 2026-01-19)
- Pass `-DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64` when configuring.
- Use `windeployqt` (in Qt's bin folder) to make a runnable folder outside Visual Studio.

### Raspberry Pi (Linux test target)
- Raspberry Pi OS **Bullseye, 64-bit (aarch64)**, kernel 6.1.21-v8+
- Reachable from Windows with `ssh rpi` (key-based login, no password prompt)
- User: `rpi`, home: `/home/rpi`
- arduino-cli **1.5.1** (Commit 01f3d4f2b, 2026-06-05)
  - real binary: `/home/rpi/.local/bin/arduino-cli`, symlinked to `/usr/local/bin/arduino-cli`
- Qt 5.15 and QScintilla come from apt. Required packages:
  `build-essential cmake ninja-build git qtbase5-dev libqt5serialport5-dev libqscintilla2-qt5-dev libqt5svg5-dev`
  (check with `dpkg -s` before assuming they are installed; ask me before running `sudo apt install`)

## arduino-cli notes

- The two machines run different versions (1.4.1 vs 1.5.1). Do not rely on behavior that only
  exists in one. If JSON output differs between them, handle both or tell me.
- Use the global `--json` flag for machine-readable output (confirmed in 1.5.1's help; check
  `arduino-cli --help` on Windows to confirm 1.4.1 supports it the same way).
- Run arduino-cli asynchronously. Never block the UI thread or the startup path on it.

## QScintilla in CMake

QScintilla ships no CMake config file. Find it with `find_path` / `find_library`:
- Windows: `qscintilla2_qt5` (release) / `qscintilla2_qt5d` (debug). Debug builds MUST link the `d` library.
- Pi: `qscintilla2_qt5`, headers under `/usr/include/<arch>/qt5/Qsci` (find via Qt's include dir).

## UI layout

```
+---------------------------------------------------+
| Toolbar: Verify | Upload | Board | Port | Serial  |
+-------------------------------+-------------------+
|          Code editor          |  Serial Monitor   |
|          (QScintilla)         |  (right, hidden   |
|                               |   until opened)   |
+-------------------------------+-------------------+
|              Output / logs pane                   |
+---------------------------------------------------+
```

- The top area is a horizontal QSplitter: editor on the left, Serial Monitor on the RIGHT.
  They share the space; the user can drag the divider.
- Clicking the Serial Monitor button toggles the monitor panel. When hidden, the editor
  takes the full width.
- The Output/logs pane sits below both, in a vertical QSplitter, spanning the full width.
- Serial Monitor panel: received-data view, a text input + Send button (Enter also sends),
  baud rate dropdown, line-ending dropdown (No line ending / Newline / Carriage return / Both).
- Every time the user sends something, add a line to the Output/logs pane: `Sent: <text>`
  (the text as typed, without the added line ending).
- Create the Serial Monitor widget lazily on first open, not at startup.
- Find / Replace (Ctrl+F / Ctrl+H, F3 / Shift+F3): a bar under the editor tabs (`src/FindBar.*`),
  created on first use. It searches the editor text itself (not QScintilla's `findFirst`): Next,
  Previous, wrap-around, "3 of 12" count, match case, whole word, Replace, Replace All (one undo).
- Verify/Upload progress bar: in the status bar, right side. arduino-cli reports no compile
  progress, so compiling fills against the last compile time of the same sketch + board (moving
  stripe the first time); uploading uses the uploader's percentages when it prints them (esptool
  always, avrdude only with verbose output), else the stripe.
- Before uploading, close the serial port if the monitor has it open (otherwise the upload
  fails with "port busy", especially on Windows), then reopen it after the upload finishes.

## Brand colors (ReArm Automation)

All UI colors come from this palette. Define them once (e.g. a `Theme` header) and reuse;
never hard-code colors elsewhere.

| Name          | Hex       | Use                                                        |
|---------------|-----------|------------------------------------------------------------|
| Navy Deep     | `#071A33` | Editor background, output pane background                  |
| Navy          | `#0B2545` | Window, toolbar, panels                                    |
| Navy Light    | `#163A66` | Hover, current-line highlight, borders, splitter handles   |
| Amber         | `#F5A623` | Primary accent: Upload button, focus rings, active items   |
| Sky Blue      | `#2E9BE6` | Secondary accent: Verify button, links, selection          |
| White         | `#FFFFFF` | Main text                                                  |
| Slate         | `#8FA3BF` | Secondary text, line numbers, comments                     |

Dark theme (default):
- Editor: Navy Deep background, white text, Slate line numbers on a Navy Deep margin,
  Navy Light current line, Sky Blue selection (about 40% opacity).
- C++ lexer: keywords Sky Blue, types/preprocessor Amber, numbers Amber,
  strings `#9ED39A` (soft green, for readability), comments Slate italic.
- Output pane: errors `#FF6B6B`, warnings Amber, success `#6FCF97`, `Sent:` lines Sky Blue.
- Buttons: Upload filled Amber with Navy text; Verify filled Sky Blue with white text.

Light theme: white background, Navy text, same Amber and Sky Blue accents. Use Amber only
for fills and borders there, never for text (poor contrast on white).
- Window/toolbar/panels: White mixed with 12% Slate. Borders: White + 45% Slate.
  Secondary text and line numbers: Slate + 45% Navy.
- Editor: White background, Navy text, current line White + 7% Sky Blue,
  selection White + 30% Sky Blue.
- C++ lexer: keywords Sky Blue + 35% Navy, types/preprocessor/numbers dark amber `#8A5A00`,
  strings `#2E7D32`, comments Slate + 35% Navy, italic.
- Output pane: errors `#C62828`, warnings `#8A5A00`, success `#2E7D32`, `Sent:` lines as keywords.
- Buttons: same as dark (Upload Amber fill + Navy text, Verify Sky Blue fill + white text).
- "X + N% Y" means a mix of the two palette colors; `src/Theme.cpp` (`mix()`) is the source of truth.

## Startup-speed rules

- Show the main window first; load everything else lazily (board list, port scan, library index).
- No network calls at startup. Approved exception: one `arduino-cli board list` port scan
  3 seconds after the window appears (it may contact Arduino's servers to identify boards).
- Approved exception: machine activation (see "Machine activation"). With a valid license the only
  call is one background check 5 seconds after the window appears; without one, the waiting window
  calls the server instead of opening the main window.
- Keep dependencies minimal; don't add a new library without asking.

## Workflow

- Keep the project in git. Make small, buildable steps.
- After a change that could affect Linux, test on the Pi:
  push, then `ssh rpi "cd ~/<project> && git pull && cmake --build build"`.
- Ask before using `sudo`, deleting files, or installing anything on either machine.