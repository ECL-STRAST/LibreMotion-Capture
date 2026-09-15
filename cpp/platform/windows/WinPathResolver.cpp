#include "windows/WinPathResolver.h"
#include "platform/Log.h"
namespace lmc {
bool WinPathResolver::configFilePath(std::string& out) { if (base_.empty()) return false; out = joinPath(base_, "initialConfig.json"); return true; }
bool WinPathResolver::c3dFilePath(const std::string& sessionId, std::string& out) { if (base_.empty()) return false; out = joinPath(base_, "session_" + sanitizeSessionId(sessionId) + ".c3d"); return true; }
} // namespace lmc
