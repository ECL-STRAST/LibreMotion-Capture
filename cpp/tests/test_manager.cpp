#include <gtest/gtest.h>
#include "platform/Log.h"
#include <vector>
namespace { struct Cap : public lmc::ILogSink { std::vector<std::string> m; std::vector<int> lvl;
    void log(int l, const char* s) override { lvl.push_back(l); m.push_back(s); } }; }
TEST(LogDispatch, RoutesToInstalledSink) {
    Cap c; lmc::Log::set(&c);
    lmc::Log::err("boom %d", 42);
    lmc::Log::set(nullptr);
    ASSERT_EQ(c.m.size(), 1u);
    EXPECT_EQ(c.lvl[0], lmc::kError);
    EXPECT_EQ(c.m[0], "boom 42");
}

#include "TelemetryManager.h"
#include "MockPlatform.h"
#include "config/ConfigParser.h"
#include <memory>
#include <limits>
using namespace lmc;

static VRFrameDataPlain makeFrame(double t) {
    VRFrameDataPlain f{}; f.timestampSec = t; f.leftHandJointCount = 0; f.rightHandJointCount = 0;
    return f;
}

TEST(Manager, ChunksByFramesPerFileAndDrainsOnShutdown) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 2;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    for (int i = 0; i < 5; ++i) m.recordFrame(makeFrame(i));  // 2 full chunks at frames 2 and 4
    m.shutdown();                                             // flushes the partial 5th frame + joins
    ASSERT_EQ(http.posts.size(), 3u);                         // 2 + 1 flushed
    ASSERT_EQ(0u, http.posts[2].body.find(R"([{"session_id":"","timestamp":4,"device_info":"",)")); // partial chunk has 1 frame
    EXPECT_EQ(http.posts[0].headerCount, 4u);
    EXPECT_EQ(http.posts[1].headerCount, 4u);
    EXPECT_EQ(http.posts[2].headerCount, 4u);
}

TEST(Manager, HandTrackingSectionAppearsWhenEnabled) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1; cfg.handTracking = true;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    m.recordFrame(makeFrame(7));
    m.shutdown();
    ASSERT_EQ(http.posts.size(), 1u);
    EXPECT_NE(std::string::npos, http.posts[0].body.find(
        R"("hands":{"left":{"joint_count":0,"joints":[]},"right":{"joint_count":0,"joints":[]}})"));
}

TEST(Manager, SerializesConfiguredButtonAndJointFields) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg);
    cfg.framesPerFile = 1;
    cfg.trigger = true; cfg.grip = true;
    cfg.primaryButton = true; cfg.secondaryButton = true;
    cfg.joystick = true; cfg.handTracking = true;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));

    VRFrameDataPlain f = makeFrame(1.5);
    f.leftCtrl.isActive = 1;
    f.leftCtrl.trigger = 0.75f;
    f.leftCtrl.grip = 0.5f;
    f.leftCtrl.buttons = 0x1u;          // BTN_PRIMARY set, BTN_SECONDARY clear
    f.leftCtrl.stickX = 0.25f;
    f.leftCtrl.stickY = -0.25f;
    f.leftHandJointCount = 2;
    f.leftHandJoints[0] = {8, 1, 1.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, {0}};
    f.leftHandJoints[1] = {9, 0, 2.5f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, {0}};
    m.recordFrame(f);
    m.shutdown();

    ASSERT_EQ(http.posts.size(), 1u);
    const std::string& body = http.posts[0].body;
    EXPECT_NE(std::string::npos, body.find("\"trigger\":0.75")) << body;
    EXPECT_NE(std::string::npos, body.find("\"grip\":0.5")) << body;
    EXPECT_NE(std::string::npos, body.find("\"primary\":true")) << body;
    EXPECT_NE(std::string::npos, body.find("\"secondary\":false")) << body;
    EXPECT_NE(std::string::npos, body.find("\"joystick_x\":0.25")) << body;
    EXPECT_NE(std::string::npos, body.find("\"joystick_y\":-0.25")) << body;
    EXPECT_NE(std::string::npos, body.find(
        "\"joints\":[{\"id\":8,\"state\":1,\"orientation\":[0,0,0,1],\"position\":[1.5,0,0],\"has_pose\":true},"
        "{\"id\":9,\"state\":0,\"orientation\":[0,0,0,1],\"position\":[2.5,0,0],\"has_pose\":true}]")) << body;
}

TEST(Manager, DropsOldestChunksWhenUploadsFallBehind) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));

    http.gate.hold(1);                    // the first upload blocks the worker
    m.recordFrame(makeFrame(0));
    ASSERT_TRUE(http.gate.waitUntilInside());
    for (int i = 1; i <= 6; ++i) m.recordFrame(makeFrame(i));  // 6 more chunks pile up
    http.gate.release();
    m.shutdown();

    // maxQueuedChunks_ is 4, so of the 6 chunks produced while the worker was busy
    // only the 4 newest survive: timestamps 3..6, plus the one already in flight.
    ASSERT_EQ(http.posts.size(), 5u);
    for (int t = 1; t <= 2; ++t)
        EXPECT_EQ(std::string::npos, http.posts[0].body.find("\"timestamp\":" + std::to_string(t)))
            << "chunk " << t << " should have been dropped";
    for (int t = 3; t <= 6; ++t) {
        bool found = false;
        for (size_t p = 0; p < http.posts.size(); ++p)
            if (http.posts[p].body.find("\"timestamp\":" + std::to_string(t)) != std::string::npos) found = true;
        EXPECT_TRUE(found) << "chunk " << t << " should have been uploaded";
    }
}

