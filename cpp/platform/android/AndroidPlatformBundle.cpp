// Android PlatformBundle: wires the JNI HTTP transport, the package-relative
// path resolver and the __android_log sink. Static storage; built once at
// telemetry_initialize. The Java references captured by AndroidJavaContext are
// handed to the transport so its first request can obtain a JNIEnv.
#include "platform/Interfaces.h"
#include "android/AndroidLogSink.h"
#include "android/AndroidPathResolver.h"
#include "android/AndroidHttpTransport.h"
#include "android/AndroidJavaContext.h"
#include <android/log.h>

namespace lmc {
PlatformBundle buildPlatformBundle(const char* /*baseDir*/) {
    static AndroidLogSink       log;
    static AndroidPathResolver  paths;
    static AndroidHttpTransport http;
    JavaVM* vm       = android::javaVm();
    jobject activity = android::javaActivity();
    // telemetry_initialize no longer fails here the way the Android-only build
    // used to fail with -2: C3D recording and buffering still work without a Java
    // context. Warn loudly at every initialize that lacks one, instead of failing
    // silently on every chunk later.
    // Logcat directly - the platform sink is only installed after this returns.
    if (!vm || !activity) {
        __android_log_print(ANDROID_LOG_ERROR, "telemetria",
            "Android: no JavaVM/Activity - call telemetry_set_java_context before telemetry_initialize; uploads will fail");
    }
    http.setJavaContext(vm, activity);
    return PlatformBundle{ &http, &paths, &log };
}
} // namespace lmc
