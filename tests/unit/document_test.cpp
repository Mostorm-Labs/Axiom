#include "canvas/document/document.h"

#include <gtest/gtest.h>

#include <limits>

using namespace canvas;

TEST(DocumentTest, ReportsSchemaVersion) {
    EXPECT_EQ(document::Document::schemaVersion, 1);
}

TEST(DocumentTest, AddsAndMovesAnEmbeddedNode) {
    document::Document doc;
    document::Node node;
    node.id = "web-1";
    node.layer = document::LayerClass::Embedded;
    node.bounds = {10, 20, 640, 480};
    node.payload = document::EmbeddedNode{
        document::EmbeddedKind::Web, "https://example.com", "Example"};
    ASSERT_TRUE(doc.add(node));
    ASSERT_TRUE(doc.setBounds("web-1", {40, 50, 800, 600}));
    ASSERT_NE(doc.find("web-1"), nullptr);
    EXPECT_EQ(doc.find("web-1")->bounds,
              (core::Rect{40, 50, 800, 600}));
}

TEST(DocumentTest, DeletesAttachedAnnotationsWithParent) {
    document::Document doc;
    document::Node web{"web-1", document::LayerClass::Embedded,
                       {0, 0, 640, 480}, {}, document::EmbeddedNode{
                           document::EmbeddedKind::Web, "https://example.com",
                           "Example"}};
    document::Node ink{"ink-1", document::LayerClass::Annotation,
                       {10, 10, 100, 40}, "web-1", document::StrokeNode{}};
    ASSERT_TRUE(doc.add(web));
    ASSERT_TRUE(doc.add(ink));
    ASSERT_TRUE(doc.erase("web-1"));
    EXPECT_EQ(doc.find("web-1"), nullptr);
    EXPECT_EQ(doc.find("ink-1"), nullptr);
}

TEST(DocumentTest, RejectsDuplicateEmptyAndOrphanNodes) {
    document::Document doc;
    document::Node base;
    base.id = "base";
    base.bounds = {0, 0, 10, 10};
    ASSERT_TRUE(doc.add(base));
    EXPECT_FALSE(doc.add(base));

    document::Node empty;
    EXPECT_FALSE(doc.add(empty));

    document::Node orphan;
    orphan.id = "orphan";
    orphan.parentId = "missing";
    EXPECT_FALSE(doc.add(orphan));
}

TEST(DocumentTest, RejectsInvalidBoundsAndUnknownIds) {
    document::Document doc;
    document::Node node;
    node.id = "node";
    node.bounds = {0, 0, 10, 10};
    ASSERT_TRUE(doc.add(node));

    EXPECT_FALSE(doc.setBounds("unknown", {0, 0, 10, 10}));
    EXPECT_FALSE(doc.setBounds("node", {0, 0, 0, 10}));
    EXPECT_FALSE(doc.setBounds("node", {0, 0, 10, 0}));
    EXPECT_FALSE(doc.setBounds("node", {0, 0, -1, 10}));
    EXPECT_TRUE(doc.setBounds("node", {2, 3, 4, 5}));
    EXPECT_EQ(doc.find("node")->bounds, (core::Rect{2, 3, 4, 5}));
}

TEST(DocumentTest, PreservesOrderAndEraseReturnValues) {
    document::Document doc;
    for (const char* id : {"first", "second", "third"}) {
        document::Node node;
        node.id = id;
        node.bounds = {0, 0, 1, 1};
        ASSERT_TRUE(doc.add(node));
    }
    ASSERT_EQ(doc.nodes().size(), 3U);
    EXPECT_EQ(doc.nodes()[0].id, "first");
    EXPECT_EQ(doc.nodes()[1].id, "second");
    EXPECT_EQ(doc.nodes()[2].id, "third");
    EXPECT_TRUE(doc.erase("second"));
    EXPECT_FALSE(doc.erase("second"));
    ASSERT_EQ(doc.nodes().size(), 2U);
    EXPECT_EQ(doc.nodes()[0].id, "first");
    EXPECT_EQ(doc.nodes()[1].id, "third");
}

TEST(DocumentTest, ConstFindAndDirectChildCascadeOnly) {
    document::Document doc;
    document::Node root{"root", document::LayerClass::Embedded,
                        {0, 0, 10, 10}, {}, document::EmbeddedNode{}};
    document::Node child{"child", document::LayerClass::Annotation,
                         {0, 0, 1, 1}, "root", document::StrokeNode{}};
    document::Node grandchild{"grandchild", document::LayerClass::Annotation,
                              {0, 0, 1, 1}, "child", document::StrokeNode{}};
    ASSERT_TRUE(doc.add(root));
    ASSERT_TRUE(doc.add(child));
    ASSERT_TRUE(doc.add(grandchild));

    const document::Document& constDoc = doc;
    ASSERT_NE(constDoc.find("root"), nullptr);
    EXPECT_EQ(constDoc.find("missing"), nullptr);

    ASSERT_TRUE(doc.erase("root"));
    EXPECT_EQ(doc.find("root"), nullptr);
    EXPECT_EQ(doc.find("child"), nullptr);
    EXPECT_NE(doc.find("grandchild"), nullptr);
}

