# Cross-compiles the Linux binaries on Windows using Zig (https://ziglang.org) as the C/C++ compiler. Zig links
# against stubs for a specific glibc version, so the binaries work on old distros regardless of the build machine.
#
# Usage: build-linux-zig.ps1 -Arch x64|arm64
# Requirements: CMake, Ninja and Zig 0.16.0 (on the PATH, or set $env:ZIG to zig.exe)
param(
    [Parameter(Mandatory = $true)][ValidateSet("x64", "arm64")][string]$Arch
)

$SCRIPTS_DIR="$PSScriptRoot"
$BASE_DIR="$SCRIPTS_DIR/.."
$SOURCES_DIR="$BASE_DIR/sources"
$ARTIFACTS_DIR="$BASE_DIR/artifacts"

$PLATFORM="linux-$Arch"
$BUILD_DIR="$BASE_DIR/build/$PLATFORM"
$TARGET_DIR="$ARTIFACTS_DIR/$PLATFORM"

$ZIG_VERSION="0.16.0"
# glibc 2.17 is the oldest version supported for both x86_64 and aarch64
$ZIG_TARGET = if ($Arch -eq "x64") { "x86_64-linux-gnu.2.17" } else { "aarch64-linux-gnu.2.17" }
$PROCESSOR = if ($Arch -eq "x64") { "x86_64" } else { "aarch64" }

$ErrorActionPreference = "Stop"
function Check { if ($LASTEXITCODE -ne 0) { throw "Command failed with exit code $LASTEXITCODE" } }

$ZIG = if ($env:ZIG) { $env:ZIG } else { (Get-Command zig).Source }
$actualVersion = & $ZIG version
if ($actualVersion -ne $ZIG_VERSION) { throw "Expected Zig $ZIG_VERSION, found $actualVersion" }

# Only these submodules of jpegli are needed for the library build (skcms is only needed to configure)
git -C "$SOURCES_DIR/jpegli" submodule update --init --depth 1 third_party/highway third_party/libjpeg-turbo third_party/skcms; Check

rm -Recurse -Force "$BUILD_DIR" -ErrorAction SilentlyContinue
mkdir -Force "$BUILD_DIR/zig" | Out-Null

# CMake needs a single executable per tool, so wrap the zig subcommands. Always passing an explicit target also
# avoids Zig's default of optimizing for the build machine's CPU.
function Write-Wrapper($name, $command) {
    Set-Content -Encoding ascii "$BUILD_DIR/zig/$name.cmd" "@`"$ZIG`" $command %*"
    return (Resolve-Path "$BUILD_DIR/zig/$name.cmd").Path
}
$CC = Write-Wrapper "cc" "cc -target $ZIG_TARGET"
$CXX = Write-Wrapper "c++" "c++ -target $ZIG_TARGET"
$AR = Write-Wrapper "ar" "ar"
$RANLIB = Write-Wrapper "ranlib" "ranlib"

. cmake -S "$BASE_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR="$PROCESSOR" `
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_AR="$AR" -DCMAKE_RANLIB="$RANLIB" `
    -DCMAKE_SHARED_LINKER_FLAGS="-s"; Check  # Zig emits debug info by default, so strip it
. cmake --build "$BUILD_DIR" --target naps2_jpeg; Check

mkdir -Force "$TARGET_DIR" | Out-Null
cp "$BUILD_DIR/libnaps2_jpeg.so" "$TARGET_DIR/libnaps2_jpeg.so"
cp "$SOURCES_DIR/jpegli/LICENSE" "$ARTIFACTS_DIR/jpegli-license.txt"
cp "$SOURCES_DIR/jpegli/PATENTS" "$ARTIFACTS_DIR/jpegli-patents.txt"
cp "$SOURCES_DIR/jpegli/third_party/highway/LICENSE" "$ARTIFACTS_DIR/highway-license.txt"
cp "$SOURCES_DIR/jpegli/third_party/libjpeg-turbo/LICENSE.md" "$ARTIFACTS_DIR/libjpeg-turbo-license.md"
