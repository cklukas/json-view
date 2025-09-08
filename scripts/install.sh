#!/usr/bin/env bash
set -euo pipefail
PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Ensure target directories exist (BSD install on macOS lacks -D)
install -d "$PREFIX/bin" "$PREFIX/share/man/man1" "$PREFIX/share/info"
install -m 755 "$SCRIPT_DIR/bin/json-view-app" "$PREFIX/bin/json-view-app"
install -m 755 "$SCRIPT_DIR/bin/json-view" "$PREFIX/bin/json-view"
install -m 644 "$SCRIPT_DIR/share/man/man1/json-view.1" "$PREFIX/share/man/man1/json-view.1"
install -m 644 "$SCRIPT_DIR/share/info/json-view.texi" "$PREFIX/share/info/json-view.texi"

echo "json-view and json-view-app installed to $PREFIX"
