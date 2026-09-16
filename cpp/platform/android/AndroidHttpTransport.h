#pragma once
#include <jni.h>
#include <string>
#include <utility>
#include <vector>
#include "platform/Interfaces.h"

namespace lmc {

// JNI bridge to the Java helper AyudanteHttp.makeRequest.
// This class does NOT perform HTTP by itself; it just bridges between
// C++ and Java, passing method, URL, body and headers to Java.
// postJson returns true only on success (no error marker, non-null response).
class AndroidHttpTransport : public IHttpTransport {
public:
    // Stores the Java VM pointer and the global Activity reference.
    // Required later to obtain a JNIEnv* and to get the Activity class loader
    // used to load the AyudanteHttp class. Kept separate from postJson so the
    // API init can inject the Java refs before the first request.
    void setJavaContext(JavaVM* vm, jobject activityGlobalRef);

    // Sends a JSON payload to the given endpoint over the Java helper (POST).
    // Returns false if the Java context is missing or if the JNI call fails.
    bool postJson(const std::string& url,
                  const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers) override;

private:
    // Cached Java VM pointer, used to attach/detach threads and obtain JNIEnv*.
    JavaVM* vm_ = nullptr;
    jobject activity_ = nullptr; // global ref, not owned
};

} // namespace lmc
