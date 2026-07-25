#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace canvas::windows::detail {

// This small tracker deliberately has no Win32 or WebView2 dependency. It
// owns the one-shot state needed to distinguish a usable controller from the
// first document becoming ready for host messages.
enum class InitialLoadState { NotRequested, Pending, Ready, Failed };

struct InitialLoadCompletion {
  InitialLoadState state = InitialLoadState::NotRequested;
  std::int32_t result = 0;
};

using InitialLoadCompletionHandler =
    std::function<void(InitialLoadCompletion completion)>;

class InitialLoadTracker final {
 public:
  bool setCompletionHandler(InitialLoadCompletionHandler handler) {
    if (handlerSet_ || handlerInstallationClosed_ ||
        state_ != InitialLoadState::NotRequested || !handler) {
      return false;
    }
    handler_ = std::move(handler);
    handlerSet_ = true;
    return true;
  }

  bool request() noexcept {
    noteNavigationRequest();
    if (state_ != InitialLoadState::NotRequested) return false;
    state_ = InitialLoadState::Pending;
    completion_ = {InitialLoadState::Pending, 0};
    activeNavigationId_ = 0U;
    return true;
  }

  // A handler belongs to the first navigation request, even if policy rejects
  // that request before it becomes a pending WebView2 navigation.
  void noteNavigationRequest() noexcept { handlerInstallationClosed_ = true; }

  void acceptNavigation(std::uint64_t navigationId) noexcept {
    if (state_ == InitialLoadState::Pending && navigationId != 0U) {
      activeNavigationId_ = navigationId;
    }
  }

  // A replacement Navigate() can synchronously complete the navigation it
  // supersedes before WebView2 reports NavigationStarting for the replacement.
  // Retire the old identity immediately before entering WebView2 so that the
  // cancelled completion cannot terminate the one-shot initial load.
  void retireActiveNavigation() noexcept { activeNavigationId_ = 0U; }

  bool completeSuccessForNavigation(std::uint64_t navigationId) noexcept {
    if (state_ != InitialLoadState::Pending || navigationId == 0U ||
        navigationId != activeNavigationId_) {
      return false;
    }
    return complete({InitialLoadState::Ready, 0});
  }

  bool completeFailure(std::int32_t result) noexcept {
    if (state_ != InitialLoadState::Pending) return false;
    return complete({InitialLoadState::Failed, result < 0 ? result : -1});
  }

  // Closing the surface cancels delivery but intentionally does not invent a
  // Failed terminal result: there is no Cancelled public state.
  void cancel() noexcept {
    handler_ = nullptr;
    activeNavigationId_ = 0U;
  }

  InitialLoadState state() const noexcept { return state_; }
  InitialLoadCompletion completion() const noexcept { return completion_; }
  std::uint64_t activeNavigationId() const noexcept {
    return activeNavigationId_;
  }

 private:
  bool complete(InitialLoadCompletion completion) noexcept {
    state_ = completion.state;
    completion_ = completion;
    activeNavigationId_ = 0U;

    // Move ownership out before invoking arbitrary client code. That makes
    // every terminal path one-shot, including a re-entrant callback.
    auto handler = std::move(handler_);
    handler_ = nullptr;
    if (!handler) return true;
    try {
      handler(completion_);
    } catch (...) {
      // WebView2 invokes us through COM. Client exceptions must not escape.
    }
    return true;
  }

  InitialLoadState state_ = InitialLoadState::NotRequested;
  InitialLoadCompletion completion_{};
  std::uint64_t activeNavigationId_ = 0U;
  InitialLoadCompletionHandler handler_;
  bool handlerSet_ = false;
  bool handlerInstallationClosed_ = false;
};

}  // namespace canvas::windows::detail
