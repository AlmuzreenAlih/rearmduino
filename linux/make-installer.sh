#!/bin/bash
# Build an OFFLINE installer for REARMduino + arduino-cli + Arduino IDE 1.8.19, for a
# fresh Raspberry Pi OS Bullseye 64-bit. Run this on the Pi that already has everything
# working; it copies what is installed here, so nothing has to be downloaded later.
#
# Usage (on the Pi):
#   linux/make-installer.sh                 # result in ~/REARMduino-installer(.tar)
#   linux/make-installer.sh /media/rpi/USB  # result in that folder instead
#   NO_IDE=1 linux/make-installer.sh        # leave Arduino IDE 1.8.19 out (saves ~200 MB)
#   REPO=~/myide linux/make-installer.sh    # source tree to build (default: this script's repo)
#
# Needs internet ONCE, here, to fetch the Qt runtime .deb files (no sudo). Everything
# else comes from this Pi's disk: the app is built from the repo, arduino-cli, the boards
# in ~/.arduino15, the libraries in ~/Arduino/libraries and the IDE in ~/arduino-1.8.19.
#
# On the new Pi: copy the folder (or extract the .tar), then run  ./install.sh
set -euo pipefail

REPO="${REPO:-$(cd "$(dirname "$0")/.." && pwd)}"
DEST="${1:-$HOME}"
NO_IDE="${NO_IDE:-0}"
IDE_VERSION="1.8.19"
IDE_DIR="$HOME/arduino-$IDE_VERSION"
OUT="$DEST/REARMduino-installer"
# Qt libraries the app needs at run time (their dependencies are added automatically).
RUNTIME_PKGS="libqt5widgets5 libqt5svg5 libqt5serialport5 libqt5printsupport5 libqscintilla2-qt5-15"

say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }
die() { printf '\033[1;31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

[ "$(uname -m)" = "aarch64" ] || die "run this on the 64-bit Pi (found $(uname -m))"
grep -q 'VERSION_CODENAME=bullseye' /etc/os-release || die "run this on Raspberry Pi OS Bullseye"
[ -f "$REPO/CMakeLists.txt" ] || die "no source tree in $REPO"
CLI="$(command -v arduino-cli)" || die "arduino-cli is not installed here"
[ -d "$HOME/.arduino15/packages" ] || die "no boards in ~/.arduino15 (run setup-arduino.sh first)"
[ "$NO_IDE" = "1" ] || [ -x "$IDE_DIR/arduino" ] || die "$IDE_DIR not found (or use NO_IDE=1)"
[ -d "$DEST" ] || die "folder $DEST does not exist"

rm -rf "$OUT"
mkdir -p "$OUT/app" "$OUT/debs"

# ---------- 1. The app ----------
say "Building REARMduino"
[ -d "$REPO/build" ] || cmake -S "$REPO" -B "$REPO/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$REPO/build"
install -m 755 "$REPO/build/REARMduino" "$OUT/app/REARMduino"
strip "$OUT/app/REARMduino"
cp "$REPO/resources/app/rearmduino-256.png" "$OUT/app/rearmduino.png"
cp "$REPO/linux/99-myide-usb-reset.rules" "$OUT/app/"
cp "$(dirname "$(readlink -f "$0")")/install-offline.sh" "$OUT/install.sh"
chmod +x "$OUT/install.sh"

# ---------- 2. Qt runtime packages with all their dependencies ----------
say "Downloading the Qt runtime packages (.deb)"
cd "$OUT/debs"
all="$(apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts --no-breaks \
        --no-replaces --no-enhances $RUNTIME_PKGS | grep '^\w' | sort -u)"
native=""; foreign=""
for p in $all; do   # drop virtual package names (they have no file)
    # (no "grep -q" here: it quits early, and with pipefail that reads as a failure)
    apt-cache show "$p" 2>/dev/null | grep '^Filename:' >/dev/null || continue
    case "$p" in *:*) foreign="$foreign $p" ;; *) native="$native $p" ;; esac
