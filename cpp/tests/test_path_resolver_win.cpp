#include <gtest/gtest.h>
#include "windows/WinPathResolver.h"

using namespace lmc;

TEST(WinPathResolver, ConfigAndC3dPaths) {
    WinPathResolver r("C:/Saved");
    std::string cfg, c3d;
    ASSERT_TRUE(r.configFilePath(cfg));
    EXPECT_EQ(cfg, "C:/Saved/initialConfig.json");
    ASSERT_TRUE(r.c3dFilePath("App-2026-01-01-abc", c3d));
    EXPECT_EQ(c3d, "C:/Saved/session_App-2026-01-01-abc.c3d");
}

TEST(WinPathResolver, SanitizesSessionId) {
    WinPathResolver r("D:/x");
    std::string c3d;
    ASSERT_TRUE(r.c3dFilePath("bad/name with spaces", c3d));
    EXPECT_EQ(c3d, "D:/x/session_bad_name_with_spaces.c3d");
}

TEST(WinPathResolver, LaterBaseDirReplacesEarlierOne) {
    WinPathResolver r("C:/First");
    std::string out;
    ASSERT_TRUE(r.c3dFilePath("s1", out));
    EXPECT_EQ(out, "C:/First/session_s1.c3d");

    r.setBaseDir("D:/Second");
    ASSERT_TRUE(r.c3dFilePath("s1", out));
    EXPECT_EQ(out, "D:/Second/session_s1.c3d");
    ASSERT_TRUE(r.configFilePath(out));
    EXPECT_EQ(out, "D:/Second/initialConfig.json");
}
