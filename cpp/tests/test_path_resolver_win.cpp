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
