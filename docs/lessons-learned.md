# Lessons learned building REARMduino

Problems hit while building this app (Qt 5.15 Widgets, QScintilla, QtSerialPort, CMake/Ninja,
arduino-cli, Windows + Raspberry Pi OS Bullseye), what caused them, and the rule that came out of
each. Collected from the whole development conversation (2026-11), including earlier context
summaries. Meant as raw material for a future Claude skill.

## 1. Shell and file-editing tooling (Windows + Git Bash)

| Problem | Cause | Rule |
|---|---|---|
| C++ strings broke (`C2001 newline in constant`, many follow-on syntax errors); text searches said "MISSING" | Git Bash heredocs passed to Python collapse `\\n` / `\"`, even with a quoted `'EOF'` | Use the Edit tool for any string containing `\n`, `\"` or backslash paths. Use Python scripts saved to a file, not heredocs, for bulk edits |
| Files rewritten with CRLF | Python text mode on Windows writes CRLF; the repo uses LF (`core.autocrlf=true`) | Read/write in binary (`'rb'`/`'wb'`) and keep `\n` |
| `git apply` failed | PowerShell `git diff > file` writes UTF-16 | Make patches from Bash |
| `cmake` not found | vcvars called inline in a one-off command | Use a `.cmd` build script that calls vcvars, then cmake |
| A path got mangled in `sed` | Backslashes in a Windows path | Use forward slashes in scripts |
| `windeployqt` deployed the wrong Qt | Anaconda's Qt was earlier on PATH | Put `C:\Qt\5.15.2\msvc2019_64\bin` first on PATH in the build script |
| Release link failed `LNK1104 cannot open file 'myide.exe'` (12 times) | The user had the app open | Never kill the user's process. Ask them to close it, or verify in a scratch build folder |

## 2. Testing GUI apps without disturbing the user

| Problem | Cause | Rule |
|---|---|---|
| Simulated mouse/keyboard went into the user's VNC window | OS-level input simulation | Never simulate OS input. Use UI Automation Invoke, `PrintWindow`, or in-process hooks (`QApplication::sendEvent`, `QWidget::grab`) |
| UIA Toggle didn't trigger a QAction | Wrong pattern | Use the UIA Invoke pattern |
| A screenshot script wiped the user's registry settings | `New-Item -Force` on an existing key recreates it empty | Never `New-Item -Force` on registry keys or files; check `Test-Path` first |
| Test runs polluted the user's real settings (recent list got 3 scratch copies, a scan with the board unplugged cleared the saved port) | Hooked builds used the real registry QSettings | In test hooks: `QSettings::setDefaultFormat(IniFormat)` + `setPath(...)` to a scratch folder before anything reads settings |
| A test sent real bytes to the user's Mega (which drives motors/relays) | Saved port setting was used by the test | Ask before any test that opens a real serial port; avoid motion commands |
| Windows GUI app stderr not captured | GUI subsystem | Hooks log to a file (`$MYIDE_TEST.log`) |
| Offscreen runs failed | Plugin not in the deploy folder | Set `QT_QPA_PLATFORM_PLUGIN_PATH`. Offscreen also hit a Debug assertion the real platform didn't; confirm on the real platform |
| Hook didn't compile (`auto *tabs = findChildren...`) | `findChildren` returns a `QList`, not a pointer | Use `const auto tabs = ...` |
| Hook found no action named "Light" | Real text was "Light Theme" | Look actions up by objectName (`registerAction` ids), not text |
| A stale test JSON opened a modal that blocked the test | Leftover file from an earlier run | Reset fixture files before each run |
| "Did you actually test on the Pi?" | Early tests were Windows-only | Test on the Pi after Linux-relevant changes, and say clearly what was and wasn't tested |
| Method that worked: temporary hook in `main.cpp` inserted by a Python script, build in a scratch folder, restore `main.cpp` with `git checkout` right after, screenshots via `grab()`. Never commit hooks | | |

## 3. Qt 5.15 Widgets

