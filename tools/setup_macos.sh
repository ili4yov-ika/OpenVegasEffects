#!/usr/bin/env bash
# Prepares macOS for building OpenVegas Effects (Homebrew: Qt 6, CMake, Ninja).
# Run:
#   bash tools/setup_macos.sh
#   source ~/.openvegaseffects_macos_env.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="${HOME}/.openvegaseffects_macos_env.sh"

echo "========================================"
echo "OpenVegas Effects - macOS setup"
echo "========================================"
echo "Project: $PROJECT_ROOT"
echo

if ! command -v brew >/dev/null 2>&1; then
    echo "[ERROR] Homebrew not found. Install it: https://brew.sh"
    exit 1
fi

echo "[1/3] Homebrew packages..."
brew update
brew install cmake ninja pkg-config qt@6

QT_PREFIX="$(brew --prefix qt@6)"
echo
echo "[2/3] Writing the environment to $ENV_FILE ..."
# The path comes from `brew --prefix qt@6`, which is right for Apple Silicon
# (/opt/homebrew) and for Intel (/usr/local) alike. A second line here once
# listed both paths and overrode the first: on a machine with two Homebrews
# (native and under Rosetta) the other one's Qt could end up in PATH.
cat >"$ENV_FILE" <<EOF
# OpenVegas Effects on macOS (written by tools/setup_macos.sh)
export PATH="${QT_PREFIX}/bin:\$PATH"
export CMAKE_PREFIX_PATH="${QT_PREFIX}"
export QT_PLUGIN_PATH="${QT_PREFIX}/plugins"
EOF

# shellcheck disable=SC1090
source "$ENV_FILE"

echo "[3/3] Checking Qt..."
if ! cmake --version >/dev/null 2>&1; then
    echo "[ERROR] cmake is not available after the installation"
    exit 1
fi

if [[ ! -f "${QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "[ERROR] Qt 6 not found in ${QT_PREFIX}"
    exit 1
fi

echo
echo "Done. Add to ~/.zshrc:"
echo "  source \"$ENV_FILE\""
echo
echo "Build:"
echo "  bash tools/macos_build.sh"
echo "  # or: cmake --preset macos-debug && cmake --build --preset macos-debug"
