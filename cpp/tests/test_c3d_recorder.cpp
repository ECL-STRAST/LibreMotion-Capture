#include <gtest/gtest.h>
#include "C3DRecorder.h"
#include "MockPlatform.h"
#include "VRTypes.h"
#include "TelemetryTypes.h"
#include "config/ConfigParser.h"
#include "platform/Log.h"
#include <ezc3d/ezc3d.h>
#include <ezc3d/Parameters.h>
#include <ezc3d/Group.h>
#include <ezc3d/Parameter.h>
#include <ezc3d/Data.h>
#include <cstdio>
#include <cmath>
using namespace lmc;

TEST(C3dRecorder, WritesAndReadsBackExpectedLayout) {
    WinTempDir tmp;                       // helper from MockPlatform.h (creates a temp dir)
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain f{}; f.timestampSec = 0.5;
    f.hmdPose.position[0]=1.f; f.hmdPose.position[1]=2.f; f.hmdPose.position[2]=3.f;
    f.hmdPose.rotation[3]=1.f;
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    // ezc3d read-back API for this vendored version exposes pointNames()/
    // channelNames() on the c3d object and valuesAsDouble() on a Parameter.
    // Semantics identical to the brief (count HEAD,LFHD,RFHD + hands + wrists;
    // label(0)=HEAD, label(3)=LWRT, last analog=RealTime, POINT:RATE=60).
    ezc3d::c3d file(tmp.c3dPath());
    EXPECT_EQ(file.pointNames().size(), 3u + 26u + 26u + 4u); // HEAD,LFHD,RFHD + hands + wrists
    EXPECT_EQ(file.pointNames()[0], "HEAD");
    EXPECT_EQ(file.pointNames()[3], "LWRT");                  // left hand base joint 0
    EXPECT_EQ(file.channelNames().back(), "RealTime");
    EXPECT_EQ(file.parameters().group("POINT").parameter("RATE").valuesAsDouble()[0], 60.0);
}

TEST(C3dRecorder, ReinitializeAfterFinalizeStartsNewSession) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;

    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain first{}; first.timestampSec = 1.0;
    r.C3DrecordFrame(first);
    r.C3Dfinalize();

    ASSERT_TRUE(r.C3Dinitialize(cfg, 90, tmp.paths()));
    VRFrameDataPlain second{}; second.timestampSec = 2.0;
    r.C3DrecordFrame(second);
    r.C3Dfinalize();

    ezc3d::c3d file(tmp.c3dPath());
    EXPECT_EQ(file.parameters().group("POINT").parameter("RATE").valuesAsDouble()[0], 90.0);
    EXPECT_EQ(file.data().nbFrames(), 1u);
}

TEST(C3dRecorder, ReinitializeWhileRecordingKeepsCurrentSession) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;

    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain f{}; f.timestampSec = 1.0;
    r.C3DrecordFrame(f);
    EXPECT_TRUE(r.C3Dinitialize(cfg, 120, tmp.paths()));
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    ezc3d::c3d file(tmp.c3dPath());
    EXPECT_EQ(file.parameters().group("POINT").parameter("RATE").valuesAsDouble()[0], 60.0);
    EXPECT_EQ(file.data().nbFrames(), 2u);
}

TEST(C3dRecorder, UnwritableOutputDirectoryIsReportedAsError) {
    WinTempDir tmp;
    WinPathResolver resolver(tmp.dir() + "/not_created");
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";

    std::string expected;
    ASSERT_TRUE(resolver.c3dFilePath("s1", expected));

    ScopedCaptureLog logCapture;
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, &resolver));
    VRFrameDataPlain f{}; f.timestampSec = 1.0;
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    EXPECT_FALSE(fileExists(expected));
    EXPECT_TRUE(logCapture.sink()->errorMentions("session_s1.c3d"));
    EXPECT_FALSE(logCapture.sink()->infoMentions("written OK"));
}

namespace {
double analogAt(const ezc3d::c3d& file, const std::string& label, size_t frame = 0) {
    const auto& names = file.channelNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == label) return file.data().frame(frame).analogs().subframe(0).channel(i).data();
    ADD_FAILURE() << "no analog channel " << label;
    return 0.0;
}
} // namespace

TEST(C3dRecorder, ControllerQuaternionLandsInItsOwnFinChannels) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain f{}; f.timestampSec = 2.5;
    f.hmdPose.rotation[3] = 1.f;
    f.leftCtrl.isActive = 1;
    f.leftCtrl.pose.position[0] = 0.1f;
    f.leftCtrl.pose.rotation[3] = 1.f;               // identity
    f.rightCtrl.isActive = 1;
    f.rightCtrl.pose.position[0] = -0.1f;
    f.rightCtrl.pose.rotation[3] = 1.f;
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    ezc3d::c3d file(tmp.c3dPath());
    // Identity in OpenXR becomes the fixed axis-change quaternion (0.5,-0.5,-0.5,0.5).
    EXPECT_FLOAT_EQ(analogAt(file, "LFIN_qw"), 0.5);
    EXPECT_FLOAT_EQ(analogAt(file, "LFIN_qx"), 0.5);
    EXPECT_FLOAT_EQ(analogAt(file, "RFIN_qw"), 0.5);
    // Nothing leaks into the wrist/palm columns that precede FIN.
    EXPECT_FLOAT_EQ(analogAt(file, "LWRT_qx"), 0.0);
    EXPECT_FLOAT_EQ(analogAt(file, "LWRT_qw"), 0.0);
    EXPECT_FLOAT_EQ(analogAt(file, "LPLM_qw"), 0.0);
    EXPECT_FLOAT_EQ(analogAt(file, "RealTime"), 2.5);
}

TEST(C3dRecorder, JointsArePlacedByIdNotByArrayPosition) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 60, tmp.paths()));
    VRFrameDataPlain f{}; f.timestampSec = 0.0;
    f.hmdPose.rotation[3] = 1.f;
    // Provider without a palm joint: the compacted array holds wrist (0) and
    // thumb metacarpal (2) in positions 0 and 1.
    f.leftHandJointCount = 2;
    f.leftHandJoints[0] = {0, 1, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, {0}};
    f.leftHandJoints[1] = {2, 1, 0.f, 2.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1, {0}};
    r.C3DrecordFrame(f);
    r.C3Dfinalize();

    ezc3d::c3d file(tmp.c3dPath());
    const auto& names = file.pointNames();
    auto pointZ = [&](const std::string& label) {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == label) return file.data().frame(0).points().point(i).z();
        ADD_FAILURE() << "no point " << label;
        return 0.0;
    };
    // OpenXR +Y (up) is gait +Z, in millimetres.
    EXPECT_DOUBLE_EQ(pointZ("LWRT"), 1000.0);
    EXPECT_DOUBLE_EQ(pointZ("LTMC"), 2000.0);
    EXPECT_TRUE(std::isnan(pointZ("LPLM")));          // not reported -> missing marker
    EXPECT_FLOAT_EQ(analogAt(file, "LTMC_qw"), 0.5);
    EXPECT_FLOAT_EQ(analogAt(file, "LPLM_qw"), 0.0);
}
