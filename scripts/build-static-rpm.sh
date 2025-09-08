#!/usr/bin/env bash
set -euo pipefail
# Build a statically linked RPM of json-view. This avoids runtime
# dependencies on the host's glibc or ncurses libraries and is suitable for
# platforms like Amazon Linux 2023 whose system libraries are older than
# the ones used to build the standard package.
# Usage: scripts/build-static-rpm.sh [build-dir]

BUILD_DIR=${1:-build-static}

cmake -S . -B "$BUILD_DIR" -DJSON_VIEW_STATIC=ON
cmake --build "$BUILD_DIR"
# Generate the RPM using CPack
cpack -G RPM --config "$BUILD_DIR/CPackConfig.cmake"
