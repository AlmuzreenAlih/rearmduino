#!/bin/bash
# Offline installer for a fresh Raspberry Pi OS Bullseye 64-bit. No internet needed.
# This file is copied into the bundle as install.sh by linux/make-installer.sh.
#
# Installs, all from this folder:
#   - REARMduino                      -> ~/REARMduino  (menu entry + desktop icon)
#   - the Qt libraries it needs       -> apt, from the .deb files in debs/  (sudo)
#   - arduino-cli                     -> ~/.local/bin/arduino-cli
#   - boards and tools                -> ~/.arduino15
#   - libraries                       -> ~/Arduino/libraries
#   - Arduino IDE 1.8.19              -> ~/arduino-1.8.19   (skip with NO_IDE=1)
#   - serial access (dialout group) and the USB reset rule  (sudo)
#
# Usage:   ./install.sh        or        NO_IDE=1 ./install.sh
# Safe to run again: finished steps are skipped, existing files are kept.
# NO_SUDO=1 skips every step that needs sudo (used to test the installer).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NO_IDE="${NO_IDE:-0}"
NO_SUDO="${NO_SUDO:-0}"
IDE_VERSION="1.8.19"
IDE_DIR="$HOME/arduino-$IDE_VERSION"
APP_DIR="$HOME/REARMduino"
BIN="$HOME/.local/bin"
RUNTIME_PKGS="libqt5widgets5 libqt5svg5 libqt5serialport5 libqt5printsupport5 libqscintilla2-qt5-15"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
export PATH="$BIN:$PATH"

say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }
relogin=0

# ---------- 0. Right kind of Pi? ----------
[ "$(uname -m)" = "aarch64" ] || die "this installer is for 64-bit Raspberry Pi OS (found $(uname -m))"
grep -q 'VERSION_CODENAME=bullseye' /etc/os-release || die "this installer is for Raspberry Pi OS Bullseye"
[ "$(id -u)" != "0" ] || die "run this as the normal user, not with sudo (it asks for sudo itself)"
if dpkg -s arduino >/dev/null 2>&1; then
    echo "WARNING: the apt 'arduino' package is installed. It uses its own toolchain and"
    echo "         breaks the CLI/IDE sync. Remove it: sudo apt remove arduino arduino-core"
fi

# ---------- 1. Qt libraries (from debs/, no download) ----------
missing=""
for p in $RUNTIME_PKGS; do
    [ "$(dpkg-query -W -f='${Status}' "$p" 2>/dev/null)" = "install ok installed" ] || missing="$missing $p"
done
if [ -z "$missing" ]; then
    say "Qt libraries already installed"
elif [ "$NO_SUDO" = "1" ]; then
    say "Skipping the Qt libraries (NO_SUDO=1); missing:$missing"
else
    say "Installing the Qt libraries:$missing"
    # apt is pointed at debs/ only, with its own list/cache folders, so the system's
    # package lists stay untouched. The symlink avoids spaces in a USB stick's path.
    ln -s "$HERE/debs" "$WORK/repo"
    echo "deb [trusted=yes] file://$WORK/repo ./" > "$WORK/sources.list"
    mkdir -p "$WORK/lists"
    APT_OPTS=(-o Dir::Etc::sourcelist="$WORK/sources.list" -o Dir::Etc::sourceparts=-
              -o Dir::State::lists="$WORK/lists" -o APT::Get::List-Cleanup=0
              -o Dir::Cache::pkgcache="$WORK/pkgcache.bin" -o Dir::Cache::srcpkgcache="$WORK/srcpkgcache.bin")
    sudo apt-get "${APT_OPTS[@]}" update
    sudo apt-get "${APT_OPTS[@]}" install -y --no-install-recommends $missing
fi

# ---------- 2. arduino-cli ----------
mkdir -p "$BIN"
want="$("$HERE/arduino-cli" version)"
if [ -x "$BIN/arduino-cli" ] && [ "$("$BIN/arduino-cli" version)" = "$want" ]; then
    say "arduino-cli already installed"
else
    say "Installing arduino-cli"
    install -m 755 "$HERE/arduino-cli" "$BIN/arduino-cli"
fi
arduino-cli version

# ---------- 3. Boards and tools (~/.arduino15) ----------
if [ -d "$HOME/.arduino15/packages" ]; then
    say "Boards: ~/.arduino15 already exists, kept as it is"
else
    say "Installing boards and tools into ~/.arduino15"
    mkdir -p "$HOME/.arduino15"
    tar -xzf "$HERE/arduino15.tar.gz" -C "$HOME/.arduino15"
fi
arduino-cli core list

# ---------- 4. Libraries (~/Arduino/libraries) ----------
if [ -f "$HERE/libraries.tar.gz" ]; then
    say "Installing libraries (existing ones are kept)"
    mkdir -p "$HOME/Arduino"
    tar -xzf "$HERE/libraries.tar.gz" -C "$HOME/Arduino" --skip-old-files
    ls "$HOME/Arduino/libraries"
fi

# ---------- 5. Arduino IDE 1.8.19 ----------
if [ "$NO_IDE" = "1" ]; then
    say "Skipping the Arduino IDE (NO_IDE=1)"
elif [ ! -f "$HERE/arduino-$IDE_VERSION.tar.gz" ]; then
    say "This bundle has no Arduino IDE (it was made with NO_IDE=1)"
    NO_IDE=1
elif [ -x "$IDE_DIR/arduino" ]; then
    say "Arduino IDE $IDE_VERSION already in $IDE_DIR"
