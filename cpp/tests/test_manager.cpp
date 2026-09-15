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
