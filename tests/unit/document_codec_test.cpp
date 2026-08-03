#include "canvas/storage/document_codec.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "platform/windows/document_store.h"

#include <windows.h>
#endif

using namespace canvas;

namespace {

document::Node makeEmbedded(document::NodeId id, document::EmbeddedKind kind,
                            std::optional<document::NodeId> parent = {}) {
  document::Node node;
  node.id = std::move(id);
  node.layer = document::LayerClass::Embedded;
  node.bounds = {10.0F, 20.0F, 640.0F, 480.0F};
  node.parentId = std::move(parent);
  node.payload = document::EmbeddedNode{kind, "https://example.com", "Example"};
  return node;
}

}  // namespace

TEST(DocumentCodecTest, RoundTripsEmbeddedAndAttachedStrokeNodes) {
  document::Document input;
  ASSERT_TRUE(input.add(makeEmbedded("web-1", document::EmbeddedKind::Web)));
  document::StrokeNode stroke;
  stroke.points = {{{0.1F, 0.2F}, 0.7F, 1000}, {{0.5F, 0.6F}, 0.8F, 2000}};
  stroke.width = 6.0F;
  stroke.colorArgb = 0x80112233U;
  stroke.coordinateSpace = document::StrokeCoordinateSpace::ParentNormalized;
  ASSERT_TRUE(input.add({"ink-1",
                         document::LayerClass::Annotation,
                         {0, 0, 1, 1},
                         "web-1",
                         stroke}));

  const auto bytes = storage::DocumentCodec::encode(input);
  ASSERT_FALSE(bytes.empty());
  const auto decoded = storage::DocumentCodec::decode(bytes);
  ASSERT_TRUE(decoded.document.has_value()) << decoded.error;
  ASSERT_NE(decoded.document->find("web-1"), nullptr);
  ASSERT_NE(decoded.document->find("ink-1"), nullptr);
  EXPECT_EQ(decoded.document->find("ink-1")->parentId, "web-1");
  const auto* decodedStroke = std::get_if<document::StrokeNode>(
      &decoded.document->find("ink-1")->payload);
  ASSERT_NE(decodedStroke, nullptr);
  EXPECT_EQ(decodedStroke->points.size(), 2U);
  EXPECT_EQ(decodedStroke->colorArgb, 0x80112233U);
  EXPECT_FLOAT_EQ(decodedStroke->width, 6.0F);
  EXPECT_EQ(decodedStroke->coordinateSpace,
            document::StrokeCoordinateSpace::ParentNormalized);
}

TEST(DocumentCodecTest, PreservesRepresentableUnknownNodeFields) {
  const std::string raw =
      R"({"schemaVersion":1,"nodes":[{"id":"future-1","type":"future-widget","layer":"base","bounds":[0,0,10,10],"parentId":null,"payload":{"answer":42,"maximum":18446744073709551615,"nested":{"keep":true}}}]})";
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  const auto decoded = storage::DocumentCodec::decodeJson(bytes);
  ASSERT_TRUE(decoded.document.has_value()) << decoded.error;
  const auto* node = decoded.document->find("future-1");
  ASSERT_NE(node, nullptr);
  const auto* unknown = std::get_if<document::UnknownNode>(&node->payload);
  ASSERT_NE(unknown, nullptr);
  EXPECT_EQ(unknown->typeName, "future-widget");
  const auto encoded = storage::DocumentCodec::encodeJson(*decoded.document);
  const std::string output(encoded.begin(), encoded.end());
  EXPECT_NE(output.find("\"answer\":42"), std::string::npos);
  EXPECT_NE(output.find("\"maximum\":18446744073709551615"),
            std::string::npos);
  EXPECT_NE(output.find("\"nested\":{"), std::string::npos);

  const auto msgpack = storage::DocumentCodec::encode(*decoded.document);
  ASSERT_FALSE(msgpack.empty());
  const auto reopened = storage::DocumentCodec::decode(msgpack);
  ASSERT_TRUE(reopened.document.has_value()) << reopened.error;
  const auto reopenedJson =
      storage::DocumentCodec::encodeJson(*reopened.document);
  const std::string reopenedText(reopenedJson.begin(), reopenedJson.end());
  EXPECT_NE(reopenedText.find("\"maximum\":18446744073709551615"),
            std::string::npos);
}

