#include "android/AndroidLogSink.h"
#include <android/log.h>
namespace lmc {
void AndroidLogSink::log(int level, const char* msg) {
    const android_LogPriority prio =
        (level >= kError) ? ANDROID_LOG_ERROR :
        (level == kWarn)  ? ANDROID_LOG_WARN  :
                            ANDROID_LOG_INFO;
    __android_log_print(prio, "telemetria", "%s", msg);
}
} // namespace lmc