| Problem | Cause | Rule |
|---|---|---|
| App-wide stylesheet cost ~75 ms at startup on the Pi | QSS restyles every widget | Apply QSS only to the widgets that need it (toolbar, menus, panels), never app-wide or to editors |
| Bottom tab labels clipped | QSS `font-weight` on `::tab` isn't counted in the tab size | Set a bold `QFont` (DemiBold) on the widget instead |
| Focus ring stuck on a combo | Disabling the focused widget moved focus | `Qt::NoFocus` on toolbar combos plus a focus reset when re-enabling |
| Mojibake (bullet character) in menus | MSVC read UTF-8 sources as ANSI | Compile with `/utf-8`; use `QChar(0x2022)` for special characters |
| `C2027 use of undefined type 'SerialMonitor'` (8 times) | Forward declaration only, then member access | Include the header in every .cpp that dereferences the type |
| Combo popup hid the in-combo spinner | The popup covers the combo | Custom `QListView` with `setViewportMargins(0,0,0,h)`: QComboBox sizes the popup to include viewport margins, so a band appears at the bottom for an overlay widget |
| Window title suffix | Avoid `setApplicationDisplayName`: on some platforms Qt appends it to window titles automatically | Build the title string yourself |
| Window/exe icon | Window icon: `QApplication::setWindowIcon` with 16 to 256 px PNGs in the qrc. Exe icon on Windows: `.rc` with `IDI_ICON1 ICON "x.ico"` added via `target_sources` only on WIN32 | |
| Renaming the program file | `set_target_properties(<target> PROPERTIES OUTPUT_NAME NewName)` keeps the target name; `$<TARGET_FILE:...>` follows. Never rename QSettings organization/application names: that resets users' settings | |

## 4. QScintilla

- One lexer per editor; subclass `QsciLexerCPP` for Arduino keywords (set 1 keywords, set 2 types).
- Clear conflicting `standardCommands()` key bindings for keys used by menu shortcuts.
- Editors keep their creation-time palette: after a runtime theme switch, scrollbars stayed dark.
  Re-apply palette/styles to existing editors on theme change.
- `SCI_SETSCROLLWIDTHTRACKING`, `SCI_SETCARETLINEVISIBLEALWAYS` are useful defaults.
- Windows: no CMake config; find `qscintilla2_qt5` / `qscintilla2_qt5d` with `find_library`. Debug must link the `d` lib.
- `Qt5PrintSupport.dll` missing at runtime: QScintilla needs it but `windeployqt` only scans the exe's
  own imports. Fix: `windeployqt --printsupport` and copy the qscintilla DLL in the deploy target.

## 5. arduino-cli

| Problem | Cause | Rule |
|---|---|---|
| Board list took 11 s / 15 MB | `board listall --json` repeats each platform's board list per board | Use `core list --json` (`platforms[].releases[installed].boards`): ~2 s / 150 KB |
| ANSI color codes in output (1.5.1 only) | Version difference between the machines (1.4.1 vs 1.5.1) | Strip ANSI codes; never rely on one version's behaviour |
| arduino-cli downloaded `library_index` in a portable copy | It downloads on any command when the index file is missing | Bundle `package_index.json`, `library_index.json` and their `.sig` files |
| `board list` can contact Arduino's servers | Board identification for unknown VID/PID | Treat it as a network call (here: one approved scan 3 s after startup, then on demand) |
| Compile failed in a deep folder: `avr-g++: error: device-specs/specs-atmega2560: No such file` (file existed) | Windows path limit: the toolchain needs ~135 chars inside the folder | Warn when the portable folder path is over ~100 characters; test with `subst X:` |
| Pi: sketch folder name ≠ .ino name created a clashing template | arduino-cli requires folder name = main .ino name | Create the main file empty with a note instead of a template |
| `millisDelay@1.3` not in the library index | It ships inside SafeString | Copy the library folder (with permission) |
| Portable mode | Point arduino-cli at bundled folders with env vars `ARDUINO_DIRECTORIES_DATA/USER/DOWNLOADS`, `ARDUINO_UPDATER_ENABLE_NOTIFICATION=false` | |

