#include <gtest/gtest.h>

#include "canvas_poc04/rich_text.h"

namespace canvas::poc04 {
namespace {

TEST(FontResourcesTest, VerifiesHashAndNeverConsultsSystemFonts) {
  const std::vector<uint8_t> bytes = {1, 2, 3, 4};
  const std::string hash = Sha256Hex(bytes);
  FontResourceResolver resolver;
  EXPECT_FALSE(resolver.Register("font", std::string(64, '0'), bytes));
  EXPECT_TRUE(resolver.Register("font", hash, bytes));
  const FontResolution result = resolver.Resolve("font", {});
  EXPECT_EQ(result.diagnostic, FontDiagnostic::kOk);
  EXPECT_EQ(result.content_hash, hash);
  EXPECT_EQ(result.bytes.size(), bytes.size());
}

TEST(FontResourcesTest, CanonicalFallbackOrderIsDeterministic) {
  FontResourceResolver resolver;
  const std::vector<uint8_t> first = {1};
  const std::vector<uint8_t> second = {2};
  ASSERT_TRUE(resolver.Register("fallback-a", Sha256Hex(first), first));
  ASSERT_TRUE(resolver.Register("fallback-b", Sha256Hex(second), second));
  const std::array<std::string, 2> chain = {"fallback-b", "fallback-a"};
  const FontResolution result = resolver.Resolve("missing", chain);
  EXPECT_EQ(result.diagnostic, FontDiagnostic::kFallbackUsed);
  EXPECT_EQ(result.resolved_id, "fallback-b");
  EXPECT_EQ(resolver.Resolve("also-missing", {}).diagnostic,
            FontDiagnostic::kMissing);
}

TEST(FontResourcesTest, DeclaredButUnavailableBlobIsHashMismatch) {
  FontResourceResolver resolver;
  ASSERT_TRUE(resolver.Declare("font", std::string(64, '1')));
  EXPECT_EQ(resolver.Resolve("font", {}).diagnostic,
            FontDiagnostic::kHashMismatch);
  EXPECT_EQ(resolver.Resolve("undeclared", {}).diagnostic,
            FontDiagnostic::kMissing);
}

TEST(FontResourcesTest, ReplacementChangesGeneration) {
  FontResourceResolver resolver;
  const std::vector<uint8_t> before = {1};
  const std::vector<uint8_t> after = {2};
  ASSERT_TRUE(resolver.Register("font", Sha256Hex(before), before));
  const uint64_t generation = resolver.generation();
  ASSERT_TRUE(resolver.Register("font", Sha256Hex(after), after));
  EXPECT_GT(resolver.generation(), generation);
  EXPECT_TRUE(resolver.Remove("font"));
  // Removing the blob preserves the declared identity, so this is a known
  // resource whose verified bytes are unavailable rather than an unknown ID.
  EXPECT_EQ(resolver.Resolve("font", {}).diagnostic,
            FontDiagnostic::kHashMismatch);
}

}  // namespace
}  // namespace canvas::poc04
