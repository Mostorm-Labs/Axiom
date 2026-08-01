#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace canvas::macos::detail {

enum class NavigationClass { Denied, Https, PackagedFile, TestData };

struct NavigationPolicyOptions {
  std::string packagedContentRoot;
  bool allowTestDataUrls = false;
};

inline bool asciiEqualsIgnoreCase(std::string_view value,
                                  std::string_view expected) noexcept {
  if (value.size() != expected.size()) return false;
  for (std::size_t i = 0; i < value.size(); ++i) {
    const auto lower = [](char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    if (lower(value[i]) != lower(expected[i])) return false;
  }
  return true;
}

inline bool hasPrefixIgnoreCase(std::string_view value,
                                std::string_view prefix) noexcept {
  return value.size() >= prefix.size() &&
         asciiEqualsIgnoreCase(value.substr(0, prefix.size()), prefix);
}

inline NavigationClass classifyNavigation(
    std::string_view uri, const NavigationPolicyOptions& options) noexcept {
  constexpr std::size_t kMaxUriBytes = 256U * 1024U;
  if (uri.empty() || uri.size() > kMaxUriBytes) return NavigationClass::Denied;

  if (hasPrefixIgnoreCase(uri, "https://")) {
    const auto authorityEnd = uri.find_first_of("/?#", 8U);
    const auto authority = uri.substr(8U, authorityEnd == std::string_view::npos
                                           ? uri.size() - 8U
                                           : authorityEnd - 8U);
    // Credentials and an empty/invalid host are never accepted.  This is a
    // deliberately small fail-closed parser; NSURL performs the full parse
    // again before a request is issued.
    if (authority.empty() || authority.find('@') != std::string_view::npos ||
        authority.find_first_of("\\\t\r\n") != std::string_view::npos)
      return NavigationClass::Denied;
    return NavigationClass::Https;
  }

  if (hasPrefixIgnoreCase(uri, "file://")) {
    if (options.packagedContentRoot.empty()) return NavigationClass::Denied;
    const auto path = uri.substr(7U);
    if (path.empty() || path.front() != '/' || path.find('\\') != std::string_view::npos)
      return NavigationClass::Denied;
    const std::string root = options.packagedContentRoot.front() == '/'
                                 ? options.packagedContentRoot
                                 : '/' + options.packagedContentRoot;
    const auto rootEnd = root.size() > 1U && root.back() == '/'
                             ? root.size() - 1U
                             : root.size();
    // Compare path segments, rejecting traversal rather than merely looking
    // for the substring ".." (e.g. `foo..bar` is a valid filename).
    std::size_t cursor = 1U;
    while (cursor <= path.size()) {
      const auto end = path.find('/', cursor);
      const auto segment = path.substr(cursor, end == std::string_view::npos
                                                  ? path.size() - cursor
                                                  : end - cursor);
      if (segment == "..") return NavigationClass::Denied;
      if (end == std::string_view::npos) break;
      cursor = end + 1U;
    }
    if (path.size() < rootEnd || path.compare(0U, rootEnd, root, 0U, rootEnd) != 0)
      return NavigationClass::Denied;
    if (path.size() > rootEnd && path[rootEnd] != '/') return NavigationClass::Denied;
    return NavigationClass::PackagedFile;
  }

  if (options.allowTestDataUrls && hasPrefixIgnoreCase(uri, "data:text/html")) {
    constexpr std::string_view kDataPrefix = "data:text/html";
    if (uri.size() <= kDataPrefix.size() ||
        (uri[kDataPrefix.size()] != ',' && uri[kDataPrefix.size()] != ';'))
      return NavigationClass::Denied;
    const auto comma = uri.find(',');
    if (comma != std::string_view::npos && uri.size() - comma - 1U <= 128U * 1024U)
      return NavigationClass::TestData;
  }
  return NavigationClass::Denied;
}

enum class NavigationEventAction { Ignore, PromoteNext, DeliverReady, DeliverFailed };
enum class InitialLoadState { NotRequested, Pending, Ready, Failed };

// Keeps the first initial-load terminal identity even when the host installs
// its completion handler after WebKit has already reported the terminal event.
class InitialLoadTerminalRecord final {
 public:
  void record(InitialLoadState state, std::string uri) {
    if (recorded_) return;
    recorded_ = true;
    state_ = state;
    uri_ = std::move(uri);
  }
  bool recorded() const noexcept { return recorded_; }
  InitialLoadState state() const noexcept { return state_; }
  const std::string& uri() const noexcept { return uri_; }

 private:
  bool recorded_ = false;
  InitialLoadState state_ = InitialLoadState::NotRequested;
  std::string uri_;
};

// A platform-independent latest-wins/one-shot initial-load state machine.
// Native delegates only feed it generation and terminal events.
class NavigationTracker final {
 public:
  using Generation = std::uint64_t;
  static constexpr Generation maxGeneration() noexcept {
    return (std::numeric_limits<Generation>::max)() - 1U;
  }

  std::optional<Generation> submit() noexcept {
    if (next_ == 0U) return std::nullopt;
    const Generation generation = next_;
    next_ = next_ == maxGeneration() ? 0U : next_ + 1U;
    queued_ = generation;
    if (state_ == InitialLoadState::NotRequested) state_ = InitialLoadState::Pending;
    return generation;
  }

  std::optional<Generation> issueNext() noexcept {
    if (active_ != 0U || queued_ == 0U) return std::nullopt;
    active_ = queued_;
    queued_ = 0U;
    return active_;
  }

  bool isActive(Generation generation) const noexcept {
    return generation != 0U && generation == active_;
  }
  Generation active() const noexcept { return active_; }
  bool hasQueued() const noexcept { return queued_ != 0U; }

  NavigationEventAction complete(Generation generation, bool success) noexcept {
    if (!isActive(generation)) return NavigationEventAction::Ignore;
    active_ = 0U;
    if (queued_ != 0U) return NavigationEventAction::PromoteNext;
    state_ = success ? InitialLoadState::Ready : InitialLoadState::Failed;
    return success ? NavigationEventAction::DeliverReady
                   : NavigationEventAction::DeliverFailed;
  }

  void abandonActive() noexcept { active_ = 0U; }
  void cancel() noexcept { active_ = queued_ = 0U; }
  InitialLoadState state() const noexcept { return state_; }

 private:
  Generation next_ = 1U;
  Generation active_ = 0U;
  Generation queued_ = 0U;
  InitialLoadState state_ = InitialLoadState::NotRequested;
};

}  // namespace canvas::macos::detail