TEST(DocumentTest, SupportsUnknownPayloadPlaceholder) {
    document::Document doc;
    document::Node unknown{"future", document::LayerClass::Base,
                           {0, 0, 20, 20}, {}, document::UnknownNode{
                               "future-type", "{\"x\":1}"}};
    ASSERT_TRUE(doc.add(unknown));
    const auto* found = doc.find("future");
    ASSERT_NE(found, nullptr);
    ASSERT_TRUE(std::holds_alternative<document::UnknownNode>(found->payload));
    const auto& payload = std::get<document::UnknownNode>(found->payload);
    EXPECT_EQ(payload.typeName, "future-type");
    EXPECT_EQ(payload.rawJson, "{\"x\":1}");
}

TEST(DocumentTest, EraseCopiesAliasedIdBeforeCompactingNodes) {
    document::Document doc;
    document::Node root{"root", document::LayerClass::Embedded,
                        {0, 0, 10, 10}, {}, document::EmbeddedNode{}};
    document::Node sibling{"sibling", document::LayerClass::Base,
                           {10, 0, 10, 10}, {}, document::StrokeNode{}};
    document::Node child{"child", document::LayerClass::Annotation,
                         {0, 0, 1, 1}, "root", document::StrokeNode{}};
    ASSERT_TRUE(doc.add(root));
    ASSERT_TRUE(doc.add(sibling));
    ASSERT_TRUE(doc.add(child));

    const std::string_view key = doc.find("root")->id;
    ASSERT_TRUE(doc.erase(key));
    EXPECT_EQ(doc.find("root"), nullptr);
    EXPECT_EQ(doc.find("child"), nullptr);
    EXPECT_NE(doc.find("sibling"), nullptr);
}

TEST(DocumentTest, RejectsNaNBoundsAndPreservesPreviousBounds) {
    document::Document doc;
    document::Node node;
    node.id = "node";
    node.bounds = {1, 2, 3, 4};
    ASSERT_TRUE(doc.add(node));
    const auto nan = std::numeric_limits<float>::quiet_NaN();

    ASSERT_FALSE(doc.setBounds("node", {10, 20, nan, 40}));
    ASSERT_NE(doc.find("node"), nullptr);
    EXPECT_EQ(doc.find("node")->bounds, (core::Rect{1, 2, 3, 4}));

    ASSERT_FALSE(doc.setBounds("node", {10, 20, 30, nan}));
    ASSERT_NE(doc.find("node"), nullptr);
    EXPECT_EQ(doc.find("node")->bounds, (core::Rect{1, 2, 3, 4}));
}

TEST(DocumentTest, BulkReplacementSupportsChildFirstAndIsAtomicOnFailure) {
    document::Document doc;
    document::Node sentinel;
    sentinel.id = "sentinel";
    sentinel.bounds = {0, 0, 1, 1};
    ASSERT_TRUE(doc.add(std::move(sentinel)));

    document::Node child{"child", document::LayerClass::Annotation,
                         {0, 0, 1, 1}, "parent", document::StrokeNode{}};
    document::Node parent{"parent", document::LayerClass::Embedded,
                          {0, 0, 10, 10}, {}, document::EmbeddedNode{}};
    ASSERT_TRUE(doc.replaceValidatedNodes({child, parent}));
    EXPECT_EQ(doc.find("sentinel"), nullptr);
    ASSERT_NE(doc.find("child"), nullptr);
    EXPECT_EQ(doc.find("child")->parentId, "parent");

    const auto instanceBeforeFailure = doc.instanceId();
    document::Node first{"a", document::LayerClass::Base,
                         {0, 0, 1, 1}, "b", document::StrokeNode{}};
    document::Node second{"b", document::LayerClass::Base,
                          {0, 0, 1, 1}, "a", document::StrokeNode{}};
    EXPECT_FALSE(doc.replaceValidatedNodes({first, second}));
    EXPECT_EQ(doc.instanceId(), instanceBeforeFailure);
    EXPECT_NE(doc.find("parent"), nullptr);
    EXPECT_NE(doc.find("child"), nullptr);
}
