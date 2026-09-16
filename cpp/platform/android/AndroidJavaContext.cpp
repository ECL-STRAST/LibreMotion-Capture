#include "android/AndroidJavaContext.h"
#include <android/log.h>
#include <mutex>

#define LOG_TAG "telemetria"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Minimal global state to keep eveerything as simple as posssible and smooth performance on the app it will be used
static JavaVM* g_vm = nullptr;
static jobject g_activity = nullptr;
static std::mutex g_mutex;

namespace lmc {
namespace android {

void setJavaContext(JavaVM* vm, jobject activity) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // if VM provided, override the global one
    if (vm) g_vm = vm;
    // If we already have a global Activity ref, delete it first.
    if (g_activity) {
        JNIEnv* env = nullptr;
        if (g_vm && g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK && env) {
            env->DeleteGlobalRef(g_activity);
        }
        g_activity = nullptr;
    }
    // Create new global ref from the passed Activity, if any.
    if (activity) {
        if (!g_vm) {
            LOGE("telemetry_set_java_context called without JavaVM");
            return;
        }
        JNIEnv* env = nullptr;
        if (g_vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
            LOGE("GetEnv failed");
            return;
        }
        g_activity = env->NewGlobalRef(activity);
    }
    LOGI("Java context set");
}

JavaVM* javaVm() { return g_vm; }

jobject javaActivity() { return g_activity; }

} // namespace android
} // namespace lmc

// Capture JavaVM when the library is loaded (this allow threads to attach later and obtain JNIEnv)
jint JNI_OnLoad(JavaVM* vm, void*) {
    // Captura JavaVM en carga de la libreria
    std::lock_guard<std::mutex> lock(g_mutex);
    g_vm = vm;
    return JNI_VERSION_1_6;
}

extern "C" {

// Thin exported wrapper: the engine-facing C ABI entry point delegates to the
// platform-global setter above.
__attribute__((visibility("default")))
void telemetry_set_java_context(JavaVM* vm, jobject activity) {
    lmc::android::setJavaContext(vm, activity);
}

} // extern "C"
