#!/bin/bash
#
# Glass76 macOS installer.
#
# This is the whole "app" -- CFBundleExecutable in the surrounding
# Install Glass76.app/Contents/Info.plist points straight at this script, no
# compiled binary involved. Finder launches it like any other app because
# LaunchServices only cares that CFBundleExecutable is an executable file; it
# doesn't have to be Mach-O.
#
# Unlike the Windows installer, there's no "close your DAW first" check here:
# macOS lets you replace a file a process still has open (the process keeps
# using the old inode until it re-reads), so there's nothing to refuse.
set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="$APP_DIR/Contents/Resources/Glass76.vst3"
DEST_DIR="$HOME/Library/Audio/Plug-Ins/VST3"
DEST="$DEST_DIR/Glass76.vst3"

die() {
    osascript -e "display dialog \"$1\" buttons {\"OK\"} default button \"OK\" with icon stop" >/dev/null 2>&1
    exit 1
}

[ -d "$SRC" ] || die "Glass76.vst3 is missing from this installer. Please re-download it."

mkdir -p "$DEST_DIR"
rm -rf "$DEST"
cp -R "$SRC" "$DEST"

# Downloaded-from-a-browser quarantine flag; harmless to clear if it wasn't
# set (this installer itself is unsigned and not notarized, same caveat).
xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

MSG=$'Glass76 installed to\n~/Library/Audio/Plug-Ins/VST3.\n\nRescan plug-ins in your DAW to see it.'
osascript -e "display dialog \"$MSG\" buttons {\"OK\"} default button \"OK\" with icon note" >/dev/null 2>&1
