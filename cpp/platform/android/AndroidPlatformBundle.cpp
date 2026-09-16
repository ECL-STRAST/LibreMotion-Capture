// Android PlatformBundle: wires the JNI HTTP transport, the package-relative
// path resolver and the __android_log sink. Static storage; built once at
// telemetry_initialize. The Java references captured by AndroidJavaContext are
// handed to the transport so its first request can obtain a JNIEnv.
#include "platform/Interfaces.h"
#include "android/AndroidLogSink.h"
#include "android/AndroidPathResolver.h"
#include "android/AndroidHttpTransport.h"
#include "android/AndroidJavaContext.h"

namespace lmc {
PlatformBundle buildPlatformBundle(const char* /*baseDir*/) {
    static AndroidLogSink       log;
    static AndroidPathResolver  paths;
    static AndroidHttpTransport http;
    http.setJavaContext(android::javaVm(), android::javaActivity());
    return PlatformBundle{ &http, &paths, &log };
}
} // namespace lmc
