#include "HttpHeaders.h"

namespace lmc {

std::vector<std::pair<std::string, std::string>> buildSupabaseHeaders(const std::string& apiKey) {
    return { {"Content-Type", "application/json"},
             {"apikey", apiKey},
             {"Authorization", std::string("Bearer ") + apiKey},
             {"Prefer", "return=representation"} };
}

} // namespace lmc
