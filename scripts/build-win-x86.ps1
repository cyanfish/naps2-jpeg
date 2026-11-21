$SCRIPTS_DIR="$PSScriptRoot"
$BASE_DIR="$SCRIPTS_DIR/.."
$SOURCES_DIR="$BASE_DIR/sources"
$ARTIFACTS_DIR="$BASE_DIR/artifacts"

$PLATFORM="win-x86"
$BUILD_DIR="$BASE_DIR/build/$PLATFORM"
$TARGET_DIR="$ARTIFACTS_DIR/$PLATFORM"

$ErrorActionPreference = "Stop"
function Check { if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" } }

# Only these submodules are needed for the library build (libjxl uses jpegli's copy of highway)
git -C "$SOURCES_DIR/jpegli" submodule update --init --depth 1 third_party/highway third_party/libjpeg-turbo third_party/skcms; Check
git -C "$SOURCES_DIR/libjxl" submodule update --init --depth 1 third_party/brotli third_party/skcms; Check

rm -Recurse -Force "$BUILD_DIR" -ErrorAction SilentlyContinue
# The 32-bit build is only used by the worker process (for JPEG), so it excludes WebP/JPEG XL
. cmake -S "$BASE_DIR" -B "$BUILD_DIR" -A Win32 -T ClangCL -DNAPS2_JPEG_ONLY=ON; Check
. cmake --build "$BUILD_DIR" --config Release --target naps2_jpeg; Check

mkdir -Force "$TARGET_DIR" | Out-Null
cp "$BUILD_DIR/Release/naps2_jpeg.dll" "$TARGET_DIR/naps2_jpeg.dll"
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
