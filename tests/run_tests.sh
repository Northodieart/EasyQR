#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/tests"

cmake -S "$ROOT/tests" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel

"$BUILD/url_tests"

check() {
    local image="$1"
    local expected="$2"
    local actual
    actual="$("$BUILD/qr_probe" "$image")"
    if [ "$actual" != "$expected" ]; then
        printf 'FAIL %s\nexpected: %s\nactual:   %s\n' "$image" "$expected" "$actual" >&2
        exit 1
    fi
    printf 'PASS %s\n' "$(basename "$image")"
}

check "$ROOT/tests/data/single.pgm" $'https://example.com/easyqr-test'
check "$ROOT/tests/data/multi.pgm" $'https://example.com/multi-a\nhttps://www.wikipedia.org/'
check "$ROOT/tests/data/inverted.pgm" $'https://example.com/inverted'
check "$ROOT/tests/data/no-qr.pgm" ""
echo "all tests passed"
