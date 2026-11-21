# LibreMotion-Capture

LibreMotion-Capture is a native C++ library developed for Android whose purpose is to capture motion data from Virtual Reality devices, including the HMD, controllers, and hand tracking, from applications developed in Unity and Unreal Engine.

The library is part of the LibreMotion ecosystem and is designed to facilitate the acquisition, normalization, and persistence of motion data for biomechanical analysis, research, or telemetry purposes.

> This README is also available in [Spanish (README.es.md)](README.es.md).

---

## Platforms

The library builds from a single codebase for two targets, sharing the same JSON wire
format, C3D marker model, feature flags and lifecycle calls:

- **Android / Meta Quest** — `libtelemetria.so` + AAR. Behaviour unchanged; config read from
  `/sdcard/Android/data/<pkg>/files/`. See `Unity_Implementation/README_Unity.md` and
  `Unreal_Implementation/README_Unreal.md`.
- **Windows x64 / PC-VR (SteamVR)** — `telemetria.dll` (+ `telemetria.lib` on MSVC), captured
  through the engines' **OpenXR** plug-ins (Unity: OpenXR + OpenXR Hands; Unreal: OpenXR with
  the SteamVR runtime). Hand tracking is optional; HMD + controllers work without it. See
  **[docs/steamvr-windows.md](docs/steamvr-windows.md)** for build, config locations,
  verification and the schema, plus `Unity_Implementation/README_Unity_Windows.md` and
  `Unreal_Implementation/README_Unreal_Windows.md`.

