#include "platform/windows/dcomp_host.h"

#include <gtest/gtest.h>

namespace {

TEST(WindowsComposition, UsesFixedBackToFrontVisualOrder) {
    constexpr auto order = canvas::windows::DCompHost::visualOrder();
    ASSERT_EQ(order.size(), 4u);
    EXPECT_EQ(order[0], canvas::windows::VisualSlot::BaseCanvas);
    EXPECT_EQ(order[1], canvas::windows::VisualSlot::EmbeddedContent);
    EXPECT_EQ(order[2], canvas::windows::VisualSlot::Annotation);
    EXPECT_EQ(order[3], canvas::windows::VisualSlot::InteractionChrome);
}

}  // namespace