done
apt-get download $native
# 64-bit Pi OS also carries 32-bit (armhf) copies of some libraries, and both copies must
# have the same version. They are downloaded separately: asked for together with the
# 64-bit names, apt fetches only one of the two.
[ -z "$foreign" ] || apt-get download $foreign
dpkg-scanpackages --multiversion . > Packages 2>/dev/null
echo "$(ls *.deb | wc -l) packages, $(du -sh . | cut -f1)"
cd - >/dev/null

# Self-check: would these .deb files alone be enough on a Pi with NO packages at all?
# (apt simulation against an empty package database; installs nothing.)
CHK="$(mktemp -d)"
trap 'rm -rf "$CHK"' EXIT
ln -s "$OUT/debs" "$CHK/repo"
echo "deb [trusted=yes] file://$CHK/repo ./" > "$CHK/sources.list"
mkdir -p "$CHK/lists" "$CHK/cache/archives/partial"
: > "$CHK/status"
CHK_OPTS=(-o Dir::Etc::sourcelist="$CHK/sources.list" -o Dir::Etc::sourceparts=-
          -o Dir::State::lists="$CHK/lists" -o APT::Get::List-Cleanup=0
          -o Dir::Cache="$CHK/cache" -o Debug::NoLocking=1)
apt-get "${CHK_OPTS[@]}" update >/dev/null 2>&1
if sim="$(apt-get "${CHK_OPTS[@]}" -o Dir::State::status="$CHK/status" -s install \
            --no-install-recommends $RUNTIME_PKGS 2>&1)"; then
    echo "Self-check passed: the packages are complete ($(echo "$sim" | grep -c '^Inst') would install on an empty system)"
else
    echo "$sim" | grep -v '^Inst\|^Conf' | tail -30
    die "the downloaded packages are not complete (see above)"
fi

# ---------- 3. arduino-cli, boards, libraries ----------
say "Copying arduino-cli, boards and libraries"
install -m 755 "$(readlink -f "$CLI")" "$OUT/arduino-cli"
# staging/ (downloaded archives), tmp/ and inventory.yaml (this Pi's own ID) are left out.
tar -czf "$OUT/arduino15.tar.gz" -C "$HOME/.arduino15" \
    --exclude=./staging --exclude=./tmp --exclude=./inventory.yaml --exclude=./preferences.txt .
if [ -d "$HOME/Arduino/libraries" ]; then
    tar -czf "$OUT/libraries.tar.gz" -C "$HOME/Arduino" libraries
fi

# ---------- 4. Arduino IDE 1.8.19 ----------
if [ "$NO_IDE" = "1" ]; then
    say "Leaving the Arduino IDE out (NO_IDE=1)"
else
    say "Packing Arduino IDE $IDE_VERSION (takes a few minutes)"
    tar -czf "$OUT/arduino-$IDE_VERSION.tar.gz" -C "$HOME" "arduino-$IDE_VERSION"
fi

# ---------- 5. What is inside ----------
{
    echo "REARMduino offline installer for Raspberry Pi OS Bullseye 64-bit"
    echo "Made on $(date '+%Y-%m-%d %H:%M') from $(git -C "$REPO" log -1 --format='%h %s' 2>/dev/null || echo 'unknown commit')"
    echo
    echo "Install on the new Pi:  ./install.sh      (NO_IDE=1 ./install.sh skips the Arduino IDE)"
    echo
    echo "arduino-cli: $("$OUT/arduino-cli" version)"
    [ "$NO_IDE" = "1" ] || echo "Arduino IDE: $IDE_VERSION"
    echo
    echo "Boards:"
    "$CLI" core list
    echo
    echo "Libraries:"
    "$CLI" lib list
} > "$OUT/README.txt"

say "Making the .tar"
tar -cf "$OUT.tar" -C "$DEST" "REARMduino-installer"

say "Done"
cat "$OUT/README.txt"
echo
du -sh "$OUT" "$OUT.tar"
