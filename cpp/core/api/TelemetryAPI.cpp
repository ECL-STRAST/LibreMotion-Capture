// Cross-platform C API entry points.
// This file is platform-agnostic: it obtains the log/path/http implementations
// through lmc::buildPlatformBundle() and talks to the rest of the core via
// lmc::Log. The Android-only Java-context entry point lives in
// platform/android/AndroidJavaContext.cpp, so there is no <jni.h> here.
#include "TelemetryAPI.h"
#include "TelemetryManager.h"
#include "C3DRecorder.h"
#include "config/ConfigParser.h"
#include "platform/Interfaces.h"
#include "platform/Log.h"
#include <mutex>

// Minimal global state to keep everything as simple as possible and to keep
// performance smooth on the app where it is used.
static std::mutex g_mutex;

// Singletons for the internal components managed through the API.
static GestorTelemetria g_gestor;
static C3DRecorder      g_c3d;
// Cached feature flags to be exposed through telemetry_get_feature_flags().
static unsigned g_featureFlags = 0;

extern "C" {

int telemetry_initialize(const TelemetryConfigPlain* cfg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // If no config was provided, fail.
    if (!cfg) return -1;

    // Resolve the platform implementations and install the platform log sink.
    lmc::PlatformBundle pb = lmc::buildPlatformBundle(cfg->baseDir);
    lmc::Log::set(pb.log);

    // Build the uploader configuration: defaults, then overlay the config file,
    // then the per-session fields coming from the engine.
    UploaderConfig ucfg;
    lmc::configSetDefaults(ucfg);

    std::string configText;
    std::string cfgPath;
    bool haveConfig = false;
    if (pb.paths && pb.paths->configFilePath(cfgPath)) {
        if (lmc::readFileToString(cfgPath, configText)) {
            haveConfig = true;
            lmc::configParseJson(configText, ucfg);
        }
    }
    if (!haveConfig) {
        lmc::Log::info("Config file not found or unreadable; using defaults.");
    }

    ucfg.sessionId  = cfg->sessionId  ? cfg->sessionId  : "";
    ucfg.deviceInfo = cfg->deviceInfo ? cfg->deviceInfo : "";

    lmc::Log::info("config flags: hand=%d primary=%d secondary=%d grip=%d trigger=%d joystick=%d",
                   (int)ucfg.handTracking, (int)ucfg.primaryButton, (int)ucfg.secondaryButton,
                   (int)ucfg.grip, (int)ucfg.trigger, (int)ucfg.joystick);

    lmc::Log::info("config final: endpointUrl='%s', apiKey.len=%d, framesPerFile=%d, sessionId='%s', deviceInfo.len=%d",
                   ucfg.endpointUrl.c_str(), (int)ucfg.apiKey.size(), ucfg.framesPerFile,
                   ucfg.sessionId.c_str(), (int)ucfg.deviceInfo.size());

    // R10: if the Supabase endpoint/api key are missing, disable uploads once at
    // init (instead of skipping on every upload). C3D recording and the rest of
    // the pipeline keep working with a null transport.
    lmc::IHttpTransport* transport = pb.http;
    if (ucfg.endpointUrl.empty() || ucfg.apiKey.empty()) {
        lmc::Log::err("Missing supabase config (endpoint/apiKey); uploads disabled");
        transport = nullptr;
    }

    // Initialize the telemetry manager (buffering + background uploads).
    if (!g_gestor.initialize(ucfg, transport)) {
        lmc::Log::err("Gestor initialize failed");
        return -4;
    }
    // Cache feature flags for the engine (telemetry_get_feature_flags).
    g_featureFlags = lmc::configFeatureFlagsBitmask(ucfg);

    lmc::Log::info("telemetry initialized");

    // Sampling rate: default 60, overridden only by a readable config file.
    int frameRate = 60;
    if (haveConfig) frameRate = lmc::configParseFrameRate(configText, 60);
    // Keep the frame rate inside a safe [1, 240] range; otherwise fall back to 60.
    if (frameRate < 1 || frameRate > 240) frameRate = 60;

    // Initialize the C3D recorder. Even if it fails, HTTP telemetry still works.
    if (!g_c3d.C3Dinitialize(ucfg, frameRate, pb.paths)) {
        lmc::Log::err("C3DRecorder initialize failed; continuing without C3D output");
        // No crash because we still have HTTP telemetry.
    }
    return frameRate;
}

unsigned telemetry_get_feature_flags() {
    // Simple read of a 32-bit value.
    return g_featureFlags;
}

void telemetry_record_frame(const VRFrameDataPlain* frame) {
    if (!frame) return;
    // Append the frame to the C3D buffer.
    g_c3d.C3DrecordFrame(*frame);
    // Append the frame to the JSON buffer.
    g_gestor.recordFrame(*frame);
}

void telemetry_force_upload() {
    // Ask GestorTelemetria to close the current buffer and enqueue it for upload,
    // even if framesPerFile was not reached yet.
    g_gestor.flushAndUpload();
}

void telemetry_shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Finalize the C3D file.
    g_c3d.C3Dfinalize();
    // Flush JSON chunks and stop the worker thread.
    g_gestor.shutdown();
    lmc::Log::info("telemetry shutdown complete");
    // Detach the platform sink so no further logging reaches a torn-down sink.
    lmc::Log::set(nullptr);
}

} // extern "C"
