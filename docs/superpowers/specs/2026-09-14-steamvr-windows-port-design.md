# LibreMotion-Capture — SteamVR / Windows Port — Design

Date: 2026-09-14
Status: Approved design, pending written-spec review
Author: brainstorming session (migueldulu/LibreMotion-Capture)

## 1. Problem statement

LibreMotion-Capture today is a native C++ library (`libtelemetria.so`) plus Unity and
Unreal integration that captures VR motion data (HMD pose, two controllers, two hands
of 26 joints) from **Meta Quest devices running Android**. It buffers frames, uploads
JSON chunks to a REST endpoint from a background thread, and writes one per-session
C3D file for biomechanical analysis.

We need a **port that runs on PC-VR headsets driven by SteamVR on Windows**, that
**replicates every feature** of the current system, and that a developer can drop into
a **standard Unity or Unreal application** on Windows. The capture path must be
**OpenXR-based** (the engines' OpenXR plug-ins driving the SteamVR runtime), not
OpenVR/SteamVR-native, unless a strong reason emerges.

## 2. Goals and non-goals

### Goals
- Full feature parity with the Android/original: HMD + 2 controllers + 2x26 hand
  joints; feature flags; configurable frame rate returned to the engine; JSON chunking
  + async HTTP upload; per-session C3D with the exact same point/analog layout, axis
  conversion, extra markers, hand-joint tags, and RealTime channel.
- The same JSON schema, the same C3D structure/naming, the same feature-flag bitmask,
  and the same lifecycle calls (`initialize` / `record_frame` / `force_upload` /
  `shutdown` / `get_feature_flags`).
- One codebase that builds for **Android (`.so` + AAR, unchanged behavior)** and
  **Windows x64 (`.dll` + import lib)**.
- A developer can integrate into a stock Unity (Standalone/x64) and Unreal (Win64)
  project with written, reproducible steps.
- A GoogleTest suite over the portable core that runs without any VR runtime.

### Non-goals
- A native, engine-independent OpenXR capture layer inside the library (the engines
  provide poses/hands, exactly as today).
- Support for the SteamVR-native OpenVR API.
- macOS/Linux targets (kept possible by the design, but not delivered now).
- Changing the JSON wire format or the C3D marker model.

## 3. Current-state findings (what is Android-specific)

Confirmed by reading `cpp/`, `LibreriaTelemetria/`, `Unity_Implementation/`,
`Unreal_Implementation/`:

- `cpp/TelemetryAPI.*` — C ABI entry points. Uses `<android/log.h>` and JNI
  (`JNI_OnLoad`, `telemetry_set_java_context`, global `JavaVM*`/`jobject`).
- `cpp/TelemetryManager.*` (`GestorTelemetria`) — buffering, chunk queue, worker
  thread, `toJsonFlat`. Platform-independent logic except `__android_log_print`.
- `cpp/JSONUploader.*` — **JNI bridge to the Java `AyudanteHttp.makeRequest`**; builds
  HTTP headers. This is the main Android-only dependency.
- `cpp/configReader.*` — reads `initialConfig.json` from
  `/sdcard/Android/data/<pkg>/files/` (package from `/proc/self/cmdline`); hand-rolled
  JSON parsing; feature flags + frameRate.
- `cpp/C3DRecorder.*` — accumulates frames, writes C3D via `ezc3d`; OpenXR→Gait axis
  conversion, meters→mm, extra head/wrist markers, 26-joint hand tags, quaternion
  analog channels, trailing `RealTime` channel. Platform-independent except logging.
- `cpp/VRTypes.h`, `cpp/TelemetryTypes.h` — plain C structs + `UploaderConfig`.
- `cpp/thirdparty/ezc3d` (submodule) — builds fine for desktop.
- `cpp/CMakeLists.txt` — NDK-only toolchain flags (`-lc++_static`, `-landroid`,
  `-llog`, `-latomic`, `-static-libstdc++`).
- Unity: `TelemetriaAPI.cs` (P/Invoke + Java-context injection),
  `TelemetriaBootstrap.cs` (`UnityEngine.XR` InputDevices + `UnityEngine.XR.Hands`,
  Unity→OpenXR axis negation, rate-limiting). Capture logic is already cross-platform;
  only the Java injection and the plugin packaging are Android-specific.
- Unreal: `TelemetryComponent.*` (`HeadMountedDisplay` motion-controller data,
  grip→palm fallback, `ToOpenXRAxes`, injected OpenXR/Oculus/Gamepad key mappings,
  `OnEndFrame` tick, background flush). The JNI/device-info blocks are already guarded
  by `#if PLATFORM_ANDROID`; the `.uplugin` already lists `Win64` as supported.

Conclusion: the engine-side capture is already portable and OpenXR-compatible. The
real work is (1) rebuilding the core for Windows, (2) replacing the JNI/Java HTTP
layer natively, and (3) a Windows-appropriate config/output path + logging.

## 4. Chosen approach (decisions locked during brainstorming)

1. **OpenXR role:** the engines drive OpenXR (Unity OpenXR plug-in + OpenXR Hands;
   Unreal OpenXR plug-in) against the SteamVR runtime. The native library stays
   engine-agnostic and receives plain structs, exactly as today.
2. **Repo strategy:** one **cross-platform core** with per-platform implementations;
   the same core builds the Android `.so`/AAR and the Windows `.dll`. Single source of
   truth for buffering, JSON serialization, config parsing, and C3D writing.
3. **HTTP on Windows:** **WinHTTP** (built into Windows, OS TLS for HTTPS, zero
   third-party deps). Android keeps its existing JNI/Java uploader behind the same
   interface.
4. **Config / C3D paths:** the **engine passes an explicit base directory** to
   `telemetry_initialize`; native reads `<baseDir>/initialConfig.json` and writes
   `<baseDir>/session_<id>.c3d`. Defaults to the executable directory when unset on
   Windows; Android keeps its `/sdcard` logic.
5. **Hand tracking:** implemented through the engines' OpenXR hand-tracking path,
   **optional at runtime**; if the runtime/device has no hands, joints stay empty and
   HMD/controllers still capture fully (mirrors current graceful behavior).
6. **Testing:** a **GoogleTest** suite over the portable core (reusing the existing
   `googletest` submodule under ezc3d) with mocked platform impls; TDD during
   implementation; live VR capture verified manually on a PC-VR headset.
7. **Platform abstraction:** **interfaces + per-platform implementations** (not
   inline `#ifdef`), so the core is mockable and platform code stays out of core logic.

## 5. Target architecture

```
cpp/
  core/                         # platform-agnostic; no JNI, no WinHTTP, no <android/log.h>
    VRTypes.h, TelemetryTypes.h         # unchanged plain structs + UploaderConfig
    platform/Interfaces.h               # IHttpTransport, IPathResolver, ILogSink
    config/ConfigParser.{h,cpp}         # PURE: JSON text -> UploaderConfig + flags + frameRate
    TelemetryManager.{h,cpp}            # buffering, chunk queue, worker thread, toJsonFlat
    C3DRecorder.{h,cpp}                 # ezc3d writing, OpenXR->Gait axes, markers, hand tags
    api/TelemetryAPI.{h,cpp}            # C ABI; wires core + injected platform bundle
  platform/
    android/
      AndroidHttpTransport.cpp          # JNI -> AyudanteHttp.makeRequest (moved from JSONUploader)
      AndroidPathResolver.cpp           # /sdcard/Android/data/<pkg>/files/ via /proc/self/cmdline
      AndroidLogSink.cpp                # __android_log_print
      JavaContext.{h,cpp}               # JNI_OnLoad + telemetry_set_java_context (Android-only)
    windows/
      WinHttpTransport.cpp              # WinHTTP POST, headers, non-2xx -> false
      WinPathResolver.cpp               # explicit base dir -> config/c3d paths
      WinLogSink.cpp                    # OutputDebugString (+ stderr in debug builds)
  thirdparty/ezc3d/                     # submodule, unchanged
  tests/                                # GoogleTest: core + MockPlatform (mock transports, temp dir)
  CMakeLists.txt                        # STATIC telemetria_core + per-platform SHARED telemetria + tests
```

Data flow (unchanged): `telemetry_record_frame` → `C3DRecorder` (accumulate) +
`TelemetryManager` (front buffer). On `framesPerFile`, swap chunk → async queue; one
worker thread serializes with `toJsonFlat` then `IHttpTransport::postJson`.
Backpressure drops the oldest chunk beyond `maxQueuedChunks_` (4). `force_upload`
flushes a partial chunk; `shutdown` flushes, stops/joins the worker, writes C3D,
releases resources. Rate-limiting remains engine-side, driven by the `frameRate`
returned from `telemetry_initialize`.

## 6. Platform abstraction seams

```cpp
// core/platform/Interfaces.h (illustrative)
struct IHttpTransport {
  virtual bool postJson(const std::string& url, const std::string& body,
                        const std::vector<std::pair<std::string,std::string>>& headers) = 0;
};
struct IPathResolver {
  virtual bool configFilePath(std::string& out) = 0;
  virtual bool c3dFilePath(const std::string& sessionId, std::string& out) = 0;
};
struct ILogSink { virtual void log(int level, const char* msg) = 0; };
```

- A **PlatformBundle** (`{ IHttpTransport*, IPathResolver*, ILogSink* }`) is built once
  at `telemetry_initialize` from compile-time platform selection and injected into the
  core singletons. Core components take interfaces, never concrete platform types.
- Behavior preserved per platform: the WinHTTP transport reproduces the exact header
  set (`Content-Type: application/json`, `apikey`, `Authorization: Bearer <key>`,
  `Prefer: return=representation`) and the non-2xx → `false` result the Java helper
  produced.

## 7. Public C ABI (minimal, additive)

Keep the five existing exported functions. Two additive changes only:
1. Append `const char* baseDir;` to `TelemetryConfigPlain` (UTF-8; empty/NULL →
   executable-dir default on Windows; ignored on Android).
2. `telemetry_set_java_context` is compiled/exported only under Android
   (`#if defined(__ANDROID__)` in C++, `#if UNITY_ANDROID` / `#if PLATFORM_ANDROID` in
   the engine layers); not exported and not called on Windows.

Unchanged: `telemetry_initialize` return value (effective frameRate),
`telemetry_record_frame`, `telemetry_force_upload`, `telemetry_shutdown`,
`telemetry_get_feature_flags`, the feature-flag bitmask layout
(bit0 hand, bit1 primary, bit2 secondary, bit3 grip, bit4 trigger, bit5 joystick), and
the full JSON + C3D data formats.

## 8. Engine integration

### Unity (Standalone / Windows x64)
- `TelemetriaAPI.cs`: keep P/Invokes; wrap `telemetry_set_java_context` and
  `InyectarContextoJava()` in `#if UNITY_ANDROID`; add `baseDir` to the marshalled
  `TelemetryConfigPlain`. Ship `telemetria.dll` under `Plugins/x86_64/` with a
  Windows/Standalone-only plugin importer configuration.
- `TelemetriaBootstrap.cs`: capture code unchanged; set `baseDir` from
  `Application.dataPath` (document `Application.persistentDataPath` as the fallback if
  C3D writing next to `dataPath` fails on a read-only install); skip Java injection off
  Android.
- Docs: enable the **OpenXR** plug-in for Standalone + the **OpenXR Hands** feature;
  SteamVR runtime; hand tracking optional at runtime.

### Unreal (Win64)
- `TelemetryComponent`: capture code unchanged (motion-controller data, grip→palm
  fallback, `ToOpenXRAxes`, injected OpenXR/Oculus/Gamepad mappings, `OnEndFrame` tick,
  background flush). Set `baseDir = FPaths::ProjectSavedDir()`. The JNI/device-info
  block stays under `#if PLATFORM_ANDROID`.
- `Telemetry.Build.cs`: add a Win64 branch — link `telemetria.lib` and stage
  `telemetria.dll` via `RuntimeDependencies`; keep the Android branch (APL + `.so`)
  intact.
- Docs: enable the **OpenXR** plugin for Win64 (SteamVR); OpenXR hand-tracking
  extension optional.

## 9. Build (CMake) and tests

- One `CMakeLists.txt`: build a **STATIC `telemetria_core`** from `core/`, then a
  **SHARED `telemetria`** that links the matching `platform/<os>` objects selected with
  generator expressions (`$<PLATFORM_ID:Windows>` → link `winhttp.lib`; Android →
  `log`/JNI). ezc3d stays static-linked in. MSVC drops the NDK-only link flags;
  exception/RTTI disabling configured per compiler.
- **Tests** (GoogleTest, reusing the existing `googletest` submodule): link
  `telemetria_core` + a `MockPlatform` (fake `IHttpTransport` that captures posted
  bodies; temp-dir `IPathResolver`; no-op `ILogSink`). Suites:
  1. `ConfigParser`: keys, defaults, feature-flag bitmask, frameRate range clamp [1,240].
  2. `toJsonFlat`: golden JSON incl. feature-flag conditionals (trigger/grip/buttons/
     joystick/hands) and joint arrays.
  3. Buffering/flush/backpressure and worker drain on shutdown.
  4. `C3DRecorder`: write then read back with ezc3d; assert point/analog names, axis
     conversion (mm), hand-joint tags, and the trailing `RealTime` channel.
  No VR runtime required; runs on Windows and host CI.

## 10. Deliverables

- Cross-platform `cpp/` (core + platform/android + platform/windows + tests) and
  `CMakeLists.txt` producing `telemetria.dll`/`.lib` (Win64) and the unchanged
  `.so`/AAR (Android).
- Updated Unity integration (Windows plugin importer settings + `x86_64/telemetria.dll`)
  and Unreal plugin (Win64 ThirdParty lib/dll + `Build.cs`), both still supporting
  Android.
- Documentation for **SteamVR/Windows**: prerequisites (SteamVR runtime; Unity
  OpenXR(+Hands) / UE OpenXR), where to place `initialConfig.json` per engine, how to
  verify (logs, JSON upload, `.c3d` file), and the hand-tracking-optional note.

## 11. Parity guardrails

Same JSON schema; same C3D point/analog layout and naming; same OpenXR→Gait axis
conversion and mm units; same extra markers (LFHD/RFHD, LWRA/LWRB/RWRA/RWRB) and
26-joint hand tags; same feature-flag bitmask; same `frameRate` return contract; same
lifecycle calls. The core logic is shared, so parity is structural, not re-implemented.

## 12. Risks

- **SteamVR OpenXR hand-tracking availability varies by runtime/device.** Mitigated by
  hands being optional at runtime; HMD/controllers unaffected.
- **Unity `Application.dataPath` may be read-only** in some installs. Mitigated by
  documenting `Application.persistentDataPath` as the writable fallback; config read
  still works from `dataPath`.
- **MSVC toolchain differences** (link flags, exceptions/RTTI) in the shared
  CMakeLists. Mitigated by per-compiler configuration and a CI build + test run.
- **WinHTTP header/behavior differences vs Java** (redirects, TLS). Mitigated by the
  transport unit tests plus manual verification against the real endpoint.

## 13. Out of scope for this spec (handled by the implementation plan)

Detailed task breakdown, file-by-file diffs, CI configuration, and packaging scripts
will be produced by the writing-plans step after this spec is approved.