TEST(Manager, FrameWithoutHandsIsValidJson) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 2; cfg.handTracking = false;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    m.recordFrame(makeFrame(1));
    m.recordFrame(makeFrame(2));
    m.shutdown();
    ASSERT_EQ(http.posts.size(), 1u);
    const std::string& body = http.posts[0].body;
    // Each frame object must close right after the controllers block, with no
    // dangling separator before the closing brace.
    EXPECT_EQ(std::string::npos, body.find(",}")) << body;
    EXPECT_NE(std::string::npos, body.find("\"tracked\":false,\"orientation\":[0,0,0,0],\"position\":[0,0,0]}}},{")) << body;
    EXPECT_EQ(body.substr(body.size() - 4), "}}}]") << body;
}

TEST(Manager, TimestampKeepsSubFramePrecisionInLongSessions) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    m.recordFrame(makeFrame(3600.0 + 1.0 / 90.0));   // one hour in, at 90 Hz
    m.shutdown();
    ASSERT_EQ(http.posts.size(), 1u);
    EXPECT_NE(std::string::npos, http.posts[0].body.find("\"timestamp\":3600.01111111111,")) << http.posts[0].body;
}

TEST(Manager, EscapesStringsAndWritesNonFiniteNumbersAsNull) {
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    cfg.sessionId = "a\"b\\c";
    cfg.deviceInfo = "Quest\n3";
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    VRFrameDataPlain f = makeFrame(0);
    f.hmdPose.position[0] = std::numeric_limits<float>::quiet_NaN();
    f.hmdPose.position[1] = std::numeric_limits<float>::infinity();
    m.recordFrame(f);
    m.shutdown();
    ASSERT_EQ(http.posts.size(), 1u);
    const std::string& body = http.posts[0].body;
    EXPECT_NE(std::string::npos, body.find(R"("session_id":"a\"b\\c")")) << body;
    EXPECT_NE(std::string::npos, body.find(R"("device_info":"Quest\n3")")) << body;
    EXPECT_NE(std::string::npos, body.find(R"("position":[null,null,0])")) << body;
}

TEST(Manager, SavesChunksLocallyWithUploadsDisabled) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 2;
    cfg.jsonOutputPrefix = joinPath(tmp.dir(), "session_s1");
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, nullptr));
    for (int i = 0; i < 3; ++i) m.recordFrame(makeFrame(i));  // 1 full chunk + 1 flushed
    m.shutdown();
    std::string first, second;
    ASSERT_TRUE(readFileToString(joinPath(tmp.dir(), "session_s1_chunk0001.json"), first));
    ASSERT_TRUE(readFileToString(joinPath(tmp.dir(), "session_s1_chunk0002.json"), second));
    EXPECT_EQ(0u, first.find(R"([{"session_id":"","timestamp":0,)"));
    EXPECT_EQ(0u, second.find(R"([{"session_id":"","timestamp":2,)"));
    EXPECT_EQ(']', first.back());
    EXPECT_FALSE(fileExists(joinPath(tmp.dir(), "session_s1_chunk0003.json")));
}

TEST(Manager, LocalCopyMatchesUploadedBody) {
    WinTempDir tmp;
    MockHttp http;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    cfg.jsonOutputPrefix = joinPath(tmp.dir(), "session_s1");
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, &http));
    m.recordFrame(makeFrame(5));
    m.shutdown();
    ASSERT_EQ(http.posts.size(), 1u);
    std::string saved;
    ASSERT_TRUE(readFileToString(joinPath(tmp.dir(), "session_s1_chunk0001.json"), saved));
    EXPECT_EQ(saved, http.posts[0].body);
}

TEST(Manager, ChunkNumberingRestartsOnReinitialize) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    cfg.jsonOutputPrefix = joinPath(tmp.dir(), "session_a");
    GestorTelemetria m;
    ASSERT_TRUE(m.initialize(cfg, nullptr));
    m.recordFrame(makeFrame(1)); m.recordFrame(makeFrame(2));
    m.shutdown();
    cfg.jsonOutputPrefix = joinPath(tmp.dir(), "session_b");
    ASSERT_TRUE(m.initialize(cfg, nullptr));
    m.recordFrame(makeFrame(3));
    m.shutdown();
    EXPECT_TRUE(fileExists(joinPath(tmp.dir(), "session_a_chunk0002.json")));
    EXPECT_TRUE(fileExists(joinPath(tmp.dir(), "session_b_chunk0001.json")));
    EXPECT_FALSE(fileExists(joinPath(tmp.dir(), "session_b_chunk0002.json")));
}

TEST(Manager, ReportsLocalWriteFailure) {
    ScopedCaptureLog log;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.framesPerFile = 1;
    cfg.jsonOutputPrefix = "/nonexistent-dir-lmc/session_s1";
    GestorTelemetria m; ASSERT_TRUE(m.initialize(cfg, nullptr));
    m.recordFrame(makeFrame(1));
    m.shutdown();
    EXPECT_TRUE(log.sink()->errorMentions("Could not save chunk locally"));
}
