#include "Log.h"
#include <cstdarg>
#include <cstdio>

namespace lmc {

// Default sink writes to stderr; used before a platform installs its own and in tests.
class StderrSink : public ILogSink {
public:
    void log(int level, const char* msg) override {
        const char* tag = (level >= kError) ? "ERROR" : (level == kWarn ? "WARN" : "INFO");
        std::fprintf(stderr, "[telemetria][%s] %s\n", tag, msg);
    }
};

static ILogSink* g_sink = nullptr;

static ILogSink* sink() {
    static StderrSink fallback;
    return g_sink ? g_sink : &fallback;
}

namespace Log {
void set(ILogSink* s) { g_sink = s; }

static void emit(int level, const char* fmt, va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    sink()->log(level, buf);
}

void info(const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kInfo, fmt, a); va_end(a); }
void warn(const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kWarn, fmt, a); va_end(a); }
void err (const char* fmt, ...) { va_list a; va_start(a, fmt); emit(kError, fmt, a); va_end(a); }
} // namespace Log

} // namespace lmc
