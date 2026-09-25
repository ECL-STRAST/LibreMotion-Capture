# SteamVR / Windows (PC-VR) — Usage Guide

This document describes the **Windows x64 / SteamVR** path of **LibreMotion-Capture**:
how to build the native library, how the `initialConfig.json` file is located per engine,
how to verify a capture session, and the (unchanged) config schema.

The library now builds from one codebase for two targets:

- **Android / Meta Quest** — `libtelemetria.so` + AAR (unchanged behaviour, see the
  engine Android READMEs).
- **Windows x64 / PC-VR (SteamVR)** — `telemetria.dll` (+ `telemetria.lib` import lib on
  MSVC), captured through the engines' **OpenXR** plug-ins.

Capture is **OpenXR-based** (the engine's OpenXR plug-in driving the SteamVR runtime), not
OpenVR/SteamVR-native. The JSON wire format, the C3D marker model, the feature-flag bitmask
and the lifecycle calls (`initialize` / `record_frame` / `force_upload` / `shutdown` /
`get_feature_flags`) are identical across platforms.

For step-by-step project setup see the per-engine guides:

- Unity Standalone/Windows → [`Unity_Implementation/README_Unity_Windows.md`](../Unity_Implementation/README_Unity_Windows.md)
- Unreal Engine Win64 → [`Unreal_Implementation/README_Unreal_Windows.md`](../Unreal_Implementation/README_Unreal_Windows.md)

---

## 1. Prerequisites

- **SteamVR** installed and running on the PC, set as the active OpenXR runtime.
- A **PC-VR headset** connected to that PC (anything SteamVR drives over OpenXR).
- Engine plug-ins:
  - **Unity (Standalone/x64):** *OpenXR Plug-in Management* + *OpenXR Hands*
    (Hands feature enabled in project settings).
  - **Unreal (Win64):** the **OpenXR** plugin (with the SteamVR runtime selected);
    optionally the OpenXR hand-tracking extension for hand joints.
- To build the native library: **CMake** + **Ninja**, and either **MSVC** (Visual Studio's
  "x64 Native Tools Command Prompt") or **MinGW-w64**. `ezc3d` is bundled as a submodule
  and built as a static library.

---

## 2. Build the native library

Long command blocks live in **[`cpp/README-build.md`](../cpp/README-build.md)**; pick the
configuration you need there:

| Target | Where | Produces |
| --- | --- | --- |
| Host (Linux/native, tests ON) | `cpp/README-build.md` → "Host" | `telemetria_tests` (GoogleTest, no VR runtime needed) |
| **Windows x64 (MSVC)** — what Unreal loads | `cpp/README-build.md` → "Windows x64 (MSVC)" | `telemetria.dll` + `telemetria.lib` |
| Windows x64 (MinGW-w64 cross from Linux) | `cpp/README-build.md` → "Windows x64 cross-compiled from Linux" | `libtelemetria.dll` + `libtelemetria.dll.a` |
| Android arm64-v8a (NDK) | `cpp/README-build.md` → "Android arm64-v8a (NDK)" | `libtelemetria.so` |

One-liners (full detail in `cpp/README-build.md`):

```sh
# Windows x64 with MSVC (x64 Native Tools Command Prompt for VS) — use this for Unreal
cmake -S cpp -B cpp/build-win -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON && cmake --build cpp/build-win

# Windows x64 cross-compiled from Linux with MinGW-w64 (GNU-style import lib, NOT telemetria.lib)
cmake -S cpp -B cpp/build-win -G "Ninja" -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
  -DTELEMETRIA_BUILD_TESTS=OFF && cmake --build cpp/build-win
```

> **`.lib` vs `.dll.a`:** Unreal/MSVC needs the MSVC import lib `telemetria.lib`; the MinGW
> cross-build emits `libtelemetria.dll.a` instead, which the MSVC linker cannot consume.
> Build with **MSVC** when linking from Unreal. See the "Important" note in the Unreal guide.

Always verify the DLL actually exports the C ABI — the commands are in
`cpp/README-build.md` ("Exported C ABI"). Expected on **Windows**: `telemetry_initialize`,
`telemetry_record_frame`, `telemetry_force_upload`, `telemetry_shutdown`,
`telemetry_get_feature_flags`, and **`telemetry_set_java_context` must be absent**
(Android-only). The check also runs automatically as a CMake `POST_BUILD` step
(`cpp/scripts/check_exports.sh`), so a build that loses an export fails instead of shipping
an unloadable DLL.

---

## 3. Where `initialConfig.json` lives

The native library reads `<baseDir>/initialConfig.json` and writes
`session_<id>.c3d` to the **same** `baseDir`. `baseDir` is supplied by the engine through
`TelemetryConfigPlain.baseDir` (third field of the config struct, see
`cpp/core/TelemetryTypes.h`):

| Platform / mode | `baseDir` | Config file location |
| --- | --- | --- |
| Unity Standalone/Windows | `Application.dataPath` | Editor: `Assets/`. Build: `<AppName>_Data/` next to the `.exe`. |
| Unreal Win64 | `FPaths::ProjectSavedDir()` | `<Project>/Saved/` (editor and packaged). |
| Windows (no `baseDir` passed) | executable directory | the directory containing the `.exe` |
| Android (unchanged) | package external files dir | `/sdcard/Android/data/<pkg>/files/initialConfig.json` |

If a Unity install is read-only (e.g. `Program Files`) and writing the `.c3d` beside
`dataPath` fails, switch the bootstrap's `baseDir` to `Application.persistentDataPath`
(see the Unity Windows guide). The Android path resolver also falls back from `/sdcard/`
to `/storage/emulated/0/` for the config file.

---

## 4. `initialConfig.json` schema (UNCHANGED)

The schema is identical on Android and Windows. All keys are optional; omitted keys keep
their defaults. A full example is at the repository root (`initialConfig.json`).

```json
{
  "endpoint": "https://examplebbdd/myVRDataTable",
  "apiKey": "yourSecurityKeyifNeeded",
  "framesPerFile": 150,
  "frameRate": 72,
  "handTracking": true,
  "primaryButton": true,
  "secondaryButton": true,
  "grip": true,
  "trigger": true,
  "joystick": true,
  "saveJsonLocally": false
}
```

| Key | Type | Default if absent | Notes |
| --- | --- | --- | --- |
| `endpoint` | string | `http://localhost:1414` | REST endpoint that receives the JSON chunks (Supabase/PostgREST style). |
| `apiKey` | string | `non_existant` | Sent as the API key header. If `endpoint` or `apiKey` ends up empty or still holds its placeholder default (no config file, or the key missing from it), uploads are disabled at init (C3D + buffering keep working). |
| `framesPerFile` | int | `200` | Frames buffered per JSON chunk; must be `> 0`. |
| `frameRate` | int | `60` | Returned to the engine (`rc`). Accepted range **[1, 240]**; out of range falls back to `60`. |
| `handTracking` | bool | `false` | Enables hand-joint capture (optional at runtime, see §6). |
| `primaryButton` | bool | `false` | Feature flag → bitmask bit 1. |
| `secondaryButton` | bool | `false` | Feature flag → bitmask bit 2. |
| `grip` | bool | `false` | Feature flag → bitmask bit 3. |
| `trigger` | bool | `false` | Feature flag → bitmask bit 4. |
| `joystick` | bool | `false` | Feature flag → bitmask bit 5. |
| `saveJsonLocally` | bool | `false` | Also writes every JSON chunk to disk, next to the C3D file, as `session_<id>_chunk<nnnn>.json` (numbered from `0001`, same body as the upload). Works with uploads disabled too. Not a feature flag. |

Defaults, parsing and the `[1, 240]` frame-rate gate are in `cpp/core/config/ConfigParser.cpp`;
the feature-flag bitmask (`handTracking`→bit0 … `joystick`→bit5) is exposed via
`telemetry_get_feature_flags()`. If the file is missing/unreadable the library logs a notice
and uses defaults.

---

## 5. Verify a session

1. **Logs.** Every log line is prefixed `[telemetria]` (see `cpp/core/platform/Log.cpp`).
   On Windows the sink writes via `OutputDebugStringA` — read it with DebugView / the Visual
   Studio output window; debug builds (or `TELEMETRIA_CONSOLE`) additionally mirror to stderr
   (`cpp/platform/windows/WinLogSink.cpp`). You should see `telemetry initialized` and the
   resolved `config final: endpointUrl=..., framesPerFile=..., sessionId=...` line.
2. **Init return.** The engine logs `telemetry_initialize rc=<frameRate>` (Unity console) /
   `[Telemetry] init rc=<frameRate>` (Unreal Output Log); `rc` is the frame rate in `[1, 240]`.
3. **C3D file.** `session_<id>.c3d` appears in `baseDir` (Unity `dataPath` / UE `Saved/`)
   after shutdown; the recorder logs the exact path (`C3DRecorder initialized. Path=...`).
4. **JSON upload.** JSON chunks are `POST`ed to the configured `endpoint` (check your
   server / proxy). Force a chunk early with `telemetry_force_upload()`.
   With `saveJsonLocally=true` each chunk is also written to `baseDir` as
   `session_<id>_chunk<nnnn>.json` (logged as `Saved chunk locally, ... path: ...`).
5. **Export check.** Confirm the C ABI is exported — commands in `cpp/README-build.md`.

---

## 6. Hand tracking is optional at runtime

HMD + two controllers are captured and uploaded **even when no hand tracking is available**.
Hand joints (26 per hand) are only polled when `handTracking=true` in the config **and** the
OpenXR hands source exposes them:

- **Unity:** needs the *OpenXR Hands* feature; if `handTracking=true` but there is no
  `XRHandSubsystem`, the bootstrap registers 0 joints and logs a warning — capture continues.
- **Unreal:** needs the OpenXR hand-tracking extension; without it joints stay empty and
  HMD + controllers are still recorded normally.

Leave `handTracking=false` (the default) to capture HMD + controllers only.

---

## 7. Behaviour notes

- **Android `initialize` return codes changed.** Previously a missing Java context
  (`JavaVM`/activity not set) made `telemetry_initialize` return **`-2`** and abort. That
  early return was removed in the cross-platform refactor: `initialize` no longer fails for
  a missing Java context, so the config/C3D pipeline still comes up. HTTP uploads that need
  the Java context now fail **per chunk** on the worker thread, logged as
  `Upload FAILED, frames: <n>` (`cpp/core/TelemetryManager.cpp`). Inject the Java context
  (`telemetry_set_java_context`) as before to enable uploads on Android.
- **Frame rate.** Default `60`; overridden only by a readable config file, and clamped to
  `[1, 240]` (out-of-range → `60`).
- **Empty or placeholder `endpoint`/`apiKey`.** Uploads are disabled once at init (logged as
  `Missing supabase config (endpoint/apiKey); uploads disabled`); buffering and C3D writing
  continue.
- **`telemetry_set_java_context` is Android-only** — it is not present in the Windows DLL
  and is guarded by `#if UNITY_ANDROID` in `TelemetriaAPI.cs`.
- **Re-initializing starts a new session.** `telemetry_initialize` after a
  `telemetry_shutdown` now produces a second C3D file; previously the recorder stayed
  `finalized_` and every frame of the second (and all later) sessions was dropped. This
  matters on Windows because neither Unity play mode nor Unreal PIE reloads the native
  library between runs. Re-initializing *without* a preceding shutdown is ignored: the
  session in progress is never clobbered.
- **`baseDir` is re-read on every `telemetry_initialize`.** The newest non-empty value
  wins for both `initialConfig.json` and the C3D output path; passing `NULL` falls back
  to the executable directory.
- **C3D is buffered in memory until shutdown.** Every frame stays in the recorder until
  `telemetry_shutdown` writes the file: `sizeof(VRFrameDataPlain)` is 2552 bytes, so a
  90 Hz hour-long PC-VR session holds roughly 800 MB in `C3DRecorder::frames_`
  (`C3Dflush()` is still a no-op stub). Keep sessions bounded, or record at a lower rate.
- **Local JSON copies (`saveJsonLocally`).** Each chunk is written by the upload worker
  before it is posted, so the file exists even when the upload fails or uploads are
  disabled. A write failure is logged as `Could not save chunk locally at '...'` and the
  upload still goes ahead. Chunks dropped by the queue backpressure (more than 4 waiting)
  are neither saved nor uploaded. On Android the files land in
  `/sdcard/Android/data/<pkg>/files/`, beside the `.c3d`.
- **A C3D write that cannot open its output is reported, not swallowed.** `ezc3d` opens
  the file and never checks `is_open()`, so the recorder now verifies the artifact after
  writing and logs `write produced no file at '...' (directory missing or read-only?)`
  instead of claiming success. Prefer a writable `baseDir` (see §3 — `Application.dataPath`
  under `Program Files` is typically not writable).
- **Windows: `baseDir` must be a plain-ASCII path.** The native library opens
  `initialConfig.json` and the `.c3d` through narrow `char*` file APIs (its own and
  `ezc3d`'s), which Windows interprets in the system ANSI code page, not UTF-8. Unreal
  passes `baseDir` as UTF-8, so *any* non-ASCII character breaks it; Unity passes it as
  ANSI, which only works for characters that exist in the machine's code page. Either way
  a folder such as `C:\Users\Martín\...` or `D:\Captures\Sesión 1\` fails **silently**: the
  config falls back to defaults (uploads disabled) and the C3D write logs
  `write produced no file at '...'`. Keep the project/install folder and `baseDir` ASCII
  only (on Unreal that includes the project path, since `baseDir` is `Saved/`). Android is
  not affected.
- **Proxy resolution differs per platform.** Android goes through the JVM/Java URL stack,
  which honours the system/IE proxy configuration; Windows uses
  `WINHTTP_ACCESS_TYPE_DEFAULT_PROXY`, which reads the WinHTTP machine configuration
  (`netsh winhttp show proxy`) and is *not* the same thing. Behind an enterprise proxy,
  configure WinHTTP explicitly or uploads will fail only on Windows.
- **Android keeps its Activity reference after shutdown.** The global reference is
  released when a new context is injected, not by `telemetry_shutdown`, so
  `telemetry_initialize` can be called again without re-injecting. The Android-only
  build used to release it at shutdown; the reference lives for the process lifetime now.
- **Unreal: background teardown is Android-only.** `OnAppWillEnterBackground` (flush +
  shutdown) is subscribed under `#if PLATFORM_ANDROID`. On Win64,
  `ApplicationWillEnterBackgroundDelegate` fires whenever the window loses activation, so
  keeping it there would end a PC-VR session on an alt-tab; Win64 sessions end in
  `EndPlay`/`OnComponentDestroyed` as before.
- **Unverified on CI.** End-to-end capture on a real PC-VR headset has not been executed on
  the headless CI host (no SteamVR/headset, no Unreal); the portable core is covered by the
  GoogleTest suite (`cpp/build-host/telemetria_tests`).
