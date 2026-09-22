#include <gtest/gtest.h>
#include "config/ConfigParser.h"
#include "TelemetryTypes.h"

using namespace lmc;

TEST(ConfigParser, DefaultsApplied) {
    UploaderConfig cfg;
    configSetDefaults(cfg);
    EXPECT_EQ(cfg.endpointUrl, "http://localhost:1414");
    EXPECT_EQ(cfg.apiKey, "non_existant");
    EXPECT_EQ(cfg.framesPerFile, 200);
    EXPECT_FALSE(cfg.handTracking);
}

TEST(ConfigParser, OverridesAndFlags) {
    const char* json = R"({
      "endpoint":"https://x/tb","apiKey":"K","framesPerFile":150,
      "handTracking":true,"primaryButton":true,"secondaryButton":true,
      "grip":true,"trigger":true,"joystick":true
    })";
    UploaderConfig cfg; configSetDefaults(cfg);
    ASSERT_TRUE(configParseJson(json, cfg));
    EXPECT_EQ(cfg.endpointUrl, "https://x/tb");
    EXPECT_EQ(cfg.apiKey, "K");
    EXPECT_EQ(cfg.framesPerFile, 150);
    EXPECT_TRUE(cfg.handTracking);
    EXPECT_EQ(configFeatureFlagsBitmask(cfg), 0b111111u);
}

TEST(ConfigParser, FrameRateClampAndFallback) {
    EXPECT_EQ(configParseFrameRate(R"({"frameRate":72})", 60), 72);
    EXPECT_EQ(configParseFrameRate(R"({"frameRate":900})", 60), 60);
    EXPECT_EQ(configParseFrameRate(R"({})", 60), 60);
}

TEST(ConfigParser, UploadsDisabledWithoutARealTarget) {
    UploaderConfig cfg; configSetDefaults(cfg);
    EXPECT_FALSE(configUploadsEnabled(cfg));          // placeholders only (no config file)
    configParseJson(R"({"endpoint":"https://x/tb"})", cfg);
    EXPECT_FALSE(configUploadsEnabled(cfg));          // key still the placeholder
    configParseJson(R"({"apiKey":"K"})", cfg);
    EXPECT_TRUE(configUploadsEnabled(cfg));
    cfg.endpointUrl.clear();
    EXPECT_FALSE(configUploadsEnabled(cfg));
}
