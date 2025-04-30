#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "xenocomm/core/version.h"

using namespace xenocomm::core;

class VersionTest : public ::testing::Test {
protected:
    Version v1_0_0{1, 0, 0};
    Version v1_1_0{1, 1, 0};
    Version v1_1_1{1, 1, 1};
    Version v2_0_0{2, 0, 0};
    Version v2_1_0{2, 1, 0};
};

TEST_F(VersionTest, BasicComparison) {
    EXPECT_TRUE(v1_0_0 < v1_1_0);
    EXPECT_TRUE(v1_1_0 < v1_1_1);
    EXPECT_TRUE(v1_1_1 < v2_0_0);
    EXPECT_TRUE(v2_0_0 < v2_1_0);

    EXPECT_TRUE(v1_0_0 <= v1_0_0);
    EXPECT_TRUE(v1_0_0 >= v1_0_0);
    EXPECT_TRUE(v1_0_0 == v1_0_0);

    EXPECT_FALSE(v2_0_0 < v1_1_1);
    EXPECT_FALSE(v1_1_0 == v1_1_1);
}

TEST_F(VersionTest, Compatibility) {
    // Same major version compatibility
    EXPECT_TRUE(v1_1_0.isCompatibleWith(v1_0_0));  // Higher minor is compatible
    EXPECT_TRUE(v1_1_1.isCompatibleWith(v1_1_0));  // Higher patch is compatible
    EXPECT_FALSE(v1_0_0.isCompatibleWith(v1_1_0)); // Lower minor is not compatible
    EXPECT_FALSE(v2_0_0.isCompatibleWith(v1_0_0)); // Different major is not compatible

    // Flexible satisfaction rules
    EXPECT_TRUE(v2_0_0.satisfies(v1_0_0));  // Higher major can satisfy lower
    EXPECT_TRUE(v2_1_0.satisfies(v2_0_0));  // Same major, higher minor satisfies
    EXPECT_FALSE(v1_1_1.satisfies(v2_0_0)); // Lower major cannot satisfy higher
}

TEST_F(VersionTest, NewerThan) {
    EXPECT_TRUE(v1_1_0.isNewerThan(v1_0_0));
    EXPECT_TRUE(v2_0_0.isNewerThan(v1_1_1));
    EXPECT_FALSE(v1_0_0.isNewerThan(v1_1_0));
}

TEST_F(VersionTest, StringRepresentation) {
    EXPECT_EQ(v1_0_0.toString(), "1.0.0");
    EXPECT_EQ(v2_1_0.toString(), "2.1.0");
    EXPECT_EQ(v1_1_1.toString(), "1.1.1");
}

TEST_F(VersionTest, EdgeCases) {
    Version v0_0_0{0, 0, 0};
    Version v0_1_0{0, 1, 0};
    Version vMax{65535, 65535, 65535};  // uint16_t max

    EXPECT_TRUE(v0_0_0.isCompatibleWith(v0_0_0));
    EXPECT_TRUE(v0_1_0.isCompatibleWith(v0_0_0));
    EXPECT_TRUE(vMax.isNewerThan(v2_1_0));
    EXPECT_FALSE(v0_0_0.isNewerThan(v0_0_0));
}

// This is a placeholder test to verify the test framework is working
TEST(VersionTest, TestFrameworkWorks) {
    EXPECT_TRUE(true);
}

// Add more version-related tests here once the version functionality is implemented
TEST(VersionTest, VersionStringFormat) {
    // This test will be implemented once version.hpp is created
    EXPECT_TRUE(true) << "Version string format test needs to be implemented";
}

TEST(VersionTest, VersionComparison) {
    // This test will be implemented once version comparison is added
    EXPECT_TRUE(true) << "Version comparison test needs to be implemented";
} 