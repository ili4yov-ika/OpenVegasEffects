#!/usr/bin/env bash
# Настройка macOS для сборки OpenVegas Effects (Homebrew: Qt 6, CMake, Ninja).
# Запуск:
#   bash tools/setup_macos.sh
#   source ~/.openvegas_macos_env.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ENV_FILE="${HOME}/.openvegaseffects_macos_env.sh"

echo "========================================"
echo "OpenVegas Effects — настройка macOS"
echo "========================================"
echo "Проект: $PROJECT_ROOT"
echo

if ! command -v brew >/dev/null 2>&1; then
    echo "[ОШИБКА] Homebrew не найден. Установите: https://brew.sh"
    exit 1
fi

echo "[1/3] Пакеты Homebrew..."
brew update
brew install cmake ninja pkg-config qt@6

QT_PREFIX="$(brew --prefix qt@6)"
echo
echo "[2/3] Запись окружения в $ENV_FILE ..."
# Путь берётся из `brew --prefix qt@6` — он уже правильный и для Apple Silicon
# (/opt/homebrew), и для Intel (/usr/local). Раньше здесь была вторая строка
# с обоими путями вразнобой, и она затирала первую: на машине с двумя
# Homebrew (нативный + под Rosetta) в PATH мог оказаться чужой Qt.
cat >"$ENV_FILE" <<EOF
# OpenVegas macOS (сгенерировано tools/setup_macos.sh)
export PATH="${QT_PREFIX}/bin:\$PATH"
export CMAKE_PREFIX_PATH="${QT_PREFIX}"
export QT_PLUGIN_PATH="${QT_PREFIX}/plugins"
EOF

# shellcheck disable=SC1090
source "$ENV_FILE"

echo "[3/3] Проверка Qt..."
if ! cmake --version >/dev/null 2>&1; then
    echo "[ОШИБКА] cmake недоступен после установки"
    exit 1
fi

if [[ ! -f "${QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
    echo "[ОШИБКА] Qt6 не найден в ${QT_PREFIX}"
    exit 1
fi

echo
echo "Готово. Добавьте в ~/.zshrc:"
echo "  source \"$ENV_FILE\""
echo
echo "Сборка:"
echo "  bash tools/macos_build.sh"
echo "  # или: cmake --preset macos-debug && cmake --build --preset macos-debug"
