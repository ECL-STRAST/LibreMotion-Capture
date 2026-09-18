#pragma once
#include <string>
#include <utility>
#include <vector>
#include "platform/Interfaces.h"
#include "HttpHeaders.h"

namespace lmc {

// buildSupabaseHeaders lives in core/HttpHeaders.h (single canonical builder, R9).

// Request target for WinHttpOpenRequest: the cracked URL path plus the query that
// WinHttpCrackUrl splits out into lpszExtraInfo. Kept as a pure std::wstring
// helper outside the _WIN32 guard so the path/query behaviour is unit-tested on
// every host, since postJson itself only compiles on Windows.
std::wstring winHttpRequestTarget(const std::wstring& urlPath, const std::wstring& extraInfo);

// WinHTTP-backed transport. postJson is compiled only under _WIN32; the
// declaration keeps the header host-includable for these tests.
class WinHttpTransport : public IHttpTransport {
public:
    bool postJson(const std::string& url,
                  const std::string& body,
                  const std::vector<std::pair<std::string, std::string>>& headers) override;
};

} // namespace lmc
