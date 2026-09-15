#pragma once
#include <string>
#include <utility>
#include <vector>
#include "platform/Interfaces.h"

namespace lmc {

// Exact header set/order emitted by the legacy JSONUploader::uploadJson:
// Content-Type, apikey, Authorization, Prefer. Parity-locked by tests/test_http_headers.cpp.
std::vector<std::pair<std::string, std::string>> buildSupabaseHeaders(const std::string& apiKey);

// WinHTTP-backed transport. postJson is compiled only under _WIN32;
// the declaration keeps this header host-includable for the header-builder test.
class WinHttpTransport : public IHttpTransport {
public:
    bool postJson(const std::string& url,
                  const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers) override;
};

} // namespace lmc
