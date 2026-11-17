// telemetry_set_frame_rate + telemetry_initialize, through the public C API.
// The test binary has no platform bundle of its own (the Windows/Android ones are only
// in the shared library), so it provides one here: a WinPathResolver rooted at the
// test's temp dir, no HTTP transport and a log sink the tests can inspect.
#include <gtest/gtest.h>
#include "api/TelemetryAPI.h"
#include "MockPlatform.h"
#include "VRTypes.h"
#include "platform/Log.h"
#include <ezc3d/ezc3d.h>
#include <ezc3d/Parameters.h>
#include <ezc3d/Group.h>
#include <ezc3d/Parameter.h>
#include <string>
using namespace lmc;

namespace lmc {
namespace {
CaptureLogSink g_apiLog;
WinPathResolver g_apiPaths("");
} // namespace

PlatformBundle buildPlatformBundle(const char* baseDir) {
    g_apiPaths.setBaseDir(baseDir ? baseDir : "");
    return PlatformBundle{ nullptr, &g_apiPaths, &g_apiLog };
}
} // namespace lmc

namespace {

// One session through the C API: optional initialConfig.json, one frame, shutdown.
// Returns telemetry_initialize's result and the POINT:RATE of the C3D it wrote.
struct SessionResult { int rc; double pointRate; double analogRate; };

SessionResult runSession(const char* configJson) {
    WinTempDir tmp;
    if (configJson) {
        EXPECT_TRUE(writeStringToFile(joinPath(tmp.dir(), "initialConfig.json"), configJson));
    }
    g_apiLog.infos.clear();
    g_apiLog.errors.clear();
    TelemetryConfigPlain cfg{ "s1", "test", tmp.dir().c_str() };
    SessionResult r{ telemetry_initialize(&cfg), 0.0, 0.0 };
    VRFrameDataPlain f{}; f.timestampSec = 0.0; f.hmdPose.rotation[3] = 1.f;
    telemetry_record_frame(&f);
    telemetry_shutdown();
    ezc3d::c3d file(tmp.c3dPath());
    r.pointRate  = file.parameters().group("POINT").parameter("RATE").valuesAsDouble()[0];
    r.analogRate = file.parameters().group("ANALOG").parameter("RATE").valuesAsDouble()[0];
    return r;
}

const char* kConfig72 = "{ \"frameRate\": 72 }";

// The override is process-wide and persists; never leak it into another test.
struct ApiFrameRate : ::testing::Test {
    void SetUp() override    { telemetry_set_frame_rate(0); }
    void TearDown() override { telemetry_set_frame_rate(0); }
};

} // namespace

TEST_F(ApiFrameRate, ConfigValueIsUsedWithoutOverride) {
    SessionResult r = runSession(kConfig72);
    EXPECT_EQ(r.rc, 72);
    EXPECT_EQ(r.pointRate, 72.0);
    EXPECT_TRUE(g_apiLog.infoMentions("frameRate=72 (source: initialConfig.json)"));
}

TEST_F(ApiFrameRate, OverrideWinsOverConfig) {
    telemetry_set_frame_rate(90);
    SessionResult r = runSession(kConfig72);
    EXPECT_EQ(r.rc, 90);
    EXPECT_EQ(r.pointRate, 90.0);
    EXPECT_EQ(r.analogRate, 90.0);
    EXPECT_TRUE(g_apiLog.infoMentions("frameRate=90 (source: telemetry_set_frame_rate; initialConfig.json has 72)"));
}

TEST_F(ApiFrameRate, OverrideWithoutConfigFile) {
    telemetry_set_frame_rate(120);
    SessionResult r = runSession(nullptr);
    EXPECT_EQ(r.rc, 120);
    EXPECT_EQ(r.pointRate, 120.0);
}

TEST_F(ApiFrameRate, ZeroRestoresConfigValue) {
    telemetry_set_frame_rate(90);
    telemetry_set_frame_rate(0);
    SessionResult r = runSession(kConfig72);
    EXPECT_EQ(r.rc, 72);
    EXPECT_EQ(r.pointRate, 72.0);
}

TEST_F(ApiFrameRate, ZeroWithoutConfigFallsBackTo60) {
    telemetry_set_frame_rate(0);
    SessionResult r = runSession(nullptr);
    EXPECT_EQ(r.rc, 60);
    EXPECT_EQ(r.pointRate, 60.0);
    EXPECT_TRUE(g_apiLog.infoMentions("frameRate=60 (source: default)"));
}

TEST_F(ApiFrameRate, OutOfRangeValuesAreIgnored) {
    telemetry_set_frame_rate(241);
    telemetry_set_frame_rate(-1);
    SessionResult none = runSession(kConfig72);
    EXPECT_EQ(none.rc, 72);           // no override was ever accepted

    telemetry_set_frame_rate(90);
    telemetry_set_frame_rate(1000);
    telemetry_set_frame_rate(-90);
    SessionResult kept = runSession(kConfig72);
    EXPECT_EQ(kept.rc, 90);           // the previous override is kept
    EXPECT_EQ(kept.pointRate, 90.0);
}

TEST_F(ApiFrameRate, RangeLimitsAreAccepted) {
    telemetry_set_frame_rate(1);
    EXPECT_EQ(runSession(kConfig72).rc, 1);
    telemetry_set_frame_rate(240);
    SessionResult r = runSession(kConfig72);
    EXPECT_EQ(r.rc, 240);
    EXPECT_EQ(r.pointRate, 240.0);
}

TEST_F(ApiFrameRate, OverridePersistsAcrossSessions) {
    telemetry_set_frame_rate(80);
    SessionResult first  = runSession(kConfig72);
    SessionResult second = runSession(kConfig72);
    EXPECT_EQ(first.rc, 80);
    EXPECT_EQ(second.rc, 80);
    EXPECT_EQ(second.pointRate, 80.0);
}
