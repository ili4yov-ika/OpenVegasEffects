#!/bin/bash
# Скрипт сборки Debian/Ubuntu пакета (.deb) для OpenVegas Effects
# Требуется: CMake, Qt6, dpkg-buildpackage или debuild
#
# Сборку выполняет сам dpkg-buildpackage через debian/rules (dh_auto_configure /
# dh_auto_build / dh_auto_install) — конфигурировать и собирать проект отдельно
# перед этим не нужно, это была бы та же работа второй раз.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEBIAN_SRC="$SCRIPT_DIR/debian"
PACKAGE_NAME="openvegaseffects"

# Версия — из debian/changelog, чтобы не разъезжалась с ним.
VERSION="$(sed -n '1s/^[^(]*(\([^)]*\)).*/\1/p' "$DEBIAN_SRC/changelog")"
: "${VERSION:?не удалось прочитать версию из $DEBIAN_SRC/changelog}"

echo "========================================"
echo "OpenVegas Effects - Debian Package Builder"
echo "========================================"
echo "Версия: $VERSION"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ОШИБКА] CMake не найден!"; exit 1; }
command -v dpkg-buildpackage >/dev/null 2>&1 || { echo "[ОШИБКА] dpkg-buildpackage не найден! Установите: sudo apt-get install build-essential devscripts debhelper"; exit 1; }

# debian/ обязан лежать в корне проекта — dpkg-buildpackage ищет его только там.
# Копию убираем на выходе (в т.ч. при ошибке), иначе рабочее дерево остаётся
# грязным и следующий запуск молча переиспользует устаревшие метаданные.
CLEANUP_DEBIAN=0
cleanup() {
    if [ "$CLEANUP_DEBIAN" -eq 1 ]; then
        rm -rf "$PROJECT_ROOT/debian"
    fi
}
trap cleanup EXIT

echo "[1/2] Подготовка debian файлов..."
if [ -e "$PROJECT_ROOT/debian" ]; then
    echo "[ОШИБКА] $PROJECT_ROOT/debian уже существует — уберите его вручную."
    echo "         Скрипт не трогает чужой каталог, чтобы не удалить ваши правки."
    exit 1
fi
cp -r "$DEBIAN_SRC" "$PROJECT_ROOT/"
CLEANUP_DEBIAN=1
chmod +x "$PROJECT_ROOT/debian/rules"

echo "[2/2] Сборка пакета (dpkg-buildpackage)..."
cd "$PROJECT_ROOT"
dpkg-buildpackage -b -us -uc

DEB_PATH="$(cd "$PROJECT_ROOT/.." && pwd)/${PACKAGE_NAME}_${VERSION}_amd64.deb"
echo
echo "========================================"
echo "Готово! Пакет: $DEB_PATH"
echo "========================================"
echo
echo "Установка: sudo apt-get install '$DEB_PATH'"
