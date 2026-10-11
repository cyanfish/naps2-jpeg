# NAPS2.Jpeg.Binaries

[![NuGet](https://img.shields.io/nuget/v/NAPS2.Jpeg.Binaries)](https://www.nuget.org/packages/NAPS2.Jpeg.Binaries/)

NAPS2.Jpeg.Binaries provides precompiled [jpegli](https://github.com/google/jpegli) JPEG encoder binaries for use with [NAPS2.Sdk](https://github.com/cyanfish/naps2/tree/master/NAPS2.Sdk).

jpegli is linked into a small native library (`naps2_jpeg`) that exposes a minimal C API (see [src/naps2_jpeg.h](src/naps2_jpeg.h)) for P/Invoke. Images are encoded as baseline JPEG with 4:2:0 chroma subsampling, adaptive quantization and optimized Huffman codes. jpegli itself is used unmodified as a submodule.

Supported platforms:
- Windows 7+ x86
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

Then run the script for each platform from the `scripts` folder (e.g. `scripts/build-win-x64.ps1`), which builds to `build/<platform>` and copies the library to `artifacts/<platform>`. Requirements: CMake 3.16+ and a C++17 compiler (Visual Studio on Windows).

The released Linux binaries are cross-compiled on Windows with [Zig](https://ziglang.org) 0.16.0 using `scripts/build-linux-zig.ps1 -Arch x64|arm64` (also requires Ninja), which targets glibc 2.17 so the binaries work on old distros. The `scripts/build-linux-*.sh` scripts build natively on Linux with the system compiler instead; the result requires the build machine's glibc version or newer.

`scripts/smoke-test.py <library path>` checks a built library (Python 3 standard library only).

## Licenses

This project is licensed under the BSD 3-Clause license, matching jpegli. jpegli and highway (dual-licensed Apache 2.0 / BSD 3-Clause) have separate copyrights; see the license files in `artifacts`. jpegli also uses headers from libjpeg-turbo.
