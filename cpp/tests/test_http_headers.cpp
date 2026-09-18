#include <gtest/gtest.h>
#include "HttpHeaders.h"
using namespace lmc;
TEST(HttpHeaders, MatchesOriginalSet) {
    auto h = buildSupabaseHeaders("K");
    ASSERT_EQ(h.size(), 4u);
    EXPECT_EQ(h[0].first, "Content-Type"); EXPECT_EQ(h[0].second, "application/json");
    EXPECT_EQ(h[1].first, "apikey");       EXPECT_EQ(h[1].second, "K");
    EXPECT_EQ(h[2].first, "Authorization");EXPECT_EQ(h[2].second, "Bearer K");
    EXPECT_EQ(h[3].first, "Prefer");       EXPECT_EQ(h[3].second, "return=representation");
}
