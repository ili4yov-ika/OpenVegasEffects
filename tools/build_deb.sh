#!/bin/bash
# Builds the Debian/Ubuntu package (.deb) of OpenVegas Effects.
# Requires: CMake, Qt 6, dpkg-buildpackage (or debuild).
#
# dpkg-buildpackage builds the project itself through debian/rules
# (dh_auto_configure / dh_auto_build / dh_auto_install), so the project is not
# configured or built separately first - that would be the same work twice.
# The build tree and the package end up in build/Linux_GCC-x64-Release-Package
# (build trees are build/<OS>_<compiler>-<arch>-<configuration>).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DEBIAN_SRC="$SCRIPT_DIR/debian"
PACKAGE_NAME="openvegaseffects"

# The version comes from debian/changelog, so the two cannot drift apart.
VERSION="$(sed -n '1s/^[^(]*(\([^)]*\)).*/\1/p' "$DEBIAN_SRC/changelog")"
: "${VERSION:?could not read the version from $DEBIAN_SRC/changelog}"

echo "========================================"
echo "OpenVegas Effects - Debian Package Builder"
echo "========================================"
echo "Version: $VERSION"
echo

command -v cmake >/dev/null 2>&1 || { echo "[ERROR] CMake not found!"; exit 1; }
command -v dpkg-buildpackage >/dev/null 2>&1 || { echo "[ERROR] dpkg-buildpackage not found! Install it: sudo apt-get install build-essential devscripts debhelper"; exit 1; }

# debian/ has to be in the project root - dpkg-buildpackage looks for it only
# there. The copy is removed on the way out (on errors too); otherwise the
# working tree stays dirty and the next run silently reuses stale metadata.
CLEANUP_DEBIAN=0
cleanup() {
    if [ "$CLEANUP_DEBIAN" -eq 1 ]; then
        rm -rf "$PROJECT_ROOT/debian"
    fi
}
trap cleanup EXIT

OUTPUT_DIR="$PROJECT_ROOT/build/Linux_GCC-x64-Release-Package"

echo "[1/3] Preparing the debian files..."
if [ -e "$PROJECT_ROOT/debian" ]; then
    echo "[ERROR] $PROJECT_ROOT/debian already exists - remove it by hand."
    echo "        The script does not touch a directory it did not create, so your edits are safe."
    exit 1
fi
cp -r "$DEBIAN_SRC" "$PROJECT_ROOT/"
CLEANUP_DEBIAN=1
chmod +x "$PROJECT_ROOT/debian/rules"

echo "[2/3] Building the package (dpkg-buildpackage)..."
cd "$PROJECT_ROOT"
dpkg-buildpackage -b -us -uc

# dpkg-buildpackage always writes beside the source tree; the results move
# into the build tree.
echo "[3/3] Moving the package to $OUTPUT_DIR..."
mkdir -p "$OUTPUT_DIR"
PARENT_DIR="$(cd "$PROJECT_ROOT/.." && pwd)"
for result in "$PARENT_DIR/${PACKAGE_NAME}"*"_${VERSION}_"*.deb \
              "$PARENT_DIR/${PACKAGE_NAME}_${VERSION}_"*.buildinfo \
              "$PARENT_DIR/${PACKAGE_NAME}_${VERSION}_"*.changes; do
    [ -e "$result" ] && mv -f "$result" "$OUTPUT_DIR/"
done
DEB_PATH="$OUTPUT_DIR/${PACKAGE_NAME}_${VERSION}_amd64.deb"
echo
echo "========================================"
echo "Done! Package: $DEB_PATH"
echo "========================================"
echo
echo "Install: sudo apt-get install '$DEB_PATH'"
