#include <gtest/gtest.h>

#include <array>

#include "foundation.h"
#include "operations.h"
#include "test_fixture.h"

namespace canvas::poc01::test {
namespace {

TEST(AssetRegistryTest, RejectsDuplicateRegistration) {
  AssetRegistry assets;
  constexpr std::array<uint8_t, 3> bytes = {1, 2, 3};
  EXPECT_EQ(assets.Register("same", bytes), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(assets.Register("same", bytes), CANVAS_POC_STATUS_ALREADY_EXISTS);
  EXPECT_NE(GetLastError().find("already registered"), std::string_view::npos);
}

TEST(DocumentTest, FixedReplayHasStableDigest) {
  auto document = MakeDocument();
  ASSERT_NE(document, nullptr);
  ASSERT_EQ(ApplyOperations(*document, FixedReplay()), CANVAS_POC_STATUS_OK);
  EXPECT_EQ(document->state().last_sequence, 7U);
  EXPECT_EQ(document->state().nodes.size(), 4U);
  EXPECT_EQ(document->Digest(), "d8af445a8826bec1c014f7c3b4f57591");
}

TEST(DocumentTest, DigestIsIdenticalAcrossTenFreshReplays) {
  std::string expected;
  for (int iteration = 0; iteration < 10; ++iteration) {
    auto document = MakeDocument();
    ASSERT_EQ(ApplyOperations(*document, FixedReplay()), CANVAS_POC_STATUS_OK);
    if (iteration == 0) {
      expected = document->Digest();
    } else {
      EXPECT_EQ(document->Digest(), expected);
    }
  }
}

}  // namespace
}  // namespace canvas::poc01::test
