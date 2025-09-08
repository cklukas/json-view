#!/usr/bin/env bash
set -euo pipefail
# Build an RPM of json-view using the system libraries on Amazon Linux 2023.
# Run this script on an Amazon Linux 2023 environment (e.g., in CI) to produce
# a package linked against the distribution's glibc and ncurses versions.
# Usage: scripts/build-al2023-rpm.sh [build-dir]

BUILD_DIR=${1:-build-al2023}

cmake -S . -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"
# Generate the RPM using CPack
cpack -G RPM --config "$BUILD_DIR/CPackConfig.cmake"
