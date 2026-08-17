#include <gtest/gtest.h>

#include <array>
#include <bit>

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

TEST(DocumentTest, TwoFreshReplaysHaveEquivalentStateAndMetadata) {
  auto first = MakeDocument();
  auto second = MakeDocument();
  ASSERT_EQ(ApplyOperations(*first, FixedReplay()), CANVAS_POC_STATUS_OK);
  ASSERT_EQ(ApplyOperations(*second, FixedReplay()), CANVAS_POC_STATUS_OK);

  EXPECT_EQ(first->state().revision, second->state().revision);
  EXPECT_EQ(first->state().last_sequence, second->state().last_sequence);
  EXPECT_EQ(first->state().nodes.size(), second->state().nodes.size());
  EXPECT_EQ(first->Digest(), second->Digest());
  auto left = first->state().nodes.begin();
  auto right = second->state().nodes.begin();
  for (; left != first->state().nodes.end(); ++left, ++right) {
    ASSERT_NE(right, second->state().nodes.end());
    EXPECT_EQ(left->first, right->first);
    EXPECT_EQ(Header(left->second).id, Header(right->second).id);
    EXPECT_EQ(Header(left->second).order, Header(right->second).order);
    EXPECT_EQ(std::bit_cast<uint32_t>(Header(left->second).translation_x),
              std::bit_cast<uint32_t>(Header(right->second).translation_x));
    EXPECT_EQ(std::bit_cast<uint32_t>(Header(left->second).translation_y),
              std::bit_cast<uint32_t>(Header(right->second).translation_y));
  }
  EXPECT_EQ(right, second->state().nodes.end());
}

}  // namespace
}  // namespace canvas::poc01::test