TEST(DocumentCodecTest, RejectsUnknownIntegerThatCannotRoundTripLosslessly) {
  const std::string raw =
      R"({"schemaVersion":1,"nodes":[{"id":"future-1","type":"future-widget","layer":"base","bounds":[0,0,10,10],"parentId":null,"payload":{"tooLarge":18446744073709551617}}]})";
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  const auto decoded = storage::DocumentCodec::decodeJson(bytes);
  EXPECT_FALSE(decoded.document.has_value());
  EXPECT_NE(decoded.error.find("losslessly"), std::string::npos);
}

TEST(DocumentCodecTest, RejectsExcessiveJsonNestingBeforeDomParsing) {
  std::string raw =
      R"({"schemaVersion":1,"nodes":[{"id":"future-1","type":"future-widget","layer":"base","bounds":[0,0,10,10],"parentId":null,"payload":)";
  raw.append(300U, '[');
  raw += '0';
  raw.append(300U, ']');
  raw += "}]}";
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  const auto decoded = storage::DocumentCodec::decodeJson(bytes);
  EXPECT_FALSE(decoded.document.has_value());
  EXPECT_NE(decoded.error.find("nesting"), std::string::npos);
}

TEST(DocumentCodecTest, EnforcesExactMessagePackNestingBoundary) {
  const auto nestedArrays = [](std::size_t depth) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(depth * 3U + 1U);
    for (std::size_t index = 0; index < depth; ++index) {
      bytes.push_back(0xDCU);  // array 16
      bytes.push_back(0x00U);
      bytes.push_back(0x01U);
    }
    bytes.push_back(0xC0U);  // nil
    return bytes;
  };

  const auto maximum = storage::DocumentCodec::decode(nestedArrays(256U));
  EXPECT_FALSE(maximum.document.has_value());
  EXPECT_EQ(maximum.error.find("nesting"), std::string::npos);

  const auto excessive = storage::DocumentCodec::decode(nestedArrays(257U));
  EXPECT_FALSE(excessive.document.has_value());
  EXPECT_NE(excessive.error.find("nesting"), std::string::npos);
}

TEST(DocumentCodecTest, RejectsInvalidUtf8FromMessagePackAndLiveModels) {
  document::Document input;
  ASSERT_TRUE(input.add(makeEmbedded("web", document::EmbeddedKind::Web)));
  auto bytes = storage::DocumentCodec::encode(input);
  ASSERT_FALSE(bytes.empty());
  const std::string source = "https://example.com";
  const auto sourceStart =
      std::search(bytes.begin(), bytes.end(), source.begin(), source.end());
  ASSERT_NE(sourceStart, bytes.end());
  *sourceStart = 0xFFU;
  const auto decoded = storage::DocumentCodec::decode(bytes);
  EXPECT_FALSE(decoded.document.has_value());
  EXPECT_NE(decoded.error.find("UTF-8"), std::string::npos);

  document::Document invalid;
  auto invalidNode = makeEmbedded("invalid", document::EmbeddedKind::Web);
  auto& embedded = std::get<document::EmbeddedNode>(invalidNode.payload);
  embedded.source.assign(1U, static_cast<char>(0xFFU));
  ASSERT_TRUE(invalid.add(std::move(invalidNode)));
  EXPECT_TRUE(storage::DocumentCodec::encode(invalid).empty());
  EXPECT_TRUE(storage::DocumentCodec::encodeJson(invalid).empty());
}

TEST(DocumentCodecTest, RejectsCorruptInputWithoutChangingDocument) {
  document::Document live;
  ASSERT_TRUE(live.add(makeEmbedded("sentinel", document::EmbeddedKind::Web)));
  const auto originalInstanceId = live.instanceId();
  std::string error;
  EXPECT_FALSE(storage::DocumentCodec::decodeInto({0xC1, 0x00}, live, error));
  EXPECT_FALSE(error.empty());
  EXPECT_NE(live.find("sentinel"), nullptr);
  EXPECT_EQ(live.instanceId(), originalInstanceId);

  document::Document replacement;
  ASSERT_TRUE(replacement.add(
      makeEmbedded("replacement", document::EmbeddedKind::Web)));
  EXPECT_TRUE(storage::DocumentCodec::decodeInto(
      storage::DocumentCodec::encode(replacement), live, error));
  EXPECT_TRUE(error.empty());
  EXPECT_EQ(live.find("sentinel"), nullptr);
  EXPECT_NE(live.find("replacement"), nullptr);
}