## 6. Serial ports and boards

| Problem | Cause | Rule |
|---|---|---|
| Selected port stayed in the list after unplugging; uploads failed confusingly | Port list only refreshed by arduino-cli scans | Poll `QSerialPortInfo::availablePorts()` every second (local, no arduino-cli) and show (PLUGGED)/(DISCONNECTED) |
| Upload "unable to open port COM3" | The official Arduino IDE's `serial-monitor.exe` held the port, or the board was unplugged | Close our port before uploading and reopen after; tell the user which program may hold it; never kill other processes |
| COM3 "Access is denied" once | Port briefly used by something else | Retry before calling it stuck |
| Sketch didn't start after the monitor opened | Opening the port resets the Arduino via DTR; bytes sent during the bootloader stop the sketch | The press that opens the port only opens it and sends nothing |
| Port "busy"/stuck after motor/relay noise | Board's USB-serial chip or the driver hung | Offer Restart USB Device: Windows `pnputil /restart-device <instance id>` elevated (UAC); Linux `USBDEVFS_RESET` ioctl on `/dev/bus/usb/BBB/DDD` (needs a udev rule giving the dialout group write access) |
| Restarting USB isn't a power cycle | Neither method cuts VBUS; boards with external power are never power-cycled by unplugging USB either | Explain the difference; on the Pi 4 `uhubctl` can cut power to all ports together |
| A different port name after a restart/replug | Can signal an electrical problem | Don't hide it with stable names (`/dev/serial/by-id`); log it clearly |
| COM20/COM21 in the list | Bluetooth serial ports | Sort ports with a recognised board first |

## 7. Windows USB drivers and the portable version

- Detect driverless boards with SetupAPI + `CM_Get_DevNode_Status` (problem codes
  `CM_PROB_FAILED_INSTALL`, `CM_PROB_NOT_CONFIGURED`). Only offer installs for boards with no driver,
  so a newer installed driver is never replaced.
- Install with `pnputil /add-driver "<dir>\*.inf" /subdirs /install`, elevated via
  `ShellExecuteEx "runas"`, waited on with `QWinEventNotifier`.
- The Arduino IDE 2.x installer contains no drivers; the drivers are in the AVR core folder
  (`packages/arduino/hardware/avr/<ver>/drivers`). Genuine boards use Windows' built-in usbser.
- CH340 (clones) is only bundled if given to the builder; otherwise the user must install CH341SER.EXE
  (Windows Update often installs one when online).
- A portable copy also needs the VC++ runtime DLLs (`vcruntime140.dll`, `vcruntime140_1.dll`,
  `msvcp140.dll`, `msvcp140_1.dll`) copied next to the exe. Never build it from a Debug build.
- Zip/unzip with Windows' built-in `tar -a -c -f` / `tar -xf`.

## 8. Raspberry Pi (Bullseye, aarch64)

- Always `git pull && cmake --build build` on the Pi after Linux-relevant changes.
- The default sketchbook on Linux is `~/Arduino` (not Documents).
- udev rule for USB resets: `SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="2341", GROUP="dialout", MODE="0664"`
  (plus 2a03, 1a86, 0403:6001); `udevadm control --reload-rules && udevadm trigger --subsystem-match=usb`;
  check with `udevadm test`. Needs sudo: ask first.
- Startup benchmark: measure time to window visible with xdotool, comparing builds in git worktrees.

## 9. Working with this user

- Ask before sudo, deleting, installing, pushing to the live backend, or opening a real serial port.
- Show a config (e.g. the udev rule) before installing it when asked.
- Small, buildable commits; push and build on the Pi after each.
- Say plainly what was tested on hardware and what wasn't.
- The user tests on a real Arduino Mega (COM3) that drives motors/relays: treat serial output as real-world actions.
