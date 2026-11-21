# NAPS2.Jpeg.Binaries

[![NuGet](https://img.shields.io/nuget/v/NAPS2.Jpeg.Binaries)](https://www.nuget.org/packages/NAPS2.Jpeg.Binaries/)

NAPS2.Jpeg.Binaries provides precompiled [jpegli](https://github.com/google/jpegli), [libwebp](https://chromium.googlesource.com/webm/libwebp) and [libjxl](https://github.com/libjxl/libjxl) binaries for use with [NAPS2.Sdk](https://github.com/cyanfish/naps2/tree/master/NAPS2.Sdk).

These are linked into a single native library (`naps2_jpeg`) that exposes a minimal C API (see [src/naps2_jpeg.h](src/naps2_jpeg.h)) for P/Invoke.

Supported platforms:
- Windows 7+ x86 (jpegli only)
- Windows 7+ x64
- Windows 7+ arm64
- Mac 11+ arm64
- Mac 10.15+ x64
- Linux x64
- Linux arm64

## Building

```
git submodule update --init
```

Then run the script for each platform from the `scripts` folder (e.g. `scripts/build-win-x64.ps1`), which builds to `build/<platform>` and copies the library to `artifacts/<platform>`. Requirements: CMake 3.18+ and a C++17 compiler (on Windows, Visual Studio with the "C++ Clang Compiler for Windows" and "MSBuild support for LLVM (clang-cl) toolset" components).

The released Linux binaries are cross-compiled on Windows with [Zig](https://ziglang.org) 0.16.0 using `scripts/build-linux-zig.ps1 -Arch x64|arm64` (also requires Ninja), which targets glibc 2.17 so the binaries work on old distros. The `scripts/build-linux-*.sh` scripts build natively on Linux with the system compiler instead; the result requires the build machine's glibc version or newer.

`scripts/smoke-test.py <library path>` checks a built library (Python 3 standard library only).

## Licenses

This project is licensed under the BSD 3-Clause license, matching jpegli. jpegli, highway (dual-licensed Apache 2.0 / BSD 3-Clause), libwebp, libjxl, skcms (BSD 3-Clause) and brotli (MIT) have separate copyrights; see the license files in `artifacts`. jpegli also uses headers from libjpeg-turbo.
