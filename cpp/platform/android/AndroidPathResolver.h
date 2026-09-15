#pragma once
#include <string>
#include "platform/Interfaces.h"
namespace lmc {
class AndroidPathResolver : public IPathResolver {
public:
    bool configFilePath(std::string& out) override;
    bool c3dFilePath(const std::string& sessionId, std::string& out) override;
};
} // namespace lmc
