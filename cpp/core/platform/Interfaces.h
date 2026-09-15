#pragma once
#include <string>
#include <utility>
#include <vector>

namespace lmc {

enum LogLevel { kInfo = 0, kWarn = 1, kError = 2 };

struct ILogSink {
    virtual ~ILogSink() = default;
    virtual void log(int level, const char* msg) = 0;
};

// Sends one already-serialized JSON body to the configured endpoint.
// Returns true only on HTTP 2xx. Must be callable from a background thread.
struct IHttpTransport {
    virtual ~IHttpTransport() = default;
    virtual bool postJson(const std::string& url,
                          const std::string& body,
                          const std::vector<std::pair<std::string, std::string>>& headers) = 0;
};

// Locates the initial config file and the per-session C3D output path.
struct IPathResolver {
    virtual ~IPathResolver() = default;
    virtual bool configFilePath(std::string& out) = 0;
    virtual bool c3dFilePath(const std::string& sessionId, std::string& out) = 0;
};

// Bundle of the three platform impls, built once at initialize().
struct PlatformBundle {
    IHttpTransport* http   = nullptr;   // owned by the platform layer (static storage)
    IPathResolver*  paths  = nullptr;
    ILogSink*       log    = nullptr;
};

// Implemented by each platform's *PlatformBundle.cpp; returns static-storage impls.
PlatformBundle buildPlatformBundle(const char* baseDir);

} // namespace lmc
