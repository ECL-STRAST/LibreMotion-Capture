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
    const std::string primary = dir + "/initialConfig.json"; // /sdcard/...
    if (fileExists(primary)) { out = primary; return true; }
    // R7: primary missing -> fall back to the /storage/emulated/0 variant so that
    // readFileToString on the returned path succeeds wherever the retired
    // configReader read-time retry (/sdcard -> /storage/emulated/0) succeeded.
    const std::string sdcard = "/sdcard/";
    if (dir.rfind(sdcard, 0) == 0) {
        out = std::string("/storage/emulated/0/") + dir.substr(sdcard.size()) + "/initialConfig.json";
        return true;
    }
    out = primary;
    return true;
}

bool AndroidPathResolver::c3dFilePath(const std::string& sessionId, std::string& out) {
    std::string dir;
    if (!getAppFilesDir(dir)) return false;
    out = joinPath(dir, "session_" + sanitizeSessionId(sessionId) + ".c3d");
    return true;
}

} // namespace lmc
