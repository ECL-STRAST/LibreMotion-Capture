# Building telemetria (cpp/)

Three supported configurations. All share one `cpp/CMakeLists.txt`; platform
source selection and link libraries are handled there (no `#ifdef` in core).

## Host (Linux/native, tests ON)

```sh
cmake -S cpp -B cpp/build-host -G Ninja -DTELEMETRIA_BUILD_TESTS=ON
cmake --build cpp/build-host
cpp/build-host/telemetria_tests
```

Tests link against the static `telemetria_core` plus GoogleTest and exercise
the Windows path-resolver/HTTP transports natively where possible.

## Windows x64 (MSVC — what Unreal loads, see Task 12)

From a "x64 Native Tools Command Prompt for VS":

```sh
cmake -S cpp -B cpp/build-win -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON
cmake --build cpp/build-win
```

Produces `telemetria.dll` + `telemetria.lib` (MSVC import lib — the name the
Unreal build expects). The `winhttp` link library resolves automatically via
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

## Notes

- Link libraries per platform: Windows `winhttp`, Android `log` (selected by
  the CMake structure, so MSVC simply never sees NDK-only flags/targets and
  vice versa).
- `ezc3d` is always built as a static library; its objects are folded into
  `telemetria_core` / the shared lib.
- Build dirs (`cpp/build-*/`) are untracked; see root `.gitignore`.
