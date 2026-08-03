#include "canvas/core/version.h"
#include <gtest/gtest.h>

TEST(VersionTest, ReportsInitialVersion) {
    EXPECT_EQ(canvas::core::version(), "0.1.0");
}