else
    say "Installing Arduino IDE $IDE_VERSION"
    mkdir -p "$HOME/.local/share/icons/hicolor" "$HOME/.local/share/applications"   # its install.sh expects them
    tar -xzf "$HERE/arduino-$IDE_VERSION.tar.gz" -C "$HOME"
    # Menu entry + desktop icon (user level). Its /usr/local/bin symlink needs sudo; ignore that.
    (cd "$IDE_DIR" && ./install.sh) || true
fi
[ "$NO_IDE" = "1" ] || ln -sf "$IDE_DIR/arduino" "$BIN/arduino"

# ---------- 6. REARMduino ----------
say "Installing REARMduino into $APP_DIR"
mkdir -p "$APP_DIR" "$HOME/.local/share/applications"
install -m 755 "$HERE/app/REARMduino" "$APP_DIR/REARMduino"
cp "$HERE/app/rearmduino.png" "$APP_DIR/rearmduino.png"
ln -sf "$APP_DIR/REARMduino" "$BIN/REARMduino"
cat > "$HOME/.local/share/applications/rearmduino.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=REARMduino
Comment=Lightweight Arduino IDE
Exec=$APP_DIR/REARMduino %f
Icon=$APP_DIR/rearmduino.png
Terminal=false
Categories=Development;IDE;Electronics;
StartupWMClass=REARMduino
EOF
if [ -d "$HOME/Desktop" ]; then
    cp "$HOME/.local/share/applications/rearmduino.desktop" "$HOME/Desktop/rearmduino.desktop"
    chmod +x "$HOME/Desktop/rearmduino.desktop"
fi

# ---------- 7. Serial port access + USB reset rule ----------
groups_now="$(id -nG "$USER")"
if grep -qw dialout <<<"$groups_now"; then
    say "$USER is in the dialout group"
elif [ "$NO_SUDO" = "1" ]; then
    say "Skipping dialout (NO_SUDO=1)"
else
    say "Adding $USER to dialout"
    sudo usermod -aG dialout "$USER" && relogin=1 || echo "Could not add to dialout; run: sudo usermod -aG dialout $USER"
fi
RULE="99-myide-usb-reset.rules"
if cmp -s "$HERE/app/$RULE" "/etc/udev/rules.d/$RULE"; then
    say "USB reset rule already installed"
elif [ "$NO_SUDO" = "1" ]; then
    say "Skipping the USB reset rule (NO_SUDO=1)"
else
    say "Installing the USB reset rule"
    sudo install -m 644 "$HERE/app/$RULE" "/etc/udev/rules.d/$RULE"
    sudo udevadm control --reload-rules
    sudo udevadm trigger --subsystem-match=usb
fi

# ---------- 8. PATH ----------
if ! grep -qs '.local/bin' "$HOME/.profile" "$HOME/.bashrc"; then
    echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc"
    echo "Added ~/.local/bin to PATH in ~/.bashrc (open a new terminal)"
fi

# ---------- 9. Check: compile offline; CLI and IDE use the same core ----------
cores="$(arduino-cli core list)"
if grep -q '^arduino:avr' <<<"$cores"; then
    say "Test compile (Uno blink) with arduino-cli"
    mkdir -p "$WORK/SyncTest"
    cat > "$WORK/SyncTest/SyncTest.ino" <<'EOF'
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH); delay(500); digitalWrite(LED_BUILTIN, LOW); delay(500);
}
EOF
    cli_out="$(arduino-cli compile --fqbn arduino:avr:uno -v "$WORK/SyncTest" 2>&1)" || { echo "$cli_out"; die "CLI compile failed"; }
    cli_core="$(echo "$cli_out" | grep -m1 -oE "$HOME/.arduino15/packages/arduino/hardware/avr/[^/]+" || true)"
    echo "$cli_out" | grep -m1 'Sketch uses' || true
    echo "core: $cli_core"

    if [ "$NO_IDE" != "1" ] && [ -n "${DISPLAY:-}" ]; then
        say "Test compile with Arduino IDE $IDE_VERSION"
        ide_out="$("$IDE_DIR/arduino" --verify --board arduino:avr:uno --verbose-build --pref build.path="$WORK/ide-build" "$WORK/SyncTest/SyncTest.ino" 2>&1)" || { echo "$ide_out" | tail -20; die "IDE compile failed"; }
        ide_core="$(echo "$ide_out" | grep -m1 -oE "$HOME/.arduino15/packages/arduino/hardware/avr/[^/]+" || true)"
        echo "$ide_out" | grep -m1 'Sketch uses' || true
        echo "core: $ide_core"
        if [ -n "$cli_core" ] && [ "$cli_core" = "$ide_core" ]; then
            say "IN SYNC: CLI and IDE use the same core and toolchain"
        else
            die "NOT in sync: CLI core '$cli_core' vs IDE core '$ide_core'"
        fi
    elif [ "$NO_IDE" != "1" ]; then
        echo "No DISPLAY (SSH?): skipped the IDE test compile. Run again from the desktop to check it."
    fi
fi

say "Done"
cat <<EOF
  REARMduino  : $APP_DIR/REARMduino  (desktop icon, menu: Programming > REARMduino)
  arduino-cli : $BIN/arduino-cli
  Arduino IDE : $( [ "$NO_IDE" = "1" ] && echo "not installed" || echo "$IDE_DIR  (menu: Programming > Arduino IDE)" )
  Shared      : ~/.arduino15 (boards/tools), ~/Arduino (sketches/libraries)
EOF
if [ "$relogin" = "1" ]; then
    echo
    echo "  Log out and in again (or reboot) before uploading: the dialout group needs it."
fi
