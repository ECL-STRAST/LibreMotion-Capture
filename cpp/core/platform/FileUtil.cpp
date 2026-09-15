#include "platform/Log.h"
#include <fstream>
#include <sstream>
#include <cctype>

namespace lmc {
bool readFileToString(const std::string& path, std::string& outText) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return false;
    std::ostringstream ss; ss << in.rdbuf();
    outText = ss.str();
    return true;
}
std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}
std::string sanitizeSessionId(const std::string& raw) {
    std::string s = raw;
    for (char& c : s)
        if (!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='-'||c=='_')) c = '_';
    if (s.empty()) s = "unknown";
    return s;
}
} // namespace lmc
