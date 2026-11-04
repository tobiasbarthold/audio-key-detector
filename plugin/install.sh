#!/bin/sh
# Build Key Detector (VST3, AU, app), sign it ad hoc and install it for the current user:
#   ~/Library/Audio/Plug-Ins/VST3/Key Detector.vst3
#   ~/Library/Audio/Plug-Ins/Components/Key Detector.component
#   ~/Applications/Key Detector.app
# Needs Xcode (command line tools), CMake >= 3.22 and Ninja. Run from anywhere.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD="$ROOT/build/plugin"

cmake -S "$ROOT/plugin" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --target KeyDetector_VST3 KeyDetector_AU KeyDetectorApp

ART="$BUILD/KeyDetector_artefacts/Release"
VST3="$HOME/Library/Audio/Plug-Ins/VST3"
AU="$HOME/Library/Audio/Plug-Ins/Components"
APPS="$HOME/Applications"
mkdir -p "$VST3" "$AU" "$APPS"

install_bundle() {   # source, destination directory
    name=$(basename "$1")
    rm -rf "$2/$name"
    cp -R "$1" "$2/"
    codesign --force --deep --sign - "$2/$name"
    echo "installed $2/$name"
}

install_bundle "$ART/VST3/Key Detector.vst3" "$VST3"
install_bundle "$ART/AU/Key Detector.component" "$AU"
install_bundle "$BUILD/KeyDetectorApp_artefacts/Release/Key Detector.app" "$APPS"

# make the AU known to macOS without logging out
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo "Done. Rescan plug-ins in your DAW (Ableton: Settings > Plug-Ins > Rescan)."
