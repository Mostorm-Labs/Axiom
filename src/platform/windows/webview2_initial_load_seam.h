#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
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

struct NavigationDocumentIdentity {
  std::wstring_view canonicalDocument;
  bool hasFragment = false;
};

enum class NativeNavigationStartExpectation {
  Required,
  NotExpected,
  RequiredOrSameDocumentSource,
};

enum class NativeNavigationSourceChangedAction {
  Ignore,
  ObserveSameDocument,
  ResolveSameDocument,
};

// If the committed source cannot be inspected, a fragment request cannot be
// safely classified as full-document or same-document. Keep its native-start
// admission until either NavigationStarting or SourceChanged(IsNewDocument=
// FALSE) resolves the ambiguity; a non-fragment request remains a normal
// full-document navigation.
inline NativeNavigationStartExpectation
nativeNavigationStartExpectationWhenSourceIsUnavailable(
    bool requestedHasFragment) noexcept {
  return requestedHasFragment
             ? NativeNavigationStartExpectation::RequiredOrSameDocumentSource
             : NativeNavigationStartExpectation::Required;
}

// IsNewDocument == FALSE is WebView2's documented same-document evidence.
// It may arrive synchronously from Navigate(), in which case the adapter only
// records it until Navigate returns, or asynchronously after the native call
// has returned, in which case it can release the temporary start admission.
inline NativeNavigationSourceChangedAction nativeNavigationSourceChangedAction(
    NativeNavigationStartExpectation expectation, bool isNewDocument,
    bool navigateCallInFlight) noexcept {
  if (expectation !=
          NativeNavigationStartExpectation::RequiredOrSameDocumentSource ||
      isNewDocument) {
    return NativeNavigationSourceChangedAction::Ignore;
  }
  return navigateCallInFlight
             ? NativeNavigationSourceChangedAction::ObserveSameDocument
             : NativeNavigationSourceChangedAction::ResolveSameDocument;
}

// WebView2's documented same-document navigation exception is intentionally
// narrow here: a fragment must be present on either side and both inputs must
// already have the same canonical, fragment-free document identity. URI
// canonicalization is Windows-specific and stays outside this portable seam.
inline bool isProvableSameDocumentNavigation(
    NavigationDocumentIdentity committedSource,
    NavigationDocumentIdentity requestedUri) noexcept {
  return (committedSource.hasFragment || requestedUri.hasFragment) &&
         committedSource.canonicalDocument == requestedUri.canonicalDocument;
}

inline bool nativeNavigationExpectsStarting(
    bool committedSourceIsCurrent,
    bool fullDocumentRequestAwaitingStart,
    NavigationDocumentIdentity committedSource,
    NavigationDocumentIdentity requestedUri) noexcept {
  return !committedSourceIsCurrent || fullDocumentRequestAwaitingStart ||
         !isProvableSameDocumentNavigation(committedSource, requestedUri);
}

enum class NavigationStartAfterGetterAction {
  Continue,
  Ignore,
  AbandonCurrentStart,
  CancelStaleEvent,
};

// Event-argument getters are COM calls and may synchronously re-enter the
// surface. A mutation can fully consume a newer request before the outer
// getter returns, leaving neither an issued nor a deferred slot to identify
// it. That outer event is still stale and must be cancelled, not accepted.
inline NavigationStartAfterGetterAction navigationStartAfterGetterAction(
    bool targetIsCurrent,
    bool mutationIsCurrent,
    bool ownsEntryIssuedNavigation,
    bool hasDeferredNavigation) noexcept {
  if (!targetIsCurrent) return NavigationStartAfterGetterAction::Ignore;
  if (mutationIsCurrent) return NavigationStartAfterGetterAction::Continue;
  if (ownsEntryIssuedNavigation || hasDeferredNavigation) {
    return NavigationStartAfterGetterAction::AbandonCurrentStart;
  }
  return NavigationStartAfterGetterAction::CancelStaleEvent;
}

// A generation is issued only after policy validation and URI ownership have
// succeeded. COM calls may synchronously re-enter navigate() or close(); their
// callers must verify the captured generation before applying the result.
class NavigationRequestGenerationTracker final {
 public:
  using Generation = std::uint64_t;

  // UINT64_MAX remains reserved for the tracker's anonymous-navigation
  // sentinel contract; a real host request must never receive it.
  static constexpr Generation maximumRequestGeneration() noexcept {
    return (std::numeric_limits<Generation>::max)() - 1U;
  }

  std::optional<Generation> begin() noexcept {
    if (next_ == 0U) return std::nullopt;
    current_ = next_;
    next_ = next_ == maximumRequestGeneration()
                ? 0U
                : next_ + 1U;
    return current_;
  }

  bool isCurrent(Generation generation) const noexcept {
    return generation != 0U && generation == current_;
  }

  void invalidate() noexcept { current_ = 0U; }

