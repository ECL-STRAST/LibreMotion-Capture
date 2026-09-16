#pragma once
#include "platform/Interfaces.h"
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
} // namespace lmc
