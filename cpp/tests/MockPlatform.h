#pragma once
#include "platform/Interfaces.h"
#include "windows/WinPathResolver.h"
#include <vector>
#include <string>
namespace lmc {
class MockHttp : public IHttpTransport {
public:
    struct Post { std::string url, body; size_t headerCount; };
    std::vector<Post> posts;
    bool okResult = true;
    bool postJson(const std::string& url, const std::string& body,
                  const std::vector<std::pair<std::string,std::string>>& h) override {
        posts.push_back({url, body, h.size()}); return okResult;
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
} // namespace lmc
