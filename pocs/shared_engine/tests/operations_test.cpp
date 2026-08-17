#include <gtest/gtest.h>

#include <string>

#include "foundation.h"
#include "operations.h"
#include "test_fixture.h"

namespace canvas::poc01::test {
namespace {

TEST(OperationsTest, CreateMoveDeleteMutateExpectedState) {
  auto document = MakeDocument();
  ASSERT_EQ(ApplyOperations(*document, FixedReplay()), CANVAS_POC_STATUS_OK);
  ASSERT_TRUE(document->state().nodes.contains(1));
  EXPECT_FALSE(document->state().nodes.contains(999));
  const NodeHeader& moved = Header(document->state().nodes.at(1));
  EXPECT_FLOAT_EQ(moved.translation_x, 12.0F);
  EXPECT_FLOAT_EQ(moved.translation_y, 8.0F);
}

TEST(OperationsTest, SequenceGapRejectsWholeBatch) {
  auto document = MakeDocument();
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":[0,0,0,255]}}\n"
      "{\"v\":1,\"seq\":3,\"op\":\"move\",\"id\":1,\"dx\":1,\"dy\":1}\n";
  EXPECT_EQ(ApplyOperations(*document, replay),
            CANVAS_POC_STATUS_SEQUENCE_ERROR);
  EXPECT_TRUE(document->state().nodes.empty());
  EXPECT_EQ(document->state().last_sequence, 0U);
}

TEST(OperationsTest, MissingNodeRejectsWholeBatch) {
  auto document = MakeDocument();
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"move\",\"id\":42,\"dx\":1,\"dy\":1}\n";
  EXPECT_EQ(ApplyOperations(*document, replay), CANVAS_POC_STATUS_NOT_FOUND);
  EXPECT_EQ(document->state().last_sequence, 0U);
}

TEST(OperationsTest, DuplicateIdRejectsWholeBatch) {
  auto document = MakeDocument();
  const std::string node =
      "{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"color\":[0,0,0,255]}";
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":" + node +
      "}\n{\"v\":1,\"seq\":2,\"op\":\"create\",\"node\":" + node +
      "}\n";
  EXPECT_EQ(ApplyOperations(*document, replay),
            CANVAS_POC_STATUS_ALREADY_EXISTS);
  EXPECT_TRUE(document->state().nodes.empty());
}

TEST(OperationsTest, UnknownTypeAndFieldsAreRejected) {
  auto document = MakeDocument();
  EXPECT_EQ(ApplyOperations(
                *document,
                "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"video\",\"order\":1}}\n"),
            CANVAS_POC_STATUS_PARSE_ERROR);
  EXPECT_EQ(ApplyOperations(
                *document,
                "{\"v\":1,\"seq\":1,\"op\":\"delete\",\"id\":1,\"extra\":true}\n"),
            CANVAS_POC_STATUS_PARSE_ERROR);
}

TEST(OperationsTest, NonFiniteAndOverflowFloatsAreRejected) {
  auto document = MakeDocument();
  const std::string huge =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":1,\"type\":\"rect\",\"order\":1,\"x\":1e400,\"y\":0,\"width\":10,\"height\":10,\"color\":[0,0,0,255]}}\n";
  EXPECT_EQ(ApplyOperations(*document, huge), CANVAS_POC_STATUS_PARSE_ERROR);
  EXPECT_TRUE(document->state().nodes.empty());
}

TEST(OperationsTest, MissingAssetRejectsCreate) {
  auto document = MakeDocument();
  const std::string replay =
      "{\"v\":1,\"seq\":1,\"op\":\"create\",\"node\":{\"id\":2,\"type\":\"image\",\"order\":1,\"x\":0,\"y\":0,\"width\":10,\"height\":10,\"asset_key\":\"missing\"}}\n";
  EXPECT_EQ(ApplyOperations(*document, replay), CANVAS_POC_STATUS_PARSE_ERROR);
  EXPECT_TRUE(document->state().nodes.empty());
}

}  // namespace
}  // namespace canvas::poc01::test
