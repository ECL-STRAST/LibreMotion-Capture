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
