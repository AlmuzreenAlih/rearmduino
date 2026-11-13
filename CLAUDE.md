# Project: Lightweight Arduino IDE

A fast-starting desktop Arduino IDE that runs on Windows and Raspberry Pi OS Bullseye.
Startup speed is the top priority: the main window must appear almost instantly, even on the Pi.

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
- No network calls at startup.
- Keep dependencies minimal; don't add a new library without asking.

## Workflow

- Keep the project in git. Make small, buildable steps.
- After a change that could affect Linux, test on the Pi:
  push, then `ssh rpi "cd ~/<project> && git pull && cmake --build build"`.
- Ask before using `sudo`, deleting files, or installing anything on either machine.