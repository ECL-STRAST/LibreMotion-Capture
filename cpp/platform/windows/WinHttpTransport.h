#pragma once
#include <string>
#include <utility>
#include <vector>
#include "platform/Interfaces.h"
#include "HttpHeaders.h"

namespace lmc {

// buildSupabaseHeaders lives in core/HttpHeaders.h (single canonical builder, R9).

// WinHTTP-backed transport. postJson is compiled only under _WIN32;
// the declaration keeps this header host-includable for the header-builder test.
class WinHttpTransport : public IHttpTransport {
public:
    bool postJson(const std::string& url,
                  const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers) override;
};

} // namespace lmc