TEST(DocumentCodecTest, RejectsUnsupportedSchemaAndInvalidParent) {
  for (
      const std::string raw :
      {R"({"nodes":[]})", R"({"schemaVersion":0,"nodes":[]})",
       R"({"schemaVersion":2,"nodes":[]})",
       R"({"schemaVersion":1,"nodes":[{"id":"a","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":"missing","payload":{"kind":"web","source":"x","title":""}}]})",
       R"({"schemaVersion":1,"nodes":[{"id":"a","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":"a","payload":{"kind":"web","source":"x","title":""}}]})",
       R"({"schemaVersion":1,"nodes":[{"id":"a","type":"web","layer":"base","bounds":[0,0,10,10],"parentId":null,"payload":{"kind":"web","source":"x","title":""}}]})"}) {
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    const auto result = storage::DocumentCodec::decodeJson(bytes);
    EXPECT_FALSE(result.document.has_value()) << raw;
    EXPECT_FALSE(result.error.empty());
  }
}

TEST(DocumentCodecTest, AllowsChildBeforeParentAndRejectsCycles) {
  const std::string childBeforeParent =
      R"({"schemaVersion":1,"nodes":[{"id":"child","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":"parent","payload":{"kind":"web","source":"x","title":""}},{"id":"parent","type":"web","layer":"embedded","bounds":[0,0,20,20],"parentId":null,"payload":{"kind":"web","source":"y","title":""}}]})";
  const std::vector<std::uint8_t> bytes(childBeforeParent.begin(),
                                        childBeforeParent.end());
  const auto result = storage::DocumentCodec::decodeJson(bytes);
  ASSERT_TRUE(result.document.has_value()) << result.error;
  EXPECT_EQ(result.document->find("child")->parentId, "parent");

  const std::string cycle =
      R"({"schemaVersion":1,"nodes":[{"id":"a","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":"b","payload":{"kind":"web","source":"x","title":""}},{"id":"b","type":"web","layer":"embedded","bounds":[0,0,20,20],"parentId":"a","payload":{"kind":"web","source":"y","title":""}}]})";
  const std::vector<std::uint8_t> cycleBytes(cycle.begin(), cycle.end());
  const auto cycleResult = storage::DocumentCodec::decodeJson(cycleBytes);
  EXPECT_FALSE(cycleResult.document.has_value());
}

TEST(DocumentCodecTest, RejectsNonFiniteAndMalformedPayload) {
  const std::string nonFinite =
      R"({"schemaVersion":1,"nodes":[{"id":"a","type":"web","layer":"embedded","bounds":[0,0,1e999,10],"parentId":null,"payload":{"kind":"web","source":"x","title":""}}]})";
  const std::vector<std::uint8_t> nonFiniteBytes(nonFinite.begin(),
                                                 nonFinite.end());
  EXPECT_FALSE(storage::DocumentCodec::decodeJson(nonFiniteBytes).document);

  const std::string malformedStroke =
      R"({"schemaVersion":1,"nodes":[{"id":"s","type":"stroke","layer":"annotation","bounds":[0,0,10,10],"parentId":null,"payload":{"width":0,"colorArgb":1,"coordinateSpace":"world","points":[[1,2,0.5]]}}]})";
  const std::vector<std::uint8_t> malformedBytes(malformedStroke.begin(),
                                                 malformedStroke.end());
  EXPECT_FALSE(storage::DocumentCodec::decodeJson(malformedBytes).document);
}

