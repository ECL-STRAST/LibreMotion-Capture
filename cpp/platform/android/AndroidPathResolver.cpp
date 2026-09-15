#include "android/AndroidPathResolver.h"
#include "platform/Log.h"
#include <fstream>
#include <string>

namespace lmc {
namespace {

// The package name is obtained from /proc/self/cmdline. On Android, the process name
// is usually the same as the app package name.
bool getPackageNameFromProc(std::string& outPkg) {
    std::ifstream f("/proc/self/cmdline", std::ios::in | std::ios::binary);
    if (!f) return false;
    std::string buf;
    // First token up to '\0'
    std::getline(f, buf, '\0');
    if (buf.empty()) return false;

    // Remove optional suffix after ':', e.g. "com.app:unity"
    size_t colon = buf.find(':');
    if (colon != std::string::npos) buf.resize(colon);

    outPkg = buf;
    return !outPkg.empty();
}

// Build the app-specific external files dir: /sdcard/Android/data/<package>/files
bool getAppFilesDir(std::string& outDir) {
    std::string pkg;
    if (!getPackageNameFromProc(pkg)) {
        Log::err("AndroidPathResolver: could not get package from /proc/self/cmdline");
        return false;
    }
    outDir = std::string("/sdcard/Android/data/") + pkg + "/files";
    return true;
}

} // namespace

bool AndroidPathResolver::configFilePath(std::string& out) {
    std::string dir;
    if (!getAppFilesDir(dir)) return false;
    out = dir + "/initialConfig.json";
    return true;
}

bool AndroidPathResolver::c3dFilePath(const std::string& sessionId, std::string& out) {
    std::string dir;
    if (!getAppFilesDir(dir)) return false;
    out = joinPath(dir, "session_" + sanitizeSessionId(sessionId) + ".c3d");
    return true;
}

} // namespace lmc
