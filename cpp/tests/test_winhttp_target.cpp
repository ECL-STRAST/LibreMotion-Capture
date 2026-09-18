#include <gtest/gtest.h>
#include "windows/WinHttpTransport.h"

using namespace lmc;

TEST(WinHttpRequestTarget, KeepsQueryStringFromCrackedUrl) {
    EXPECT_EQ(winHttpRequestTarget(L"/rest/v1/frames", L"?on_conflict=session"),
              L"/rest/v1/frames?on_conflict=session");
}

TEST(WinHttpRequestTarget, EmptyPathBecomesRoot) {
    EXPECT_EQ(winHttpRequestTarget(L"", L""), L"/");
}

TEST(WinHttpRequestTarget, MissingQueryDelimiterIsInserted) {
    EXPECT_EQ(winHttpRequestTarget(L"/frames", L"columns=a"), L"/frames?columns=a");
}
