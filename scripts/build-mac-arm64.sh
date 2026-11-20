#!/bin/bash

set -e

SCRIPTS_DIR="$( dirname -- "$0"; )"
BASE_DIR="$SCRIPTS_DIR/.."
SOURCES_DIR="$BASE_DIR/sources"
ARTIFACTS_DIR="$BASE_DIR/artifacts"

PLATFORM="mac-arm64"
BUILD_DIR="$BASE_DIR/build/$PLATFORM"
TARGET_DIR="$ARTIFACTS_DIR/$PLATFORM"

# Only these submodules are needed for the library build (libjxl uses jpegli's copy of highway)
git -C "$SOURCES_DIR/jpegli" submodule update --init --depth 1 third_party/highway third_party/libjpeg-turbo third_party/skcms
git -C "$SOURCES_DIR/libjxl" submodule update --init --depth 1 third_party/brotli third_party/skcms

rm -rf "$BUILD_DIR"
cmake -S "$BASE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build "$BUILD_DIR" --target naps2_jpeg -j"$(sysctl -n hw.ncpu)"

mkdir -p "$TARGET_DIR"
cp "$BUILD_DIR/libnaps2_jpeg.dylib" "$TARGET_DIR/libnaps2_jpeg.dylib"
cp "$SOURCES_DIR/jpegli/LICENSE" "$ARTIFACTS_DIR/jpegli-license.txt"
cp "$SOURCES_DIR/jpegli/PATENTS" "$ARTIFACTS_DIR/jpegli-patents.txt"
cp "$SOURCES_DIR/jpegli/third_party/highway/LICENSE" "$ARTIFACTS_DIR/highway-license.txt"
cp "$SOURCES_DIR/jpegli/third_party/libjpeg-turbo/LICENSE.md" "$ARTIFACTS_DIR/libjpeg-turbo-license.md"
cp "$SOURCES_DIR/libwebp/COPYING" "$ARTIFACTS_DIR/libwebp-license.txt"
cp "$SOURCES_DIR/libwebp/PATENTS" "$ARTIFACTS_DIR/libwebp-patents.txt"
cp "$SOURCES_DIR/libjxl/LICENSE" "$ARTIFACTS_DIR/libjxl-license.txt"
cp "$SOURCES_DIR/libjxl/PATENTS" "$ARTIFACTS_DIR/libjxl-patents.txt"
cp "$SOURCES_DIR/libjxl/third_party/brotli/LICENSE" "$ARTIFACTS_DIR/brotli-license.txt"
cp "$SOURCES_DIR/libjxl/third_party/skcms/LICENSE" "$ARTIFACTS_DIR/skcms-license.txt"

# Notes:
# - Brew dependencies: cmake
