#pragma once
#include <string>
#include "TelemetryTypes.h"

namespace lmc {
void     configSetDefaults(UploaderConfig& cfg);
bool     configParseJson(const std::string& text, UploaderConfig& cfg);
int      configParseFrameRate(const std::string& text, int fallback = 60);
unsigned configFeatureFlagsBitmask(const UploaderConfig& cfg);
// False when endpoint/apiKey are empty or still the built-in placeholders, i.e. no
// usable upload target was configured.
bool     configUploadsEnabled(const UploaderConfig& cfg);
} // namespace lmc
