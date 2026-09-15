#pragma once
#include <string>
#include "platform/Interfaces.h"
namespace lmc {
class WinPathResolver : public IPathResolver {
public:
    explicit WinPathResolver(std::string baseDir) : base_(std::move(baseDir)) {}
    bool configFilePath(std::string& out) override;
    bool c3dFilePath(const std::string& sessionId, std::string& out) override;
private:
    std::string base_;
};
} // namespace lmc
