#pragma once
#include <string>
#include "Interfaces.h"

namespace lmc {
namespace Log {
    void set(ILogSink* sink);                 // install sink (nullptr => default stderr sink)
    void info(const char* fmt, ...);
    void warn(const char* fmt, ...);
    void err (const char* fmt, ...);
}
bool readFileToString(const std::string& path, std::string& outText);
bool fileExists(const std::string& path);
// Writes text to path, replacing any existing file. False if it could not be written.
bool writeStringToFile(const std::string& path, const std::string& text);
std::string joinPath(const std::string& a, const std::string& b);
std::string sanitizeSessionId(const std::string& raw);
} // namespace lmc
