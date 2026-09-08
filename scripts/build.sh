#!/usr/bin/env bash
#
# Configure, build, validate and install Glass76 on macOS.
#
# The one script you need to go from a clean checkout to a plug-in a DAW can
# see. It only needs CMake and Xcode on PATH; the VST 3 SDK is located the
# same way CMakeLists.txt locates it (see docs/BUILDING.md), and --fetch-sdk
# will clone it for you.
#
# Usage:
#   ./scripts/build.sh [--config Release|Debug] [--sdk-dir <path>]
#                       [--fetch-sdk] [--clean] [--test] [--validate]
#                       [--install]
#
# Examples:
#   ./scripts/build.sh --validate --test
#   ./scripts/build.sh --fetch-sdk --install

set -euo pipefail

CONFIGURATION="Release"
SDK_DIR=""
FETCH_SDK=0
CLEAN=0
RUN_TEST=0
VALIDATE=0
INSTALL=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --config)     CONFIGURATION="$2"; shift 2 ;;
        --sdk-dir)    SDK_DIR="$2"; shift 2 ;;
        --fetch-sdk)  FETCH_SDK=1; shift ;;
        --clean)      CLEAN=1; shift ;;
        --test)       RUN_TEST=1; shift ;;
        --validate)   VALIDATE=1; shift ;;
        --install)    INSTALL=1; shift ;;
        *) echo "!!  Unknown argument: $1" >&2; exit 1 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build"
BUNDLE_DIR="$BUILD_DIR/VST3/$CONFIGURATION/Glass76.vst3"

step() { echo -e "\033[36m==> $1\033[0m"; }
fail() { echo -e "\033[31m!!  $1\033[0m" >&2; exit 1; }

command -v cmake >/dev/null 2>&1 || fail "cmake is not on PATH. Install CMake 3.25 or newer (brew install cmake)."

if [[ $CLEAN -eq 1 && -d "$BUILD_DIR" ]]; then
    step "Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

#--- Configure ----------------------------------------------------------------
cfg_args=(-S "$ROOT" -B "$BUILD_DIR" -G Xcode)
[[ -n "$SDK_DIR" ]] && cfg_args+=(-Dvst3sdk_SOURCE_DIR="$(cd "$SDK_DIR" && pwd)")
[[ $FETCH_SDK -eq 1 ]] && cfg_args+=(-DGLASS76_FETCH_SDK=ON)
[[ $RUN_TEST -eq 1 ]] && cfg_args+=(-DGLASS76_BUILD_TESTS=ON)
[[ $VALIDATE -eq 1 ]] && cfg_args+=(-DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=ON)

step "Configuring ($CONFIGURATION, Xcode)"
cmake "${cfg_args[@]}"

#--- Build ----------------------------------------------------------------
step "Building Glass76"
cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --target Glass76

[[ -d "$BUNDLE_DIR" ]] || fail "Expected a bundle at $BUNDLE_DIR but it is not there."
step "Built $BUNDLE_DIR"

#--- Validate -------------------------------------------------------------
if [[ $VALIDATE -eq 1 ]]; then
    step "Building the SDK validator"
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --target validator

    validator="$BUILD_DIR/bin/$CONFIGURATION/validator"
    [[ -x "$validator" ]] || validator="$BUILD_DIR/bin/validator"
    step "Running the validator"
    "$validator" "$BUNDLE_DIR"
fi

#--- Offline DSP tests ------------------------------------------------------
if [[ $RUN_TEST -eq 1 ]]; then
    step "Building the offline test host"
    cmake --build "$BUILD_DIR" --config "$CONFIGURATION" --target glass76_test

    exe="$BUILD_DIR/bin/$CONFIGURATION/glass76_test"
    [[ -x "$exe" ]] || exe="$BUILD_DIR/bin/glass76_test"
    step "Running the offline DSP tests"
    "$exe" "$BUNDLE_DIR"
fi

#--- Install ----------------------------------------------------------------
if [[ $INSTALL -eq 1 ]]; then
    dest_dir="$HOME/Library/Audio/Plug-Ins/VST3"
    dest="$dest_dir/Glass76.vst3"

    step "Installing to $dest"
    mkdir -p "$dest_dir"
    rm -rf "$dest"
    cp -R "$BUNDLE_DIR" "$dest"

    echo
    echo -e "\033[33mUnsigned and not notarized -- Gatekeeper may refuse to load it.\033[0m"
    echo -e "\033[33mIf so: xattr -dr com.apple.quarantine \"$dest\"\033[0m"
fi

echo
step "Done."
