#include "config/ConfigParser.h"
#include <cctype>

namespace {
// --- moved verbatim from cpp/configReader.cpp (anonymous namespace) ---
// Looks for: "key" : "value" (with optional spaces) and extracts value.
static bool extractJsonString(const std::string& text, const std::string& key, std::string& out) {
    const std::string q = "\"" + key + "\"";
    size_t p = text.find(q);
    if (p == std::string::npos) return false;

    p = text.find(':', p + q.size());
    if (p == std::string::npos) return false;

    // Skip colon and whitespace
    while (p < text.size() && (text[p] == ':' || text[p] == ' ' || text[p] == '\t' || text[p] == '\r' || text[p] == '\n')) ++p;
    if (p >= text.size() || text[p] != '"') return false;
    ++p; // now inside the string
    std::string val;
    while (p < text.size()) {
        char c = text[p++];
        if (c == '\\') {  // Minimal escape handling: copy next char as is
            if (p < text.size()) {
                char n = text[p++];
                val.push_back(n);
            }
        } else if (c == '"') {
            break;
        } else {
            val.push_back(c);
        }
    }
    out = val;
    return !out.empty();
}

// Looks for: "key" : <integer> and stores it in out.
static bool extractJsonInt(const std::string& text, const std::string& key, int& out) {
    const std::string kq = "\"" + key + "\"";
    size_t p = text.find(kq);
    if (p == std::string::npos) return false;
    p = text.find(':', p + kq.size());
    if (p == std::string::npos) return false;

    // saltar espacios
    while (p < text.size() && (text[p] == ':' || std::isspace(static_cast<unsigned char>(text[p])))) ++p;
    if (p >= text.size()) return false;

    // leer numero
    bool neg = false;
    if (text[p] == '-') { neg = true; ++p; }
    if (p >= text.size() || !std::isdigit(static_cast<unsigned char>(text[p]))) return false;

    long val = 0;
    while (p < text.size() && std::isdigit(static_cast<unsigned char>(text[p]))) {
        val = val * 10 + (text[p] - '0');
        ++p;
    }
    if (neg) val = -val;
    out = static_cast<int>(val);
    return true;
}

// Looks for: "key" : true/false (lowercase)
static bool extractJsonBool(const std::string& text, const std::string& key, bool& out) {
    const std::string kq = "\"" + key + "\"";
    size_t p = text.find(kq);
    if (p == std::string::npos) return false;
    p = text.find(':', p + kq.size());
    if (p == std::string::npos) return false;
    while (p < text.size() && (text[p] == ':' || std::isspace((unsigned char)text[p]))) ++p;
    if (p >= text.size()) return false;

    // aceptamos true/false (lowercase)
    if (text.compare(p, 4, "true") == 0)  { out = true;  return true; }
    if (text.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}
} // namespace

namespace lmc {
static constexpr const char* kDefaultEndpoint     = "http://localhost:1414";
static constexpr const char* kDefaultApiKey       = "non_existant";
static constexpr int         kDefaultFramesPerFile = 200;
static constexpr int         kDefaultFrameRate     = 60;

void configSetDefaults(UploaderConfig& c) {
    c.endpointUrl = kDefaultEndpoint; c.apiKey = kDefaultApiKey;
    c.framesPerFile = kDefaultFramesPerFile;
    c.handTracking = c.primaryButton = c.secondaryButton = false;
    c.grip = c.trigger = c.joystick = false;
}
bool configParseJson(const std::string& t, UploaderConfig& c) {
    std::string s = c.endpointUrl, k = c.apiKey; int vi; bool vb;
    if (extractJsonString(t, "endpoint", s) && !s.empty()) c.endpointUrl = s;
    if (extractJsonString(t, "apiKey", k)   && !k.empty()) c.apiKey = k;
    if (extractJsonInt(t, "framesPerFile", vi) && vi > 0)  c.framesPerFile = vi;
    if (extractJsonBool(t, "handTracking", vb))    c.handTracking = vb;
    if (extractJsonBool(t, "primaryButton", vb))   c.primaryButton = vb;
    if (extractJsonBool(t, "secondaryButton", vb)) c.secondaryButton = vb;
    if (extractJsonBool(t, "grip", vb))    c.grip = vb;
    if (extractJsonBool(t, "trigger", vb)) c.trigger = vb;
    if (extractJsonBool(t, "joystick", vb))c.joystick = vb;
    return true;
}
int configParseFrameRate(const std::string& t, int fallback) {
    int vi;
    if (extractJsonInt(t, "frameRate", vi) && vi >= 1 && vi <= 240) return vi;
    return fallback;
}
unsigned configFeatureFlagsBitmask(const UploaderConfig& c) {
    unsigned m = 0;
    if (c.handTracking)   m |= 1u << 0;
    if (c.primaryButton)  m |= 1u << 1;
    if (c.secondaryButton)m |= 1u << 2;
    if (c.grip)           m |= 1u << 3;
    if (c.trigger)        m |= 1u << 4;
    if (c.joystick)       m |= 1u << 5;
    return m;
}
bool configUploadsEnabled(const UploaderConfig& c) {
    if (c.endpointUrl.empty() || c.apiKey.empty()) return false;
    // Without an initialConfig.json the placeholders are still in place. Treating
    // them as a real target made every chunk try http://localhost:1414 and log
    // "Upload FAILED" for the whole session instead of disabling uploads once.
    return c.endpointUrl != kDefaultEndpoint && c.apiKey != kDefaultApiKey;
}
} // namespace lmc
