#include <gtest/gtest.h>

#include <bit>

#include "conformance.h"
#include "foundation.h"

namespace canvas::poc01::test {
namespace {

TEST(CoreConformanceTest, CanonicalEncoderNormalizesNegativeZero) {
  CanonicalEncoder encoder;
  encoder.F32(-0.0F);
  ASSERT_EQ(encoder.data().size(), 4U);
  EXPECT_EQ(encoder.data()[0], 0U);
  EXPECT_EQ(encoder.data()[1], 0U);
  EXPECT_EQ(encoder.data()[2], 0U);
  EXPECT_EQ(encoder.data()[3], 0U);
}

TEST(CoreConformanceTest, NumericAndReplayCorpusPasses) {
  const CoreConformanceResult result = RunCoreConformance();
  EXPECT_EQ(result.case_count, 10U);
  EXPECT_EQ(result.replay_revision, 1U);
  EXPECT_EQ(result.replay_sequence, 4U);
  EXPECT_TRUE(result.passed) << result.failure;
  EXPECT_TRUE(result.failure.empty());
  EXPECT_EQ(result.corpus_digest.size(), 32U);
  EXPECT_EQ(result.replay_digest.size(), 32U);
}

}  // namespace
}  // namespace canvas::poc01::test
