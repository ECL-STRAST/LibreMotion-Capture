#pragma once
#include <string>
#include <utility>
#include <vector>

namespace lmc {

// Exact header set/order emitted by the legacy JSONUploader::uploadJson:
// Content-Type, apikey, Authorization, Prefer. Parity-locked by tests/test_http_headers.cpp.
std::vector<std::pair<std::string, std::string>> buildSupabaseHeaders(const std::string& apiKey);

} // namespace lmc
