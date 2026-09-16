#pragma once
#include <jni.h>

namespace lmc {
namespace android {

// Global Java state captured by JNI_OnLoad and telemetry_set_java_context.
// setJavaContext installs/refreshes the (global) JavaVM + Activity references;
// javaVm()/javaActivity() expose them to the rest of the Android platform layer.
void    setJavaContext(JavaVM* vm, jobject activity);
JavaVM* javaVm();
jobject javaActivity();

} // namespace android
} // namespace lmc
