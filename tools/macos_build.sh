#!/usr/bin/env bash
# Builds OpenVegas Effects on macOS (CMake presets + Ninja).
#
# Usage:
#   bash tools/macos_build.sh              # Debug
#   bash tools/macos_build.sh release      # Release
#   bash tools/macos_build.sh debug test   # Debug + ctest (when there are tests)
#   bash tools/macos_build.sh release deploy  # Release + macdeployqt

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="${HOME}/.openvegaseffects_macos_env.sh"

if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
fi

BUILD_TYPE="debug"
RUN_TEST=0
DEPLOY=0

for arg in "$@"; do
    case "$arg" in
        release|Release) BUILD_TYPE="release" ;;
        debug|Debug) BUILD_TYPE="debug" ;;
        test) RUN_TEST=1 ;;
        deploy) DEPLOY=1 ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [debug|release] [test] [deploy]"
            exit 1
            ;;
    esac
done

# build/macOS_LLVM-<arch>-<configuration>, for the Mac's own architecture
# (CMakePresets.json: macos-* for Apple silicon, macos-x64-* for Intel).
if [[ "$(uname -m)" == "arm64" ]]; then
    PRESET="macos-${BUILD_TYPE}"
    ARCH_NAME="arm64"
else
    PRESET="macos-x64-${BUILD_TYPE}"
    ARCH_NAME="x64"
fi
if [[ "$BUILD_TYPE" == "release" ]]; then CONFIG_NAME="Release"; else CONFIG_NAME="Debug"; fi
BUILD_DIR="${PROJECT_ROOT}/build/macOS_LLVM-${ARCH_NAME}-${CONFIG_NAME}"

cd "$PROJECT_ROOT"

echo "=== OpenVegas Effects macOS: preset ${PRESET} ==="
VLC_CMAKE_ARGS=()
if [[ -n "${OPENVEGAS_VLC_RUNTIME_DIR:-}" ]]; then
    VLC_CMAKE_ARGS+=("-DOPENVEGAS_VLC_RUNTIME_DIR=${OPENVEGAS_VLC_RUNTIME_DIR}")
fi
cmake --preset "${PRESET}" "${VLC_CMAKE_ARGS[@]}"
cmake --build --preset "${PRESET}" --parallel

APP_BIN="${BUILD_DIR}/OpenVegasEffects"
if [[ ! -x "$APP_BIN" ]]; then
    echo "[ERROR] Executable not found: $APP_BIN"
    exit 1
fi

echo "Built: $APP_BIN"

if [[ "$RUN_TEST" -eq 1 ]]; then
    echo "=== CTest (QT_QPA_PLATFORM=offscreen) ==="
    QT_QPA_PLATFORM=offscreen ctest --test-dir "$BUILD_DIR" --output-on-failure || true
fi

if [[ "$DEPLOY" -eq 1 ]]; then
    MACDEPLOYQT="$(command -v macdeployqt || true)"
    if [[ -z "$MACDEPLOYQT" && -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        MACDEPLOYQT="${CMAKE_PREFIX_PATH}/bin/macdeployqt"
    fi
    if [[ ! -x "$MACDEPLOYQT" ]]; then
        echo "[WARN] macdeployqt not found - skipping deploy"
    else
        APP_DIR="${BUILD_DIR}/OpenVegasEffects.app"
        mkdir -p "${APP_DIR}/Contents/MacOS"
        cp "$APP_BIN" "${APP_DIR}/Contents/MacOS/OpenVegasEffects"

        # The bundle is put together here by hand: CMakeLists.txt does not set
        # MACOSX_BUNDLE, so the target links as a plain executable. Without an
        # Info.plist such a .app does not start from Finder and macdeployqt
        # complains, so a minimal plist is written here. The proper fix is
        # MACOSX_BUNDLE in CMakeLists.txt and dropping this block.
        cat >"${APP_DIR}/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>          <string>OpenVegasEffects</string>
    <key>CFBundleIdentifier</key>          <string>org.openvegas.OpenVegasEffects</string>
    <key>CFBundleName</key>                <string>OpenVegas Effects</string>
    <key>CFBundlePackageType</key>         <string>APPL</string>
    <key>CFBundleShortVersionString</key>  <string>0.1.0</string>
    <key>CFBundleVersion</key>             <string>0.1.0</string>
    <key>LSMinimumSystemVersion</key>      <string>11.0</string>
    <key>NSHighResolutionCapable</key>     <true/>
    <key>NSMicrophoneUsageDescription</key><string>Record voiceover audio for your project.</string>
</dict>
</plist>
PLIST

        if [[ -d "${BUILD_DIR}/vlc" ]]; then
            cp -R "${BUILD_DIR}/vlc" "${APP_DIR}/Contents/MacOS/"
        fi
        "$MACDEPLOYQT" "$APP_DIR"
        echo "Deployed: $APP_DIR"
    fi
fi
