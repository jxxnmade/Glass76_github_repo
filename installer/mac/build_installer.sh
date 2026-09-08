#!/usr/bin/env bash
#
# Assemble "Install Glass76.app" around a built Glass76.vst3, and zip it for
# distribution. No Xcode project involved -- see install.sh for why a plain
# script can be an app's CFBundleExecutable.
#
# Usage:
#   ./installer/mac/build_installer.sh [--bundle-dir <path>] [--build]
#                                       [--out-dir <path>] [--config Release|Debug]
#
# Examples:
#   ./installer/mac/build_installer.sh --build
#   ./installer/mac/build_installer.sh --bundle-dir build/VST3/Release/Glass76.vst3

set -euo pipefail

CONFIGURATION="Release"
BUNDLE_DIR=""
OUT_DIR=""
DO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle-dir) BUNDLE_DIR="$2"; shift 2 ;;
        --out-dir)    OUT_DIR="$2"; shift 2 ;;
        --config)     CONFIGURATION="$2"; shift 2 ;;
        --build)      DO_BUILD=1; shift ;;
        *) echo "!!  Unknown argument: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
step() { echo -e "\033[36m==> $1\033[0m"; }
fail() { echo -e "\033[31m!!  $1\033[0m" >&2; exit 1; }

#--- Version -------------------------------------------------------------
# Single source of truth is project(... VERSION x.y.z.w) in CMakeLists.txt --
# same line build_installer.ps1 reads for the Windows installer.
RAW_VERSION="$(grep -m1 -E '^\s*VERSION\s+[0-9]+(\.[0-9]+)*' "$ROOT/CMakeLists.txt" | grep -Eo '[0-9]+(\.[0-9]+)*' || true)"
[[ -n "$RAW_VERSION" ]] || fail "Could not read the project version out of CMakeLists.txt."
IFS='.' read -r V1 V2 V3 _ <<< "$RAW_VERSION.0.0.0"
VERSION="$V1.$V2.$V3"
step "Version $VERSION"

#--- Build the plug-in if asked -------------------------------------------
if [[ $DO_BUILD -eq 1 ]]; then
    "$ROOT/scripts/build.sh" --config "$CONFIGURATION"
fi

#--- Locate the bundle -----------------------------------------------------
[[ -n "$BUNDLE_DIR" ]] || BUNDLE_DIR="$ROOT/build/VST3/$CONFIGURATION/Glass76.vst3"
[[ -d "$BUNDLE_DIR/Contents/MacOS" ]] || fail "No plug-in bundle at $BUNDLE_DIR. Build it first (scripts/build.sh) or pass --build."
BUNDLE_DIR="$(cd "$BUNDLE_DIR" && pwd)"
step "Packaging $BUNDLE_DIR"

#--- Assemble the .app -----------------------------------------------------
[[ -n "$OUT_DIR" ]] || OUT_DIR="$ROOT/build/installer"
mkdir -p "$OUT_DIR"
APP="$OUT_DIR/Install Glass76.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

sed "s/@VERSION@/$VERSION/g" "$ROOT/installer/mac/Info.plist.in" > "$APP/Contents/Info.plist"
cp "$ROOT/installer/mac/install.sh" "$APP/Contents/MacOS/install"
chmod +x "$APP/Contents/MacOS/install"
cp -R "$BUNDLE_DIR" "$APP/Contents/Resources/Glass76.vst3"

step "Built $APP"

#--- Zip it ------------------------------------------------------------
# ditto, not zip -- it preserves the bundle's directory structure, resource
# forks and Unix permissions (the install script's +x bit) correctly.
ZIP="$OUT_DIR/Glass76-$VERSION-macos-installer.zip"
rm -f "$ZIP"
( cd "$OUT_DIR" && ditto -c -k --sequesterRsrc --keepParent "Install Glass76.app" "$(basename "$ZIP")" )

step "Installer written: $ZIP"
echo
echo -e "\033[33mUnsigned and not notarized -- Gatekeeper will block a browser download\033[0m"
echo -e "\033[33muntil the user right-clicks it and chooses Open, or clears the flag:\033[0m"
echo -e "\033[33m  xattr -dr com.apple.quarantine \"$OUT_DIR/Install Glass76.app\"\033[0m"