TEST(DocumentCodecTest, RefusesToEncodeInvalidLiveModels) {
  document::Document invalidBounds;
  auto zeroWidth = makeEmbedded("zero-width", document::EmbeddedKind::Web);
  zeroWidth.bounds.width = 0.0F;
  ASSERT_TRUE(invalidBounds.add(std::move(zeroWidth)));
  EXPECT_TRUE(storage::DocumentCodec::encode(invalidBounds).empty());

  document::Document invalidStroke;
  document::StrokeNode stroke;
  stroke.width = std::numeric_limits<float>::quiet_NaN();
  stroke.points = {{{1.0F, 2.0F}, 0.5F, 1U}};
  ASSERT_TRUE(invalidStroke.add({"bad-stroke",
                                 document::LayerClass::Annotation,
                                 {0.0F, 0.0F, 10.0F, 10.0F},
                                 std::nullopt,
                                 stroke}));
  EXPECT_TRUE(storage::DocumentCodec::encode(invalidStroke).empty());

  document::Document orphan;
  ASSERT_TRUE(orphan.add(makeEmbedded("node", document::EmbeddedKind::Web)));
  ASSERT_TRUE(orphan.mutate("node", [](document::Node& node) {
    node.parentId = "missing";
  }));
  EXPECT_TRUE(storage::DocumentCodec::encode(orphan).empty());

  document::Document cycle;
  ASSERT_TRUE(cycle.add(makeEmbedded("a", document::EmbeddedKind::Web)));
  ASSERT_TRUE(cycle.add(makeEmbedded("b", document::EmbeddedKind::Web)));
  ASSERT_TRUE(cycle.mutate("a", [](document::Node& node) {
    node.parentId = "b";
  }));
  ASSERT_TRUE(cycle.mutate("b", [](document::Node& node) {
    node.parentId = "a";
  }));
  EXPECT_TRUE(storage::DocumentCodec::encode(cycle).empty());

  document::Document detachedNormalized;
  document::StrokeNode normalized;
  normalized.coordinateSpace =
      document::StrokeCoordinateSpace::ParentNormalized;
  normalized.points = {{{0.1F, 0.2F}, 0.5F, 1U}};
  ASSERT_TRUE(detachedNormalized.add({"detached",
                                      document::LayerClass::Annotation,
                                      {0.0F, 0.0F, 1.0F, 1.0F},
                                      std::nullopt,
                                      normalized}));
  EXPECT_TRUE(storage::DocumentCodec::encode(detachedNormalized).empty());
}

TEST(DocumentCodecTest, RejectsExcessiveParentDepthIteratively) {
  const auto parentChain = [](std::size_t nodeCount) {
    std::string raw = R"({"schemaVersion":1,"nodes":[)";
    raw.reserve(nodeCount * 180U);
    for (std::size_t index = 0; index < nodeCount; ++index) {
      if (index != 0U) raw.push_back(',');
      raw += R"({"id":"n)" + std::to_string(index) +
             R"(","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":)";
      if (index + 1U < nodeCount) {
        raw += R"("n)" + std::to_string(index + 1U) + '"';
      } else {
        raw += "null";
      }
      raw +=
          R"(,"payload":{"kind":"web","source":"https://example.com/","title":""}})";
    }
    raw += "]}";
    return std::vector<std::uint8_t>(raw.begin(), raw.end());
  };

  const auto maximum = storage::DocumentCodec::decodeJson(parentChain(4096U));
  ASSERT_TRUE(maximum.document.has_value()) << maximum.error;
  EXPECT_EQ(maximum.document->nodes().size(), 4096U);

  const auto excessive =
      storage::DocumentCodec::decodeJson(parentChain(4097U));
  EXPECT_FALSE(excessive.document.has_value());
  EXPECT_NE(excessive.error.find("depth"), std::string::npos);
}

TEST(DocumentCodecTest, LoadsAFlatDocumentInBulk) {
  constexpr std::size_t kNodeCount = 5000U;
  std::string raw = R"({"schemaVersion":1,"nodes":[)";
  raw.reserve(kNodeCount * 170U);
  for (std::size_t index = 0; index < kNodeCount; ++index) {
    if (index != 0U) raw.push_back(',');
    raw += R"({"id":"flat-)" + std::to_string(index) +
           R"(","type":"web","layer":"embedded","bounds":[0,0,10,10],"parentId":null,"payload":{"kind":"web","source":"https://example.com/","title":""}})";
  }
  raw += "]}";
  const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
  const auto decoded = storage::DocumentCodec::decodeJson(bytes);
  ASSERT_TRUE(decoded.document.has_value()) << decoded.error;
  EXPECT_EQ(decoded.document->nodes().size(), kNodeCount);
  EXPECT_NE(decoded.document->find("flat-4999"), nullptr);
}

