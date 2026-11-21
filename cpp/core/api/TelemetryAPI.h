#pragma once
#include "VRTypes.h"
#include "TelemetryTypes.h"

// JNI types are an Android-only concern: <jni.h> and the java-context entry
// point live exclusively on Android. The rest of the C ABI is portable.
#ifdef __ANDROID__
#include <jni.h>
#endif

// Export macro:
//  - Windows (MSVC/MinGW): dllexport when we are building the code that *defines* the
//    entry points (TELEMETRIA_EXPORTS, set on the SHARED telemetria target and, on
//    Windows, on the STATIC telemetria_core that also holds a copy: a dllimport
//    declaration there would mean "definition of dllimport function" = MSVC C2491),
//    and dllimport for consumers that link the import lib of telemetria.dll.
//  - everywhere else (Linux/Android): default visibility so the C ABI stays
//    visible outside the .so.
#ifndef TELEMETRIA_API
#  if defined(_WIN32)
#    if defined(TELEMETRIA_EXPORTS)
#      define TELEMETRIA_API __declspec(dllexport)
#    else
#      define TELEMETRIA_API __declspec(dllimport)
#    endif
#  else
#    define TELEMETRIA_API __attribute__((visibility("default")))
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ANDROID__
// Android-only: must be called before telemetry_initialize so that the transport
// can dynamically load the classes from the AAR.
TELEMETRIA_API void telemetry_set_java_context(JavaVM* vm, jobject activity);
#endif

// Sets the sampling rate used by the next telemetry_initialize, overriding `frameRate`
// from initialConfig.json (e.g. with the headset's display refresh rate).
// - hz in [1, 240]: override `frameRate`.
// - hz == 0       : clear the override; `frameRate` from the config (or 60) is used again.
// - anything else : ignored (the previous value, override or none, is kept).
// Call it before telemetry_initialize; a call while a session is running only affects the
// next session. The value persists across telemetry_shutdown/telemetry_initialize until it
// is changed again, so an app that runs several sessions sets it once.
TELEMETRIA_API void telemetry_set_frame_rate(int hz);

// Initializes the telemetry manager and HTTP uploader.
// - cfg: basic configuration coming from Unity/Unreal (sessionId, deviceInfo).
// Return:
//   > 0 : frame rate that the caller should use for sampling (1 to 240). It is the
//         telemetry_set_frame_rate override if one is set, otherwise `frameRate` from
//         initialConfig.json, otherwise 60; the same value goes into the C3D header.
//   < 0 : negative error code.
TELEMETRIA_API int  telemetry_initialize(const TelemetryConfigPlain* cfg);

// Records one frame of VR telemetry data.
// This function is expected to be called frequently (e.g. every frame or at a fixed rate). It will:
//   - append the frame to the C3D recorder buffer;
//   - append the frame to the JSON buffer in GestorTelemetria.
TELEMETRIA_API void telemetry_record_frame(const VRFrameDataPlain* frame);

// Same as telemetry_record_frame, plus the frame's tracking state (VRTypes.h):
//   - validity->hmdHasPose == 0: the headset has no pose; its C3D points (HEAD, LFHD,
//     RFHD) are written as missing (residual -1) and its quaternion channels as 0;
//   - hmdFlags / leftFlags / rightFlags: stored verbatim in the analog channels
//     HMD_flags, L_flags, R_flags (exact up to 2^24).
// validity == NULL records exactly like telemetry_record_frame (flag channels -1).
// Frames of one session may mix both calls. The JSON upload is not affected.
TELEMETRIA_API void telemetry_record_frame_ex(const VRFrameDataPlain* frame,
                                              const VRFrameValidityPlain* validity);

// Forces upload of any telemetry JSON chunks left.
// This can be called when the app is about to go to background to avoid losing any data
TELEMETRIA_API void telemetry_force_upload();

// Shuts down all telemetry components and releases resources.
// This will:
//   - finalize the C3D recorder and write the .c3d file if needed;
//   - flush and stop the JSON uploader worker;
//   - detach the platform log sink.
TELEMETRIA_API void telemetry_shutdown();

// Returns a bitmask of feature flags read from initialConfig.json.
// Bit layout:
//   bit 0: hand tracking enabled
//   bit 1: primary button fields enabled
//   bit 2: secondary button fields enabled
//   bit 3: grip value enabled
//   bit 4: trigger value enabled
//   bit 5: joystick axes enabled
// The engine can use this to know which fields are actually configured.
TELEMETRIA_API unsigned telemetry_get_feature_flags();

#ifdef __cplusplus
}
#endif
