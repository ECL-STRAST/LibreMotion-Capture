// Windows PlatformBundle: wires the WinHTTP transport, the Win32 path resolver
// and the console log sink. Static storage; built once at telemetry_initialize.
#include "platform/Interfaces.h"
#include "windows/WinLogSink.h"
#include "windows/WinPathResolver.h"
#include "windows/WinHttpTransport.h"
#include <string>
#include <windows.h>

namespace lmc {
namespace {
// Directory that contains this executable, used when the engine does not pass a
// base dir. Falls back to "." if the module path is unavailable.
std::string executableDir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::string(".");
    std::string p(buf, n);
    size_t slash = p.find_last_of("\\/");
    if (slash == std::string::npos) return std::string(".");
    return p.substr(0, slash);
}
} // namespace

PlatformBundle buildPlatformBundle(const char* baseDir) {
    const std::string base = (baseDir && baseDir[0]) ? std::string(baseDir) : executableDir();
    static WinLogSink      log;
    static WinHttpTransport http;
    static WinPathResolver  paths(base);
    // The statics are constructed on the first call only, so a second
    // telemetry_initialize with a different baseDir would otherwise keep writing
    // the config/C3D paths under the first one.
    paths.setBaseDir(base);
    return PlatformBundle{ &http, &paths, &log };
}
} // namespace lmc
