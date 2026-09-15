#pragma once
#include <string>
#include "TelemetryTypes.h"

namespace lmc {
void     configSetDefaults(UploaderConfig& cfg);
bool     configParseJson(const std::string& text, UploaderConfig& cfg);
int      configParseFrameRate(const std::string& text, int fallback = 60);
unsigned configFeatureFlagsBitmask(const UploaderConfig& cfg);
} // namespace lmc
