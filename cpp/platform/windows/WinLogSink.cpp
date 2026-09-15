#include "windows/WinLogSink.h"
#include <windows.h>
#include <cstdio>
#include <string>
namespace lmc {
void WinLogSink::log(int, const char* msg) {
    std::string s = std::string("[telemetria] ") + msg + "\n";
    OutputDebugStringA(s.c_str());
#if !defined(NDEBUG) || defined(TELEMETRIA_CONSOLE)
    std::fputs(s.c_str(), stderr);
#endif
}
} // namespace lmc