> [!WARNING]
> **Windows: use ASCII-only folder paths.** The project/install folder where
> `initialConfig.json` is read and the `.c3d` is written (Unity: `Application.dataPath`;
> Unreal: `<Project>/Saved/`) must not contain accents or other non-ASCII characters (e.g.
> `C:\Users\Martín\...`). Otherwise the config is silently ignored and no `.c3d` file is
> produced. See [docs/steamvr-windows.md §7](docs/steamvr-windows.md#7-behaviour-notes).

Native build commands for every target live in **[cpp/README-build.md](cpp/README-build.md)**.

### C API

Declared in `cpp/core/api/TelemetryAPI.h` (C# bindings: `Unity_Implementation/Plugins/telemetria/TelemetriaAPI.cs`):

| Function | Purpose |
| --- | --- |
| `telemetry_set_java_context(vm, activity)` | Android only, before `telemetry_initialize`: enables uploads through the AAR. |
| `telemetry_set_frame_rate(hz)` | Before `telemetry_initialize`: `hz` in [1, 240] overrides `frameRate` from `initialConfig.json` (e.g. with the headset's refresh rate), `0` clears the override. Persists across sessions. See [Unity_Implementation/HMD_REFRESH_RATE.md](Unity_Implementation/HMD_REFRESH_RATE.md). |
| `telemetry_initialize(cfg)` | Starts a session. Returns the sampling rate (1–240), also written to the C3D, or a negative error. |
| `telemetry_get_feature_flags()` | Bitmask of the optional fields enabled in the config. |
| `telemetry_record_frame(frame)` | Records one frame (C3D + JSON buffer). |
| `telemetry_record_frame_ex(frame, validity)` | Same, plus the frame's tracking state (`VRFrameValidityPlain`: headset pose present, per-device flags) for the C3D. `validity = NULL` records exactly like `telemetry_record_frame`. |
| `telemetry_force_upload()` | Uploads the pending JSON chunk now. |
| `telemetry_shutdown()` | Writes the C3D file and stops the uploader. |


### C3D layout

One file per session (`session_<sessionId>.c3d`), written by `telemetry_shutdown`. Gait axes
(X forward, Y left, Z up, converted from OpenXR), millimetres, float32, one analog sample per
frame (`ANALOG:RATE` = `POINT:RATE` = the sampling rate).

**Points (59)**, in this order: `HEAD` (headset), `LFHD` / `RFHD` (derived from the headset pose,
0.15 m apart), 26 left-hand joints `L<joint>`, 26 right-hand joints `R<joint>` (joint names in
`C3DRecorder.cpp`; a controller's position goes to `LFIN` / `RFIN`), and `LWRA`, `LWRB`, `RWRA`,
`RWRB`. A missing point has residual -1. A controller is missing when `isActive` is 0 (or its
position is exactly 0,0,0). `HEAD`, `LFHD` and `RFHD` are missing only when the frame was recorded
with `telemetry_record_frame_ex` and `validity->hmdHasPose == 0`; otherwise they are always
written as present.

**Analog channels (218)**, by index:

| Index | Label | Content |
| --- | --- | --- |
| 0-3 | `HMD_qx` ... `HMD_qw` | Headset orientation (0 when `hmdHasPose == 0` was reported). |
| 4-107 | `L<joint>_q*` | Left-hand joint orientations; the left controller's in `LFIN_q*`. 0 when absent. |
| 108-211 | `R<joint>_q*` | Right hand, likewise (`RFIN_q*`). |
| 212 | `RealTime` | `timestampSec` as float32 (steps of about 0.1 ms after 20 min). |
| 213 | `RealTime_s` | Whole seconds of `timestampSec` (exact). |
| 214 | `RealTime_frac` | The rest, in [0, 1). `RealTime_s + RealTime_frac` = `timestampSec` to about 1e-7 s. |
| 215 | `HMD_flags` | The application's headset tracking bits (`validity->hmdFlags`), or -1 when not reported. |
| 216 | `L_flags` | Left controller bits (`validity->leftFlags`), or -1. |
| 217 | `R_flags` | Right controller bits (`validity->rightFlags`), or -1. |

Channels 0-212 have the labels and indices they had before channels 213-217 were added, so a
reader that looks channels up by label, or by index up to 212, reads files of both versions.
`RealTime` is no longer the last channel. The flag bits are the application's own and are not
interpreted by the library; they are exact as long as they fit in 24 bits. The JSON upload does
not carry the validity.

---

## Repository contents

This repository includes:

- Native C++ core, compilable as a shared library (.so), located in the /cpp folder  
- Android wrapper packaged as an AAR for use in Unity, located in /LibreriaTelemetria  
- Scripts for using the library in Unity  
- Plugin for using the library in Unreal Engine  
- Example of the initial configuration JSON file `initialConfig.json`  
- Scripts and configuration required for compilation  

---

## Requirements

- Android NDK  
- CMake  
- Ninja  
- Gradle  

Note: the current build workflow targets Android arm64-v8a with API level 24.

---

## Native library (.so) compilation

### 1. Configure the NDK path

In this example, the NDK bundled with Unity is used. Adjust the path if your Unity version is different.

```powershell
$NDK = "C:/Program Files/Unity/Hub/Editor/6000.2.5f1/Editor/Data/PlaybackEngines/AndroidPlayer/NDK"
```

### 2. Generate build files with CMake

From the folder containing the source files (.cpp):

```powershell
cmake -S . -B build-arm64 -G "Ninja" ^
  -D CMAKE_BUILD_TYPE=Release ^
  -D CMAKE_TOOLCHAIN_FILE="%NDK%/build/cmake/android.toolchain.cmake" ^
  -D ANDROID_ABI=arm64-v8a ^
  -D ANDROID_PLATFORM=24 ^
  -D ANDROID_STL=c++_static ^
  -D ANDROID_ARM_NEON=ON
```

### 3. Build the library

```powershell
cmake --build build-arm64 --config Release
```

### 4. Build output

The following file will be generated:

```
build-arm64/libreriaTelemetria.so
```

This file must be manually copied to the following path in order to generate the AAR:

```
LibreriaTelemetria/AARTelemetria/src/main/jniLibs/arm64-v8a/
```

---

## AAR generation (Android Library)

### 1. Required files

To correctly generate the AAR, the following files and paths must exist:

In `AARTelemetria/src/`:
- build.gradle  
- AndroidManifest.xml  

In `AARTelemetria/src/main/`:
- consumer-rules.pro  

In `AARTelemetria/src/main/java/io/github/migueldulu/telemetria/`:
- AyudanteHttp.java  

In the project root (`LibreriaTelemetria/`):
- settings.gradle  
- build.gradle (empty, only required if Gradle produces errors)  

### 2. Generate the Gradle wrapper (if it does not exist)

From the `LibreriaTelemetria` folder:

```powershell
gradle wrapper --gradle-version 8.7 --distribution-type all
```

### 3. Build the AAR

```powershell
.\gradlew.bat :AARTelemetria:assembleDebug
```

### 4. Output

The AAR file will be generated in:

```
AARTelemetria/build/outputs/aar/
```
