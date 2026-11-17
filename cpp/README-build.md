# Building telemetria (cpp/)

Three supported configurations. All share one `cpp/CMakeLists.txt`; platform
source selection and link libraries are handled there (no `#ifdef` in core).

## Host (Linux/native, tests ON)

```sh
cmake -S cpp -B cpp/build-host -G Ninja -DTELEMETRIA_BUILD_TESTS=ON
cmake --build cpp/build-host
cpp/build-host/telemetria_tests
```

Tests link against the static `telemetria_core` plus GoogleTest and run on Linux.
They cover the core pipeline natively (config parsing, buffering/backpressure, the
JSON wire format, C3D write + read-back) plus the platform pieces that are pure
standard C++ (`WinPathResolver`, `winHttpRequestTarget`). `WinHttpTransport::postJson`
itself is `#ifdef _WIN32` and is only compile-verified by the MinGW cross-build.

## Windows x64 (MSVC — what Unreal loads, see Task 12)

From a "x64 Native Tools Command Prompt for VS":

```sh
cmake -S cpp -B cpp/build-win -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON
cmake --build cpp/build-win
```

Produces `telemetria.dll` + `telemetria.lib` (MSVC import lib — the name the
Unreal build expects; it is emitted because the C ABI is `__declspec(dllexport)`).
The `winhttp` link library resolves automatically via
MSVC's `#pragma comment(lib, ...)`; CMake's `target_link_libraries(... winhttp)`
covers the import as well.

### Windows x64 cross-compiled from Linux (MinGW-w64)

```sh
cmake -S cpp -B cpp/build-win -G "Ninja" \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DTELEMETRIA_BUILD_TESTS=OFF
cmake --build cpp/build-win
```

Produces `libtelemetria.dll` plus the GNU-style import library
`libtelemetria.dll.a`. On MinGW the `winhttp` target links as `-lwinhttp`
(MSVC-only `#pragma comment` is not needed). Note the GNU import lib is NOT
named `telemetria.lib`; use the MSVC build when linking from Unreal/MSVC.
Tests are OFF for the cross build: the host GoogleTest binary cannot run
here — host tests gate the core.

## Android arm64-v8a (NDK)

```sh
export NDK=/opt/android-ndk-r26d
cmake -S cpp -B cpp/build-arm64 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=24 \
  -DTELEMETRIA_BUILD_TESTS=OFF
cmake --build cpp/build-arm64
```

Produces `libtelemetria.so` (ELF64 / AArch64). Links against the NDK `log`
library for the Android log sink. Tests are never built on Android.

## Exported C ABI (always verify the artifacts)

`core/api/TelemetryAPI.cpp` is compiled into the SHARED `telemetria` target (not only
into the static `telemetria_core`): a static archive member is pulled in only when some
symbol is still undefined, so a shared lib built purely from the platform sources would
silently export nothing usable. The entry points are marked through the `TELEMETRIA_API`
macro: `__declspec(dllexport)` when `TELEMETRIA_EXPORTS` is set (the SHARED target,
Windows) / `__declspec(dllimport)` for consumers, and default visibility elsewhere.

```sh
# Windows (MinGW cross-build shown here; MSVC: dumpbin /exports telemetria.dll)
x86_64-w64-mingw32-objdump -p cpp/build-win/libtelemetria.dll | grep telemetry_
# expect exactly: telemetry_initialize, telemetry_record_frame, telemetry_force_upload,
# telemetry_shutdown, telemetry_get_feature_flags, telemetry_set_frame_rate
# telemetry_set_java_context MUST be absent on Windows (Android-only)

# Android
$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf --dyn-syms \
    cpp/build-arm64/libtelemetria.so | grep -E "telemetry_|JNI_OnLoad"
# expect the 6 above + telemetry_set_java_context + JNI_OnLoad
```

Both builds run this check automatically (`cpp/scripts/check_exports.sh` as a `POST_BUILD`
step), so losing an export breaks the build instead of shipping a broken artifact.

## Notes

- Link libraries per platform: Windows `winhttp`, Android `log` (selected by
  the CMake structure, so MSVC simply never sees NDK-only flags/targets and
  vice versa).
- `ezc3d` is always built as a static library; its objects are folded into
  `telemetria_core` / the shared lib.
- Build dirs (`cpp/build-*/`) are untracked; see root `.gitignore`.
