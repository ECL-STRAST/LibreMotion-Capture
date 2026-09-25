#pragma once
#include <string>

// Configuration used by GestorTelemetria and JSONUploader
// This struct is filled partly from the engine (sessionId, deviceInfo)
// and partly from the JSON file initialConfig.json (endpoint, apiKey,
// framesPerFile and feature flags).
struct UploaderConfig {
    std::string endpointUrl;  // Loaded from initialConfig.json
    std::string apiKey;       // Loaded from initialConfig.json
    std::string sessionId;    // Returned by initialize (Unity/UE)
    std::string deviceInfo;   // Returned by initialize (Unity/UE)
    int framesPerFile = 150;
    // Feature flags (DEFAULT = false if missing in JSON).
    bool handTracking  = false;
    bool primaryButton = false;
    bool secondaryButton = false;
    bool grip = false;
    bool trigger = false;
    bool joystick = false;
    // Also write every JSON chunk to disk (initialConfig.json "saveJsonLocally").
    bool saveJsonLocally = false;
    // Set by telemetry_initialize when saveJsonLocally is on: the session's C3D path
    // without ".c3d". Chunk n is written to <prefix>_chunk<nnnn>.json. Empty = off.
    std::string jsonOutputPrefix;
};

// Plain configuration struct sent from Unity/Unreal to the C API.
// This struct only contains the fields that make sense at engine side,
// and uses const char* to be easily marshalled from C# / Blueprints.
struct TelemetryConfigPlain {
    const char* sessionId;     // UTF-8
    const char* deviceInfo;    // UTF-8
    const char* baseDir;       // UTF-8; optional. Base directory for config + C3D output (Windows/engine-provided). May be null.
};