TEST(DocumentCodecTest, JsonEncodingIsCanonicalAndMessagePackDiffers) {
  document::Document input;
  ASSERT_TRUE(input.add(makeEmbedded("web", document::EmbeddedKind::Web)));
  const auto json = storage::DocumentCodec::encodeJson(input);
  const auto msgpack = storage::DocumentCodec::encode(input);
  ASSERT_FALSE(json.empty());
  ASSERT_FALSE(msgpack.empty());
  EXPECT_NE(json, msgpack);
  const std::string jsonText(json.begin(), json.end());
  EXPECT_NE(jsonText.find("\"schemaVersion\":1"), std::string::npos);

  document::Document invalid;
  auto wrongLayer = makeEmbedded("wrong-layer", document::EmbeddedKind::Web);
  wrongLayer.layer = document::LayerClass::Base;
  ASSERT_TRUE(invalid.add(std::move(wrongLayer)));
  EXPECT_TRUE(storage::DocumentCodec::encode(invalid).empty());
  EXPECT_TRUE(storage::DocumentCodec::encodeJson(invalid).empty());
}

#if defined(_WIN32)
TEST(DocumentStoreTest, AtomicRoundTripAndOversizeLoadLimit) {
  EXPECT_TRUE(windows::DocumentStore::supportsDocumentSize(
      windows::DocumentStore::maximumDocumentBytes));
  EXPECT_FALSE(windows::DocumentStore::supportsDocumentSize(
      windows::DocumentStore::maximumDocumentBytes + 1U));

  const auto path = std::filesystem::temp_directory_path() /
                    "canvas-document-store-test.canvas";
  auto tempPath = path;
  tempPath += L".tmp";
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(tempPath, ignored);

  std::string error;
  const std::vector<std::uint8_t> expected{1, 2, 3, 4};
  ASSERT_TRUE(windows::DocumentStore::saveAtomic(path, expected, error))
      << error;
  std::vector<std::uint8_t> actual;
  ASSERT_TRUE(windows::DocumentStore::load(path, actual, error)) << error;
  EXPECT_EQ(actual, expected);
  const std::vector<std::uint8_t> replacement{5, 6, 7};
  ASSERT_TRUE(windows::DocumentStore::saveAtomic(path, replacement, error))
      << error;
  ASSERT_TRUE(windows::DocumentStore::load(path, actual, error)) << error;
  EXPECT_EQ(actual, replacement);
  EXPECT_FALSE(std::filesystem::exists(tempPath));

  HANDLE destinationLock =
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(destinationLock, INVALID_HANDLE_VALUE);
  EXPECT_FALSE(windows::DocumentStore::saveAtomic(path, {8, 9}, error));
  EXPECT_FALSE(error.empty());
  ASSERT_TRUE(CloseHandle(destinationLock));
  ASSERT_TRUE(windows::DocumentStore::load(path, actual, error)) << error;
  EXPECT_EQ(actual, replacement);
  EXPECT_FALSE(std::filesystem::exists(tempPath));

  const auto oversizedPath =
      path.parent_path() / (path.filename().wstring() + L".oversized");
  HANDLE oversized =
      CreateFileW(oversizedPath.c_str(), GENERIC_WRITE, 0, nullptr,
                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(oversized, INVALID_HANDLE_VALUE);
  LARGE_INTEGER offset{};
  offset.QuadPart = 512LL * 1024LL * 1024LL + 1LL;
  ASSERT_TRUE(SetFilePointerEx(oversized, offset, nullptr, FILE_BEGIN));
  ASSERT_TRUE(SetEndOfFile(oversized));
  ASSERT_TRUE(CloseHandle(oversized));
  actual = {9, 9, 9};
  EXPECT_FALSE(windows::DocumentStore::load(oversizedPath, actual, error));
  EXPECT_TRUE(actual.empty());
  EXPECT_FALSE(error.empty());

  std::filesystem::remove(path, ignored);
  std::filesystem::remove(oversizedPath, ignored);
}
#endif
