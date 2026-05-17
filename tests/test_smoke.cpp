#include <gtest/gtest.h>

#include <wf/version.h>

TEST(SmokeTest, VersionDefines) {
    EXPECT_EQ(WF_VERSION_MAJOR, 0);
    EXPECT_EQ(WF_VERSION_MINOR, 1);
    EXPECT_EQ(WF_VERSION_PATCH, 0);
    EXPECT_STREQ(WF_VERSION, "0.1.0");
}

TEST(SmokeTest, VersionLinkage) {
    auto ver = wf::version_string();
    EXPECT_NE(ver, nullptr);
    EXPECT_STREQ(ver, WF_VERSION);
}

TEST(SmokeTest, CompilesAndLinks) {
    EXPECT_TRUE(true);
}
