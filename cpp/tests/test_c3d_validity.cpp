// telemetry_record_frame_ex / C3DrecordFrame(frame, validity): headset presence,
// per-device tracking bits and the two-part time stamp (RealTime_s + RealTime_frac).
#include <gtest/gtest.h>
#include "C3DRecorder.h"
#include "MockPlatform.h"
#include "VRTypes.h"
#include "TelemetryTypes.h"
#include "config/ConfigParser.h"
#include <ezc3d/ezc3d.h>
#include <ezc3d/Data.h>
#include <cmath>
#include <string>
using namespace lmc;

namespace {
size_t analogIndex(const ezc3d::c3d& file, const std::string& label) {
    const auto& names = file.channelNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == label) return i;
    ADD_FAILURE() << "no analog channel " << label;
    return 0;
}
double analog(const ezc3d::c3d& file, const std::string& label, size_t frame) {
    return file.data().frame(frame).analogs().subframe(0).channel(analogIndex(file, label)).data();
}
const ezc3d::DataNS::Points3dNS::Point& point(const ezc3d::c3d& file, const std::string& label, size_t frame) {
    const auto& names = file.pointNames();
    size_t idx = 0;
    for (; idx < names.size(); ++idx)
        if (names[idx] == label) break;
    EXPECT_LT(idx, names.size()) << "no point " << label;
    return file.data().frame(frame).points().point(idx);
}

VRFrameDataPlain trackedFrame(double t) {
    VRFrameDataPlain f{};
    f.timestampSec = t;
    f.hmdPose.position[0] = 0.1f; f.hmdPose.position[1] = 1.6f; f.hmdPose.position[2] = -0.2f;
    f.hmdPose.rotation[3] = 1.f;
    f.leftCtrl.isActive = 1;
    f.leftCtrl.pose.position[0] = -0.3f; f.leftCtrl.pose.position[1] = 1.0f;
    f.leftCtrl.pose.rotation[3] = 1.f;
    f.rightCtrl.isActive = 1;
    f.rightCtrl.pose.position[0] = 0.3f; f.rightCtrl.pose.position[1] = 1.0f;
    f.rightCtrl.pose.rotation[3] = 1.f;
    return f;
}
} // namespace

// Every channel and point of the old layout keeps its label and index; the five new
// channels follow RealTime.
TEST(C3dValidity, NewChannelsAreAppendedAfterRealTime) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 72, tmp.paths()));
    VRFrameDataPlain f = trackedFrame(1.0);
    r.C3DrecordFrame(f);
    r.C3Dfinalize();
    ezc3d::c3d file(tmp.c3dPath());
    const auto& names = file.channelNames();
    ASSERT_EQ(names.size(), 218u);
    EXPECT_EQ(names[0], "HMD_qx");
    EXPECT_EQ(names[4 + 7 * 4], "LFIN_qx");
    EXPECT_EQ(names[4 + 26 * 4 + 7 * 4 + 3], "RFIN_qw");
    EXPECT_EQ(names[212], "RealTime");
    EXPECT_EQ(names[213], "RealTime_s");
    EXPECT_EQ(names[214], "RealTime_frac");
    EXPECT_EQ(names[215], "HMD_flags");
    EXPECT_EQ(names[216], "L_flags");
    EXPECT_EQ(names[217], "R_flags");
    EXPECT_EQ(file.pointNames().size(), 59u);
    EXPECT_EQ(file.pointNames()[0], "HEAD");
}

// A frame recorded without validity is written as before: HEAD present (residual 0)
// even at the origin, HMD quaternion written, flag channels -1.
TEST(C3dValidity, PlainRecordKeepsTodaysHeadAndMarksFlagsNotReported) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 72, tmp.paths()));
    VRFrameDataPlain f{};                 // headset at the origin, identity
    f.timestampSec = 3.25;
    f.hmdPose.rotation[3] = 1.f;
    r.C3DrecordFrame(f);
    r.C3DrecordFrame(f, nullptr);         // explicit null = the same
    r.C3Dfinalize();
    ezc3d::c3d file(tmp.c3dPath());
    ASSERT_EQ(file.data().nbFrames(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(point(file, "HEAD", i).residual(), 0.0);
        EXPECT_EQ(point(file, "LFHD", i).residual(), 0.0);
        EXPECT_FLOAT_EQ(analog(file, "HMD_qw", i), 0.5);
        EXPECT_EQ(analog(file, "HMD_flags", i), -1.0);
        EXPECT_EQ(analog(file, "L_flags", i), -1.0);
        EXPECT_EQ(analog(file, "R_flags", i), -1.0);
        EXPECT_EQ(analog(file, "RealTime", i), 3.25);
        EXPECT_EQ(analog(file, "RealTime_s", i), 3.0);
        EXPECT_EQ(analog(file, "RealTime_frac", i), 0.25);
    }
}

