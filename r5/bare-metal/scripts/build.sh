#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

rm -rf "$BUILD_DIR"

cmake \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_DIR/cmake/toolchain-cortexr5.cmake" \
    -S "$PROJECT_DIR" \
    -B "$BUILD_DIR" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \

cmake --build "$PROJECT_DIR/build"
