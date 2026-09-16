#include <gtest/gtest.h>
#include "C3DRecorder.h"
#include "MockPlatform.h"
#include "VRTypes.h"
#include "TelemetryTypes.h"
#include "config/ConfigParser.h"
#include <ezc3d/ezc3d.h>
#include <ezc3d/Parameters.h>
#include <ezc3d/Group.h>
#include <ezc3d/Parameter.h>
#include <cstdio>
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
