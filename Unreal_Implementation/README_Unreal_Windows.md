# Telemetria – Unreal Engine / Windows x64 (SteamVR)

Guide for the **Telemetria** native library in an **Unreal Engine (Win64)** PC-VR project
using the **SteamVR** OpenXR runtime. Complements `README_Unreal.md` (Android / Meta Quest).

The capture component (`TelemetryComponent.cpp`) is the same as on Android; the only
C-ABI difference is the new third field `baseDir` of `TelemetryConfigPlain`
(see `cpp/core/TelemetryTypes.h`), which the component sets to
`FPaths::ProjectSavedDir()`. The JNI context injection
(`telemetry_set_java_context`) remains **Android-only**.

---

## 1. Build the native library (MSVC)

From `cpp/`, follow `cpp/README-build.md` → section **"Windows x64 (MSVC)"**
("x64 Native Tools Command Prompt for VS"):

```sh
cmake -S cpp -B cpp/build-win -G "Ninja" -DTELEMETRIA_BUILD_TESTS=ON
cmake --build cpp/build-win
```

This produces **both** `telemetria.dll` and `telemetria.lib` (the MSVC linker emits the
import lib because the C ABI is marked `__declspec(dllexport)` via `TELEMETRIA_EXPORTS`,
see `cpp/CMakeLists.txt`).

> **Important (`.lib` vs `.a`):** Unreal on Win64 uses the MSVC linker and needs the
> MSVC import library named `telemetria.lib`. The MinGW-w64 cross-build instead produces
> **`libtelemetria.dll` + `libtelemetria.dll.a`** — the GNU-style import lib is *not* named
> `telemetria.lib` and the MSVC linker cannot consume it, so the Win64 Unreal target
> (`PublicAdditionalLibraries.Add(.../lib/telemetria.lib)`) fails to link with MinGW
> artifacts. Use the MSVC build when linking from Unreal. See
> `cpp/README-build.md` ("Windows x64 cross-compiled from Linux").

### Verify the DLL actually exports the C ABI

Do not trust the file name — check the export table:

```sh
# MinGW-built artifact (names carry the lib*/.dll.a suffix)
x86_64-w64-mingw32-objdump -p cpp/build-win/libtelemetria.dll | grep telemetry_
```

Expected: exactly the 5 entry points the Windows plugin links —
`telemetry_initialize`, `telemetry_record_frame`, `telemetry_force_upload`,
`telemetry_shutdown`, `telemetry_get_feature_flags`. `telemetry_set_java_context` must be
**absent** on Windows (Android-only export). The same check runs automatically as a
CMake `POST_BUILD` step (`cpp/scripts/check_exports.sh`), so a build that loses the
exports fails instead of producing an unloadable DLL.

---

## 2. Place the binaries in the plugin

```
Plugins/Telemetry/Source/ThirdParty/
└── Windows/
    ├── lib/
    │   └── telemetria.lib      # MSVC import lib (link time)
    └── bin/
        └── telemetria.dll      # staged next to the game (runtime)
```

`Telemetry.Build.cs` handles both:

- `PublicAdditionalLibraries.Add(.../ThirdParty/Windows/lib/telemetria.lib)`
- `PublicDelayLoadDLLs.Add("telemetria.dll")` → loaded on first call
- `RuntimeDependencies.Add(.../ThirdParty/Windows/bin/telemetria.dll)` → copied into the
  packaged build's binary folder

No extra `winhttp` dependency is needed in the module rules: the DLL links WinHTTP
privately.

The `.uplugin` already lists `Win64` in `SupportedTargetPlatforms`.

---

## 3. Project configuration (OpenXR + SteamVR)

1. `Edit > Plugins` → enable **OpenXR** (and **OpenXR Hand Tracking** extension support
   if you want hand joints) → restart editor.
2. `Project Settings > Plug-in (XR) > OpenXR` → set **SteamVR** as the preferred
   OpenXR-SDK/active runtime.
3. SteamVR installed and running on the PC with the headset connected.

**Hand tracking is OPTIONAL at runtime:** joints are only polled when the
`handTracking` flag is enabled in the config and the OpenXR hand-tracking extension
provides `FXRMotionControllerData::HandKeyPositions`. Without it, HMD + controllers are
still captured normally.

---

## 4. `initialConfig.json`

The native library reads `<baseDir>/initialConfig.json` and writes the
`session_<id>.c3d` files to the same `baseDir`, where `baseDir = FPaths::ProjectSavedDir()`:

- **Editor / from-launcher:** `<YourProject>/Saved/`
- **Packaged build:** `<Game>/Saved/` (UE data dir of the packaged app)

JSON schema is unchanged with respect to Android (see the main README / example at the
repository root).

---

## 5. Verification

1. Unreal **Output Log**: `[Telemetry] BeginPlay` and
   `[Telemetry] init rc=<frameRate>` (rc between 1 and 240).
2. `session_<id>.c3d` appears under `<Project>/Saved/`.
3. JSON chunks reach the configured `endpointUrl` (check your server / proxy).

---

## 6. Notes

- The component frees its `sessionId` / `deviceInfo` / `baseDir` UTF-8 buffers in
  `EndPlay`, `OnComponentDestroyed` and on the will-enter-background callback, following
  the same lifetime pattern used on Android.
- On macOS (and any platform without `telemetria`), the extern prototypes fall back to
  static no-op implementations, so the plugin still compiles and simply does nothing.
- Packaging/run on a real PC-VR headset has **not** been executed on the CI host used
  for this change (no Unreal Engine / no headset available); the code path follows the
  Android integration and the C ABI in `cpp/core/api/TelemetryAPI.h`.
