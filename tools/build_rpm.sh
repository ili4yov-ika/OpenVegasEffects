#!/bin/bash
# Скрипт сборки RPM пакета для OpenVegas Effects (Fedora/RHEL)
# Требуется: CMake, Qt6, rpmbuild
#
# Сборка идёт из архива силами rpmbuild (%build в openvegaseffects.spec) — локально
# проект не конфигурируется и не собирается, иначе та же работа делалась бы дважды.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RPM_DIR="$SCRIPT_DIR/rpm"
PACKAGE_NAME="openvegaseffects"
SPEC_FILE="$RPM_DIR/${PACKAGE_NAME}.spec"

# Версия — из spec-файла, чтобы не разъезжалась с ним и с CMakeLists.
VERSION="$(sed -n 's/^%define[[:space:]]\+version[[:space:]]\+\(.*\)$/\1/p' "$SPEC_FILE" | head -n1)"
RELEASE="$(sed -n 's/^%define[[:space:]]\+release[[:space:]]\+\(.*\)$/\1/p' "$SPEC_FILE" | head -n1)"
: "${VERSION:?не удалось прочитать version из $SPEC_FILE}"
: "${RELEASE:=1}"

echo "========================================"
echo "OpenVegas Effects - RPM Package Builder"
echo "========================================"
echo "Версия: ${VERSION}-${RELEASE}"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ОШИБКА] CMake не найден!"; exit 1; }
command -v rpmbuild >/dev/null 2>&1 || { echo "[ОШИБКА] rpmbuild не найден! Установите: sudo dnf install rpm-build rpmdevtools"; exit 1; }

echo "[1/4] Настройка RPM build окружения..."
RPMBUILD_DIR="$HOME/rpmbuild"
mkdir -p "$RPMBUILD_DIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

echo "[2/4] Создание исходного архива..."
# Каталог верхнего уровня в архиве обязан называться openvegas-<version>: именно
# туда переходит %setup -q. Без --transform архив разворачивается в каталог с
# именем папки проекта (например OpenVegas) и %prep падает.
cd "$PROJECT_ROOT/.."
PROJECT_DIR_NAME="$(basename "$PROJECT_ROOT")"
tar --exclude-vcs \
    --exclude='build' \
    --exclude='build-*' \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='**/node_modules' \
    --transform="s,^${PROJECT_DIR_NAME},${PACKAGE_NAME}-${VERSION}," \
    -czf "$RPMBUILD_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
    "$PROJECT_DIR_NAME"

echo "[3/4] Подготовка spec файла..."
cp "$SPEC_FILE" "$RPMBUILD_DIR/SPECS/"

echo "[4/4] Сборка RPM пакета..."
cd "$RPMBUILD_DIR"
rpmbuild -ba "SPECS/${PACKAGE_NAME}.spec"

echo
echo "========================================"
echo "Готово! RPM пакет создан в:"
echo "  $RPMBUILD_DIR/RPMS/x86_64/${PACKAGE_NAME}-${VERSION}-${RELEASE}*.x86_64.rpm"
echo "========================================"
echo
echo "Установка: sudo dnf install $RPMBUILD_DIR/RPMS/x86_64/${PACKAGE_NAME}-${VERSION}-${RELEASE}*.x86_64.rpm"