 private:
  Generation current_ = 0U;
  Generation next_ = 1U;
};

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
    return true;
  }

  // A handler belongs to the first navigation request, even if policy rejects
  // that request before it becomes a pending WebView2 navigation.
  void noteNavigationRequest() noexcept { handlerInstallationClosed_ = true; }

  // Admit only a call that is immediately about to enter WebView2::Navigate
  // and is expected to produce NavigationStarting. The platform adapter is a
  // serial scheduler: it never permits a second host Navigate call until the
  // first call's non-redirect start has consumed this slot. A request proven
  // to be same-document still enters WebView2, but it does not take the slot
  // because WebView2 normally emits no starting/completed pair for it.
  bool tryBeginNativeNavigation(
      bool expectsNavigationStarting = true) noexcept {
    if (!expectsNavigationStarting) return true;
    if (pendingNativeNavigationGeneration_ != 0U ||
        pendingAnonymousNativeNavigation_) {
      return false;
    }
    pendingAnonymousNativeNavigation_ = true;
    activeNavigationId_ = 0U;
    activeRequestGeneration_ = 0U;
    return true;
  }

  bool tryBeginNativeNavigationForRequest(
      std::uint64_t requestGeneration,
      bool expectsNavigationStarting = true) noexcept {
    if (!expectsNavigationStarting) return true;
    if (pendingNativeNavigationGeneration_ != 0U ||
        pendingAnonymousNativeNavigation_) {
      return false;
    }
    pendingNativeNavigationGeneration_ = requestGeneration;
    activeNavigationId_ = 0U;
    activeRequestGeneration_ = 0U;
    return true;
  }

  // A native call can return failure after re-entering a newer request. Remove
  // only the failed call's admission so the replacement's future start is not
  // consumed as though it belonged to the old request.
  bool cancelPendingNativeNavigation(
      std::uint64_t requestGeneration) noexcept {
    if (pendingNativeNavigationGeneration_ != requestGeneration) return false;
    pendingNativeNavigationGeneration_ = 0U;
    return true;
  }

  // A source event proving same-document navigation resolves the temporary
  // native-start admission without creating a new active navigation id.
  bool resolveSameDocumentNavigationForRequest(
      std::uint64_t requestGeneration) noexcept {
    return cancelPendingNativeNavigation(requestGeneration);
  }

  // The surface owns the sole issued request, so the event is matched by that
  // slot rather than by URI spelling. Redirect starts belong to the current
  // navigation and never consume the pending host request.
  bool acceptNavigationForRequest(std::uint64_t navigationId,
                                  std::uint64_t requestGeneration,
                                  bool isRedirected = false) noexcept {
    if (navigationId == 0U || requestGeneration == 0U) return false;
    if (isRedirected) {
      return state_ != InitialLoadState::Pending ||
             navigationId == activeNavigationId_;
    }
    if (pendingNativeNavigationGeneration_ != requestGeneration) return false;
    pendingNativeNavigationGeneration_ = 0U;
    activeNavigationId_ = navigationId;
    activeRequestGeneration_ = requestGeneration;
    return true;
  }

  // With no issued host request, a non-redirect start is external navigation.
  // It must never steal the sole native slot if the surface and tracker become
  // inconsistent.
  bool acceptNavigation(std::uint64_t navigationId,
                        bool isRedirected = false) noexcept {
    if (navigationId == 0U) return false;
    if (isRedirected) {
      return state_ != InitialLoadState::Pending ||
             navigationId == activeNavigationId_;
    }
    if (pendingAnonymousNativeNavigation_) {
      pendingAnonymousNativeNavigation_ = false;
      activeNavigationId_ = navigationId;
      activeRequestGeneration_ = 0U;
      return true;
    }
    if (pendingNativeNavigationGeneration_ != 0U) return false;
    activeNavigationId_ = navigationId;
    activeRequestGeneration_ = 0U;
    return true;
  }

  void retireActiveNavigation() noexcept {
    activeNavigationId_ = 0U;
    activeRequestGeneration_ = 0U;
  }

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
    retireActiveNavigation();
    pendingNativeNavigationGeneration_ = 0U;
    pendingAnonymousNativeNavigation_ = false;
  }

  InitialLoadState state() const noexcept { return state_; }
  InitialLoadCompletion completion() const noexcept { return completion_; }
  std::uint64_t activeNavigationId() const noexcept {
    return activeNavigationId_;
  }
  std::uint32_t pendingNativeNavigationStarts() const noexcept {
    return pendingNativeNavigationGeneration_ == 0U &&
                   !pendingAnonymousNativeNavigation_
               ? 0U
               : 1U;
  }

 private:
  bool complete(InitialLoadCompletion completion) noexcept {
    state_ = completion.state;
    completion_ = completion;
    retireActiveNavigation();
    pendingNativeNavigationGeneration_ = 0U;
    pendingAnonymousNativeNavigation_ = false;

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
  std::uint64_t activeRequestGeneration_ = 0U;
  std::uint64_t pendingNativeNavigationGeneration_ = 0U;
  bool pendingAnonymousNativeNavigation_ = false;
  InitialLoadCompletionHandler handler_;
  bool handlerSet_ = false;
  bool handlerInstallationClosed_ = false;
};

}  // namespace canvas::windows::detail