// The flags are stored verbatim; without a headset pose HEAD, LFHD and RFHD are missing
// and the HMD quaternion is 0, while the controllers are unaffected.
TEST(C3dValidity, ReportedFlagsAndHeadsetWithoutPose) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 72, tmp.paths()));
    VRFrameDataPlain f = trackedFrame(10.0);
    VRFrameValidityPlain tracked{ 1, 7951u, 7951u, 1027u };
    r.C3DrecordFrame(f, &tracked);
    VRFrameValidityPlain lost{ 0, 512u, 7951u, (1u << 24) - 1u };
    r.C3DrecordFrame(f, &lost);
    r.C3Dfinalize();
    ezc3d::c3d file(tmp.c3dPath());
    ASSERT_EQ(file.data().nbFrames(), 2u);

    EXPECT_EQ(point(file, "HEAD", 0).residual(), 0.0);
    EXPECT_NEAR(point(file, "HEAD", 0).z(), 1600.0, 1e-3);
    EXPECT_FLOAT_EQ(analog(file, "HMD_qw", 0), 0.5);
    EXPECT_EQ(analog(file, "HMD_flags", 0), 7951.0);
    EXPECT_EQ(analog(file, "L_flags", 0), 7951.0);
    EXPECT_EQ(analog(file, "R_flags", 0), 1027.0);

    for (const char* label : { "HEAD", "LFHD", "RFHD" }) {
        EXPECT_LT(point(file, label, 1).residual(), 0.0) << label;
    }
    for (const char* label : { "HMD_qx", "HMD_qy", "HMD_qz", "HMD_qw" }) {
        EXPECT_EQ(analog(file, label, 1), 0.0) << label;
    }
    EXPECT_EQ(point(file, "LFIN", 1).residual(), 0.0);
    EXPECT_EQ(point(file, "RFIN", 1).residual(), 0.0);
    EXPECT_FLOAT_EQ(analog(file, "LFIN_qw", 1), 0.5);
    EXPECT_EQ(analog(file, "HMD_flags", 1), 512.0);
    EXPECT_EQ(analog(file, "L_flags", 1), 7951.0);
    EXPECT_EQ(analog(file, "R_flags", 1), 16777215.0);   // 2^24 - 1, still exact
}

// RealTime is float32; RealTime_s + RealTime_frac gives the double time back to well
// below a microsecond at session times where RealTime is off by tens of microseconds.
TEST(C3dValidity, TwoPartTimeKeepsTheDoubleTimeStamp) {
    WinTempDir tmp;
    UploaderConfig cfg; configSetDefaults(cfg); cfg.sessionId = "s1";
    C3DRecorder r;
    ASSERT_TRUE(r.C3Dinitialize(cfg, 72, tmp.paths()));
    const double times[] = { 0.0, 0.013889, 1234.567891, 1799.986111, 86399.999999, 1e6 + 0.5 };
    for (double t : times) {
        VRFrameDataPlain f = trackedFrame(t);
        r.C3DrecordFrame(f);
    }
    r.C3Dfinalize();
    ezc3d::c3d file(tmp.c3dPath());
    ASSERT_EQ(file.data().nbFrames(), sizeof(times) / sizeof(times[0]));
    for (size_t i = 0; i < file.data().nbFrames(); ++i) {
        const double s = analog(file, "RealTime_s", i);
        const double frac = analog(file, "RealTime_frac", i);
        EXPECT_EQ(s, std::floor(times[i]));
        EXPECT_GE(frac, 0.0);
        EXPECT_LT(frac, 1.0);
        EXPECT_NEAR(s + frac, times[i], 1e-7) << "t=" << times[i];
        EXPECT_EQ(analog(file, "RealTime", i), static_cast<double>(static_cast<float>(times[i])));
    }
    // At 1234.567891 s the float32 RealTime alone is off by more than 10 microseconds.
    EXPECT_GT(std::fabs(analog(file, "RealTime", 2) - times[2]), 1e-5);
}
