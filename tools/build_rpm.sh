#!/bin/bash
# Builds the RPM package of OpenVegas Effects (Fedora/RHEL).
# Requires: CMake, Qt 6, rpmbuild.
#
# rpmbuild builds from the source archive (%build in openvegaseffects.spec), so
# the project is not configured or built locally first - that would be the same
# work twice. Its tree (_topdir) is build/Linux_GCC-x64-Release-RPM, as build
# trees are build/<OS>_<compiler>-<arch>-<configuration>.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RPM_DIR="$SCRIPT_DIR/rpm"
PACKAGE_NAME="openvegaseffects"
SPEC_FILE="$RPM_DIR/${PACKAGE_NAME}.spec"

# The version comes from the spec file, so it cannot drift from it or from
# CMakeLists.txt.
VERSION="$(sed -n 's/^%define[[:space:]]\+version[[:space:]]\+\(.*\)$/\1/p' "$SPEC_FILE" | head -n1)"
RELEASE="$(sed -n 's/^%define[[:space:]]\+release[[:space:]]\+\(.*\)$/\1/p' "$SPEC_FILE" | head -n1)"
: "${VERSION:?could not read the version from $SPEC_FILE}"
: "${RELEASE:=1}"

echo "========================================"
echo "OpenVegas Effects - RPM Package Builder"
echo "========================================"
echo "Version: ${VERSION}-${RELEASE}"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ERROR] CMake not found!"; exit 1; }
command -v rpmbuild >/dev/null 2>&1 || { echo "[ERROR] rpmbuild not found! Install it: sudo dnf install rpm-build rpmdevtools"; exit 1; }

echo "[1/4] Setting up the RPM build tree..."
RPMBUILD_DIR="$PROJECT_ROOT/build/Linux_GCC-x64-Release-RPM"
mkdir -p "$RPMBUILD_DIR"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

echo "[2/4] Creating the source archive..."
# The archive's top-level directory has to be named <package>-<version>: that
# is where %setup -q goes. Without --transform the archive unpacks into a
# directory named after the project folder (OpenVegas_Effects, say) and %prep
# fails.
cd "$PROJECT_ROOT/.."
PROJECT_DIR_NAME="$(basename "$PROJECT_ROOT")"
tar --exclude-vcs \
    --exclude="${PROJECT_DIR_NAME}/build" \
    --exclude='build-*' \
    --exclude="${PROJECT_DIR_NAME}/SAMPLES" \
    --exclude='*.o' \
    --exclude='*.a' \
    --exclude='**/node_modules' \
    --transform="s,^${PROJECT_DIR_NAME},${PACKAGE_NAME}-${VERSION}," \
    -czf "$RPMBUILD_DIR/SOURCES/${PACKAGE_NAME}-${VERSION}.tar.gz" \
    "$PROJECT_DIR_NAME"

echo "[3/4] Copying the spec file..."
cp "$SPEC_FILE" "$RPMBUILD_DIR/SPECS/"

echo "[4/4] Building the RPM package..."
cd "$RPMBUILD_DIR"
rpmbuild --define "_topdir $RPMBUILD_DIR" -ba "SPECS/${PACKAGE_NAME}.spec"

echo
echo "========================================"
echo "Done! The RPM package is in:"
echo "  $RPMBUILD_DIR/RPMS/x86_64/${PACKAGE_NAME}-${VERSION}-${RELEASE}*.x86_64.rpm"
echo "========================================"
echo
echo "Install: sudo dnf install $RPMBUILD_DIR/RPMS/x86_64/${PACKAGE_NAME}-${VERSION}-${RELEASE}*.x86_64.rpm"
