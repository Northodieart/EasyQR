#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERSION="1.3.0"
BUILD_DIR="$ROOT/build/win-x64"
DIST_DIR="$ROOT/dist"

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-mingw64.cmake"
cmake --build "$BUILD_DIR" --parallel

mkdir -p "$DIST_DIR"
install -m 0755 "$BUILD_DIR/EasyQR.exe" "$DIST_DIR/EasyQR.exe"

command -v makensis >/dev/null || { echo "makensis (NSIS) is required" >&2; exit 1; }
makensis -V2 \
    -DAPP_EXE="$DIST_DIR/EasyQR.exe" \
    -DOUTPUT="$DIST_DIR/EasyQR-$VERSION-x64-setup.exe" \
    -DAPP_ICON="$ROOT/assets/easyqr.ico" \
    -DLICENSE_FILE="$ROOT/LICENSE" \
    "$ROOT/installer/EasyQR.nsi"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
cp "$DIST_DIR/EasyQR.exe" "$STAGE/EasyQR.exe"
cp "$ROOT/LICENSE" "$STAGE/LICENSE.txt"
(cd "$STAGE" && zip -9 -q "$DIST_DIR/EasyQR-$VERSION-x64-portable.zip" EasyQR.exe LICENSE.txt)

(cd "$DIST_DIR" && sha256sum EasyQR.exe "EasyQR-$VERSION-x64-portable.zip" \
    "EasyQR-$VERSION-x64-setup.exe" > SHA256SUMS.txt)

echo
echo "Built:"
ls -lh "$DIST_DIR/EasyQR.exe" \
       "$DIST_DIR/EasyQR-$VERSION-x64-portable.zip" \
       "$DIST_DIR/EasyQR-$VERSION-x64-setup.exe" \
       "$DIST_DIR/SHA256SUMS.txt"
