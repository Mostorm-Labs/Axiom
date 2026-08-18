#include <gtest/gtest.h>

#include "canvas_poc02/ink_engine.h"
#include "test_support.h"

namespace canvas::poc02 {
namespace {

TEST(DocumentReplay, AddStrokeRoundTripReproducesFreshDocument) {
  StrokeDocument original;
  DefaultPreviewSink sink;
  const AddStrokeOperation operation =
      test::RunFixture("vector-pressure.ndjson", &original, &sink);
  const std::string ndjson = SerializeAddStrokeNdjson(operation);
  AddStrokeOperation decoded;
  std::string error;
  ASSERT_EQ(ParseAddStrokeNdjson(ndjson, &decoded, &error), Status::kOk) << error;
  StrokeDocument replayed;
  ASSERT_EQ(replayed.Apply(decoded), Status::kOk);
  EXPECT_EQ(replayed.Digest(), original.Digest());
  EXPECT_EQ(StrokeDigest(decoded.stroke), StrokeDigest(operation.stroke));
  EXPECT_EQ(replayed.revision(), original.revision());
  EXPECT_EQ(replayed.operation_sequence(), original.operation_sequence());
}

TEST(DocumentReplay, SequenceAndDuplicateIdAreTransactional) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  AddStrokeOperation operation =
      test::RunFixture("vector-pressure.ndjson", &document, &sink);
  const std::string digest = document.Digest();
  operation.sequence = 3;
  EXPECT_EQ(document.Apply(operation), Status::kSequenceError);
  EXPECT_EQ(document.Digest(), digest);
  operation.sequence = 2;
  EXPECT_EQ(document.Apply(operation), Status::kInvalidArgument);
  EXPECT_EQ(document.Digest(), digest);
}

TEST(DocumentReplay, UnknownFieldsAndVersionsAreRejected) {
  StrokeDocument document;
  DefaultPreviewSink sink;
  const AddStrokeOperation operation =
      test::RunFixture("vector-pressure.ndjson", &document, &sink);
  std::string encoded = SerializeAddStrokeNdjson(operation);
  encoded.insert(encoded.find('{') + 1, "\"unknown\":1,");
  AddStrokeOperation decoded;
  std::string error;
  EXPECT_EQ(ParseAddStrokeNdjson(encoded, &decoded, &error), Status::kParseError);
  AddStrokeOperation unsupported = operation;
  unsupported.schema_version = 2;
  EXPECT_EQ(document.Apply(unsupported), Status::kUnsupportedVersion);
}

}  // namespace
}  // namespace canvas::poc02
