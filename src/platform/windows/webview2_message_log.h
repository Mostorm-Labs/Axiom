#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace canvas::windows::detail {

// WebView2 exposes messages as UTF-16 strings. Keep both an individual
// message and the retained diagnostic/queue memory bounded even when a page
// (or a native caller) sends unexpectedly large data.
inline constexpr std::size_t kWebView2MaxMessageCodeUnits =
    1U * 1024U * 1024U;
inline constexpr std::size_t kWebView2MaxTotalMessageCodeUnits =
    4U * 1024U * 1024U;
inline constexpr std::size_t kWebView2MaxNavigationCodeUnits =
    1U * 1024U * 1024U;

enum class MessagePushResult {
  Added,
  Oversized,
  AllocationFailure,
};

template <std::size_t MaxMessages>
class BoundedWebView2MessageQueue final {
  static_assert(MaxMessages > 0U);

 public:
  static constexpr std::size_t maxSize() noexcept { return MaxMessages; }
  static constexpr std::size_t maxMessageCodeUnits() noexcept {
    return kWebView2MaxMessageCodeUnits;
  }
  static constexpr std::size_t maxTotalCodeUnits() noexcept {
    return kWebView2MaxTotalMessageCodeUnits;
  }

  // The view overload checks the size before allocating a copy. All failures
  // are converted to a result so diagnostics and WebView callbacks cannot
  // let C++ allocation exceptions escape into COM.
  MessagePushResult tryPush(std::wstring_view message) noexcept {
    if (message.size() > maxMessageCodeUnits() ||
        message.size() > maxTotalCodeUnits()) {
      return MessagePushResult::Oversized;
    }

    try {
      std::wstring copy(message);
      while (!values_.empty() &&
             (values_.size() >= maxSize() ||
              totalCodeUnits_ > maxTotalCodeUnits() - copy.size())) {
        evictOldest();
      }
      values_.push_back(std::move(copy));
      totalCodeUnits_ += values_.back().size();
      return MessagePushResult::Added;
    } catch (...) {
      return MessagePushResult::AllocationFailure;
    }
  }

  // Preserve the original convenience API for existing diagnostics callers.
  void push(std::wstring_view message) noexcept {
    (void)tryPush(message);
  }

  void clear() noexcept {
    values_.clear();
    totalCodeUnits_ = 0U;
  }

  // Detach the current generation before entering arbitrary WebView2 code.
  // A synchronous callback may clear or append to this queue; iterating the
  // detached vector remains valid and any newly queued values stay isolated.
  std::vector<std::wstring> takeValues() noexcept {
    std::vector<std::wstring> detached;
    detached.swap(values_);
    totalCodeUnits_ = 0U;
    return detached;
  }

  std::size_t totalCodeUnits() const noexcept { return totalCodeUnits_; }

  const std::vector<std::wstring>& values() const noexcept { return values_; }

 private:
  void evictOldest() {
    if (values_.empty()) return;
    const std::size_t removedCodeUnits = values_.front().size();
    values_.erase(values_.begin());
    totalCodeUnits_ -= removedCodeUnits;
  }

  std::vector<std::wstring> values_;
  std::size_t totalCodeUnits_ = 0U;
};

using WebView2MessageLog = BoundedWebView2MessageQueue<256U>;
using WebView2PendingMessageQueue = BoundedWebView2MessageQueue<64U>;

}  // namespace canvas::windows::detail
