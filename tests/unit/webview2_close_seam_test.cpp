#include "platform/windows/webview2_close_seam.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace {

enum class CloseStep : std::size_t {
  RemoveHandlers,
  ClearMappings,
  DetachRoot,
  HideController,
  CloseController,
  RemoveVisual,
  Commit,
  Count,
};

const std::vector<CloseStep> kExpectedOrder{
    CloseStep::RemoveHandlers, CloseStep::ClearMappings,
    CloseStep::DetachRoot, CloseStep::HideController,
    CloseStep::CloseController, CloseStep::RemoveVisual, CloseStep::Commit};

class FakeCloseOperations final
    : public canvas::windows::detail::WebView2CloseOperations {
 public:
  HRESULT removeEventHandlers() noexcept override {
    return record(CloseStep::RemoveHandlers);
  }
  HRESULT clearVirtualHostMappings() noexcept override {
    return record(CloseStep::ClearMappings);
  }
  HRESULT detachRootVisualTarget() noexcept override {
    return record(CloseStep::DetachRoot);
  }
  HRESULT hideController() noexcept override {
    return record(CloseStep::HideController);
  }
  HRESULT closeController() noexcept override {
    return record(CloseStep::CloseController);
  }
  HRESULT removeChildVisual() noexcept override {
    return record(CloseStep::RemoveVisual);
  }
  HRESULT commitComposition() noexcept override {
    return record(CloseStep::Commit);
  }

  HRESULT record(CloseStep step) noexcept {
    calls.push_back(step);
    return results[static_cast<std::size_t>(step)];
  }

  std::array<HRESULT, static_cast<std::size_t>(CloseStep::Count)> results{};
  std::vector<CloseStep> calls;
};

TEST(WebView2CloseSeam, ControllerCloseFailurePropagatesWithoutShortCircuit) {
  FakeCloseOperations operations;
  operations.results[static_cast<std::size_t>(CloseStep::CloseController)] =
      E_ACCESSDENIED;

  EXPECT_EQ(canvas::windows::detail::runWebView2CloseOperations(operations),
            E_ACCESSDENIED);
  EXPECT_EQ(operations.calls, kExpectedOrder);
}

TEST(WebView2CloseSeam, FirstFailureWinsAndCompositionCommitStillRuns) {
  FakeCloseOperations operations;
  operations.results[static_cast<std::size_t>(CloseStep::CloseController)] =
      E_ACCESSDENIED;
  operations.results[static_cast<std::size_t>(CloseStep::Commit)] = E_ABORT;

  EXPECT_EQ(canvas::windows::detail::runWebView2CloseOperations(operations),
            E_ACCESSDENIED);
  EXPECT_EQ(operations.calls, kExpectedOrder);
}

TEST(WebView2CloseSeam, CommitFailureIsReturnedAfterAllPriorCleanup) {
  FakeCloseOperations operations;
  operations.results[static_cast<std::size_t>(CloseStep::Commit)] = E_ABORT;

  EXPECT_EQ(canvas::windows::detail::runWebView2CloseOperations(operations),
            E_ABORT);
  EXPECT_EQ(operations.calls, kExpectedOrder);
}

}  // namespace
