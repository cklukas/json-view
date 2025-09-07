#!/usr/bin/env bash
set -euo pipefail
PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
install -Dm755 "$SCRIPT_DIR/bin/json-view" "$PREFIX/bin/json-view"
install -Dm644 "$SCRIPT_DIR/share/man/man1/json-view.1" "$PREFIX/share/man/man1/json-view.1"
install -Dm644 "$SCRIPT_DIR/share/info/json-view.texi" "$PREFIX/share/info/json-view.texi"

echo "json-view installed to $PREFIX"