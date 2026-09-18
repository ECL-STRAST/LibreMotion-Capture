#pragma once
#include "platform/Interfaces.h"
#include "platform/Log.h"
#include "windows/WinPathResolver.h"
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
namespace lmc {
// Holds a fixed number of transport calls inside postJson so a test can keep the
// upload worker busy while the producer keeps enqueueing chunks.
class PostGate {
public:
    void hold(int n);
    void enter();
    bool waitUntilInside();
    void release();
private:
    std::mutex mtx_;
    std::condition_variable cv_;
    int toHold_ = 0;
    int inside_ = 0;
};

class MockHttp : public IHttpTransport {
public:
    struct Post { std::string url, body; size_t headerCount; };
    std::vector<Post> posts;
    bool okResult = true;
    PostGate gate;
    bool postJson(const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string,std::string>>& h) override {
        posts.push_back({url, body, h.size()});
        gate.enter();
        return okResult;
    }
};

// Creates a unique temp directory (already existing on disk) and exposes a
// WinPathResolver rooted there. c3dPath() reports the path the recorder is
// expected to resolve for sessionId "s1", i.e. <dir>/session_s1.c3d.
class WinTempDir {
public:
    WinTempDir();
    IPathResolver* paths();
    std::string c3dPath();
    const std::string& dir() const { return dir_; }
private:
    std::string dir_;
    WinPathResolver resolver_;
};

// Records what the production code logged, so a test can assert on the
// reported outcome instead of only on return values.
class CaptureLogSink : public ILogSink {
public:
    std::vector<std::string> errors;
    std::vector<std::string> infos;
    void log(int level, const char* msg) override {
        std::string s(msg ? msg : "");
        (level == kError ? errors : infos).push_back(s);
    }
    bool errorMentions(const std::string& needle) const;
    bool infoMentions(const std::string& needle) const;
};

// Routes core logging into a CaptureLogSink for the enclosing scope.
class ScopedCaptureLog {
public:
    ScopedCaptureLog();
    ~ScopedCaptureLog();
    const CaptureLogSink* sink() const { return &sink_; }
private:
    CaptureLogSink sink_;
};
} // namespace lmc
